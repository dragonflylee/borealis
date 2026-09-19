/*
    Copyright 2022 xfangfang

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#pragma once

#include <stdexcept>

#ifdef PS5_NATIVE_GPU
#include <cstdio>
#include <limits>
#include <list>
#include <string>
#include <unordered_map>
#endif

#include "borealis/core/singleton.hpp"

namespace brls
{

template <typename K, typename T>
struct Node
{
    K key;
    T value;

    /// Cache entries marked as dirty will be refreshed when fetching the cache next time
    bool dirty = false;

    /// Reference count, 1 for each cache hit
    size_t count = 1;

    Node(K k, T v)
        : key(k)
        , value(v)
    {
    }
};

/**
 * LRU cache
 * If the reference count is not 0, the cache will never expire.
 * Cache items with a reference count of 0 are cached according to LRU rules
 */
#ifdef PS5_NATIVE_GPU
// Native textures have immutable handles while acquired by views. All calls,
// including close(), must run on the UI thread while its NVG context is alive.
template <typename K, typename T>
class LRUCache
{
  public:
    using CacheIter = typename std::list<Node<K, T>>::iterator;
    inline static constexpr size_t DEFAULT_CAPACITY = 32;
    inline static bool ALWAYS_CACHE_LOCAL_FILE      = true;

    LRUCache(size_t c, T defaultValue)
        : capacity(c)
        , defaultValue(defaultValue)
    {
        if (c < 1 || c > DEFAULT_CAPACITY)
            throw std::logic_error("Native cache retention target must be between 1 and 32.");
    }

    LRUCache(const LRUCache&)            = delete;
    LRUCache& operator=(const LRUCache&) = delete;

    T get(const K& key)
    {
        if (closing)
            return defaultValue;
        const auto found = cacheMap.find(key);
        if (found == cacheMap.end() || found->second->count == std::numeric_limits<size_t>::max())
            return defaultValue;
        auto item = found->second;
        ++item->count;
        // list::splice preserves both indexes' iterators.
        cacheList.splice(cacheList.begin(), cacheList, item);
        return item->value;
    }

    // Success takes ownership and grants one reference. Rejection or allocation
    // failure leaves the input with its caller, without evicting existing entries.
    bool trySet(const K& key, T value)
    {
        if (closing || value == defaultValue)
            return false;
        auto item = cacheList.end();
        auto keyEntry = cacheMap.end();
        bool keyInserted = false;
        try
        {
            if (cacheMap.find(key) != cacheMap.end() || valueMap.find(value) != valueMap.end())
                return false;
            cacheList.emplace_front(key, value);
            item = cacheList.begin();
            const auto keyResult = cacheMap.emplace(item->key, item);
            if (!keyResult.second)
            {
                cacheList.erase(item);
                return false;
            }
            keyEntry = keyResult.first;
            keyInserted = true;
            if (!valueMap.emplace(value, item).second)
            {
                cacheMap.erase(keyEntry);
                cacheList.erase(item);
                return false;
            }
        }
        catch (...)
        {
            if (keyInserted)
                cacheMap.erase(keyEntry);
            if (item != cacheList.end())
                cacheList.erase(item);
            return false;
        }
        trim();
        return true;
    }

    void remove(T value)
    {
        if (closing)
            return;
        const auto found = valueMap.find(value);
        if (found == valueMap.end() || found->second->count == 0)
            return;
        --found->second->count;
        trim();
    }

    void setCapacity(int c)
    {
        if (c < 1 || c > static_cast<int>(DEFAULT_CAPACITY))
            throw std::logic_error("Native cache retention target must be between 1 and 32.");
        capacity = static_cast<size_t>(c);
        trim();
    }

    void markDirty(T value)
    {
        if (closing)
            return;
        const auto found = valueMap.find(value);
        if (found == valueMap.end() || found->second->dirty)
            return;
        auto item = found->second;
        cacheMap.erase(item->key);
        item->dirty = true;
    }

    void markAllDirty()
    {
        if (closing)
            return;
        cacheMap.clear();
        for (auto& item : cacheList)
            item.dirty = true;
    }

    bool isClosing() const { return closing; }
    size_t getCapacity() const { return capacity; }
    const std::list<Node<K, T>>& getCacheList() const { return cacheList; }

    // Exit notification precedes view destruction. Remove every lookup first,
    // then delete handles once; late releases and repeated cleanup are harmless.
    void close()
    {
        if (closing)
            return;
        closing = true;
        cacheMap.clear();
        valueMap.clear();
        std::list<Node<K, T>> retired;
        retired.swap(cacheList);
        auto* vg = brls::Application::getNVGContext();
        for (const auto& item : retired)
            nvgDeleteImage(vg, item.value);
    }

    void debug()
    {
        printf("===== native cache size: %zu, retention target: %zu, closing: %d =====\n",
            cacheList.size(), capacity, closing);
        for (const auto& item : cacheList)
            printf("count: %zu, dirty: %d, value: %zu, key: %s\n", item.count,
                item.dirty, item.value, item.key.c_str());
    }

  private:
    size_t capacity;
    T defaultValue;
    bool closing = false;
    std::list<Node<K, T>> cacheList;
    std::unordered_map<K, CacheIter> cacheMap;
    std::unordered_map<T, CacheIter> valueMap;

    void trim()
    {
        // The target is soft: referenced entries survive even above the target.
        // A forward iterator anchored after the candidate stays valid on erase.
        auto cursor = cacheList.end();
        while (cacheList.size() > capacity && cursor != cacheList.begin())
        {
            auto item = cursor;
            --item;
            if (item->count != 0)
            {
                cursor = item;
                continue;
            }
            const auto value = item->value;
            const auto keyEntry = cacheMap.find(item->key);
            // A dirty entry can share its original key with a newer clean entry.
            if (keyEntry != cacheMap.end() && keyEntry->second == item)
                cacheMap.erase(keyEntry);
            valueMap.erase(value);
            cursor = cacheList.erase(item);
            nvgDeleteImage(brls::Application::getNVGContext(), value);
        }
    }
};
#else
template <typename K, typename T>
class LRUCache
{
  public:
    typedef typename std::list<Node<K, T>>::iterator CacheIter;
#define DIRTY "_$dirty$"
    inline static size_t DEFAULT_CAPACITY      = 400;
    inline static bool ALWAYS_CACHE_LOCAL_FILE = true;

    LRUCache(size_t c, T defaultValue)
        : capacity(c)
        , defaultValue(defaultValue)
    {
        if (c < 1)
            throw std::logic_error("Cache capacity cannot less than 1.");
    }

    T get(K key)
    {
        if (!isCacheHit(key))
        {
            // Cache not hit
            return defaultValue;
        }

        // Cache hit
        cacheList.splice(cacheList.begin(), cacheList, cacheMap[key]);
        cacheMap[key]                  = cacheList.begin();
        valueMap[cacheMap[key]->value] = cacheList.begin();
        cacheMap[key]->count++;
        return cacheMap[key]->value;
    }

    void set(K key, T value)
    {
        if (isCacheHit(key))
        {
            throw std::logic_error("Can not cache the same key twice.");
        }

        // Check capacity limits
        if (cacheList.size() >= capacity)
        {
            deleteCache(cacheList.size() - capacity);
        }
        // Add new cache
        cacheList.push_front(Node<K, T>(key, value));

        // Update the values of two maps
        cacheMap[key]                  = cacheList.begin();
        valueMap[cacheMap[key]->value] = cacheList.begin();
    }

    /**
     * It is not really to remove a cache, but to reduce the counter of the corresponding cache by 1
     */
    void remove(T value)
    {
        if (!isExisted(value))
        {
            return;
        }
        valueMap[value]->count--;
    }

    /**
     * Update a cache value
     */
    void update(T old_val, T new_val)
    {
        if (!isExisted(old_val))
        {
            return;
        }
        valueMap[old_val]->value = new_val;
        valueMap[new_val]        = valueMap[old_val];
        valueMap.erase(old_val);
    }

    void setCapacity(int c)
    {
        if (c < 1)
            throw std::logic_error("Cache capacity cannot less than 1.");
        // The setting standard of the DEFAULT_CAPACITY should be the upper limit of the number of all pictures.
        c += DEFAULT_CAPACITY;
        this->capacity = c;
        if (cacheList.size() > capacity)
        {
            deleteCache(cacheList.size() - capacity);
        }
    }

    /**
     * A dirty cache is not able to be hit
     * @param value
     */
    void markDirty(T value)
    {
        if (!isExisted(value))
        {
            return;
        }

        auto item = valueMap[value];
        if (item->dirty)
            return;

        // modify existing Key.
        cacheMap.erase(item->key);
        item->key += DIRTY;
        item->dirty = true;
    }

    void markAllDirty()
    {
        for (auto& i : cacheList)
        {
            if (i.dirty)
                continue;
            cacheMap.erase(i.key);
            i.key += DIRTY;
            i.dirty = true;
        }
    }

    std::list<Node<K, T>>& getCacheList() { return cacheList; }

    void debug()
    {
        printf("===== cache size: %zu =====\n", cacheList.size());
        for (auto& i : cacheList)
        {
            printf("count: %zu, dirty: %d, value: %zu, key: %s\n", i.count,
                i.dirty, i.value, i.key.c_str());
        }
    }

  private:
    size_t capacity = 1;
    T defaultValue;
    std::list<Node<K, T>> cacheList;
    std::unordered_map<K, CacheIter> cacheMap;
    std::unordered_map<T, CacheIter> valueMap;

    /**
     * Delete N caches from back to front
     * 1. Delete only the cache with reference count 0
     * 2. If the deletion is successful, 0 is returned; otherwise,
     * the returned number represents the quantity that has not been deleted
     */
    size_t deleteCache(size_t num)
    {
        // todo: The current data structure will increase the complexity of each
        //  new cache from O(1) to O(N) after the cache is full.
        if (num <= 0)
            return 0;
        auto vg = brls::Application::getNVGContext();
        for (auto i = cacheList.rbegin(); i != cacheList.rend(); i++)
        {
            if (i->count <= 0)
            {
                num--;
                nvgDeleteImage(vg, i->value);
                cacheMap.erase(i->key);
                valueMap.erase(i->value);
                cacheList.erase(std::next(i).base());
                if (num == 0)
                    break;
            }
        }
        return num;
    }

    bool isExisted(K key) { return cacheMap.find(key) != cacheMap.end(); }

    bool isExisted(T val) { return valueMap.find(val) != valueMap.end(); }

    bool isCacheHit(K key)
    {
        if (isExisted(key))
            return !cacheMap[key]->dirty;
        return false;
    }
};
#endif

class TextureCache : public Singleton<TextureCache>
{
  public:
    TextureCache()
    {
        brls::Application::getWindowSizeChangedEvent()->subscribe(
            [this]()
            { this->cache.markAllDirty(); });

        brls::Application::getExitEvent()->subscribe([this]()
            { this->clean(); });
    }

    int getCache(const std::string& key) { return cache.get(key); }

#ifdef PS5_NATIVE_GPU
    // Native replacements acquire/register, commit to the view, then release
    // the old handle. Failed registration leaves ownership with the creator.
    bool tryAddCache(const std::string& key, size_t texture)
    {
        if (key.empty() || texture == 0 || texture > static_cast<size_t>(std::numeric_limits<int>::max()))
            return false;
        return cache.trySet(key, texture);
    }

    bool isClosing() const { return cache.isClosing(); }
#else
    /**
     * Add cache
     */
    void addCache(const std::string& key, size_t texture)
    {
        if (texture <= 0)
            return;
        cache.set(key, texture);
    }
#endif

    /**
     * Release one reference to the matching texture.
     * Unreferenced entries may be retained until the cache needs space.
     */
    void removeCache(size_t texture)
    {
        if (texture <= 0)
            return;
        cache.remove(texture);
    }

    void markDirty(size_t texture)
    {
        if (texture <= 0)
            return;
        cache.markDirty(texture);
    }

#ifndef PS5_NATIVE_GPU
    /**
     * update texture id
     */
    void updateCache(size_t old_tex, size_t new_tex)
    {
        if (old_tex <= 0 || new_tex <= 0)
            return;
        cache.update(old_tex, new_tex);
    }
#endif

    void clean()
    {
#ifdef PS5_NATIVE_GPU
        cache.close();
#else
        auto vg = brls::Application::getNVGContext();
        for (auto& i : cache.getCacheList())
        {
            nvgDeleteImage(vg, i.value);
        }
#endif
    }

    void debug() { cache.debug(); }

#ifdef PS5_NATIVE_GPU
    LRUCache<std::string, size_t> cache { 32, 0 };
#else
    LRUCache<std::string, size_t> cache = LRUCache<std::string, size_t>(200, 0);
#endif
};

}
