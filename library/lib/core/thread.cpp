/*
    Copyright 2021 XITRIX

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

#include <libretro-common/retro_timers.h>

#include <borealis/core/logger.hpp>
#include <borealis/core/thread.hpp>
#include <exception>
#ifdef PS5_NATIVE_GPU
#include <atomic>
#include <list>
#endif

#ifdef BOREALIS_USE_STD_THREAD
#include <thread>
#else
#include <pthread.h>
#endif

#ifdef PS4
#include <orbis/libkernel.h>
#endif

namespace brls
{

#ifdef PS5_NATIVE_GPU
// File-local native storage leaves the shared Threading declaration intact.
// Pending timers retain their nodes while moving between frame batches.
static std::list<DelayOperation> nativeDelayTasks;
static std::atomic<bool> nativeTaskLoopActive{true};

// Callback failures must not terminate the worker or strand later UI work.
// Keep diagnostics bounded and independent of arbitrary exception text. The
// logger itself can allocate, so failure reporting must also be contained.
static void reportDeferredFailure(const char* kind) noexcept
{
    static std::atomic<unsigned> failures{0};
    unsigned observed = failures.load(std::memory_order_relaxed);
    do
    {
        if (observed >= 16) return;
    } while (!failures.compare_exchange_weak(observed, observed + 1, std::memory_order_relaxed));
    try { Logger::error("ps5 native: {} callback failed", kind); }
    catch (...) {}
}
#endif

#ifdef BOREALIS_USE_STD_THREAD
static std::thread *task_loop_thread = nullptr;
#else
static pthread_t task_loop_thread = pthread_t(0);
#endif

Threading::Threading()
{
    start_task_loop();
}

void sync(const std::function<void()>& func)
{
    Threading::sync(func);
}

void async(const std::function<void()>& task)
{
    Threading::async(task);
}

size_t delay(long milliseconds, const std::function<void()>& func)
{
    return Threading::delay(milliseconds, func);
}

void cancelDelay(size_t iter)
{
    Threading::cancelDelay(iter);
}

void Threading::sync(const std::function<void()>& func)
{
    std::lock_guard<std::mutex> guard(m_sync_mutex);
    m_sync_functions.push_back(func);
}

void Threading::async(const std::function<void()>& task)
{
    std::lock_guard<std::mutex> guard(m_async_mutex);
    m_async_tasks.push_back(task);
}

size_t Threading::delay(long milliseconds, const std::function<void()>& func)
{
    std::lock_guard<std::mutex> guard(m_delay_mutex);
    DelayOperation operation;
#ifdef PS4
    operation.startPoint        = sceKernelGetProcessTime();
    operation.delayMilliseconds = milliseconds * 1000;
#else
    operation.startPoint        = std::chrono::high_resolution_clock::now();
    operation.delayMilliseconds = milliseconds;
#endif
    operation.func              = func;
    operation.index             = ++m_delay_index;
#ifdef PS5_NATIVE_GPU
    nativeDelayTasks.push_back(std::move(operation));
#else
    m_delay_tasks.push_back(operation);
#endif
    return m_delay_index;
}

void Threading::cancelDelay(size_t iter)
{
    std::lock_guard<std::mutex> guard(m_delay_mutex);
    m_delay_cancel_set.insert(iter);
}

void Threading::performSyncTasks()
{
#ifdef PS5_NATIVE_GPU
    std::vector<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> guard(m_sync_mutex);
        local.swap(m_sync_functions);
    }
    for (auto& func : local)
    {
        try { func(); }
        catch (...) { reportDeferredFailure("UI"); }
    }

    std::list<DelayOperation> delay_local;
    {
        std::lock_guard<std::mutex> guard(m_delay_mutex);
        delay_local.splice(delay_local.end(), nativeDelayTasks);
    }
    // Capture this frame's batch. Newly scheduled timers wait until the next
    // frame; original future timers are returned without allocating/copying.
    for (auto next = delay_local.begin(); next != delay_local.end();)
    {
        auto current = next++;
        auto& operation = *current;
        {
            std::lock_guard<std::mutex> guard(m_delay_mutex);
            if (m_delay_cancel_set.erase(operation.index)) continue;
        }
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - operation.startPoint).count();
        if (duration >= operation.delayMilliseconds)
        {
            try { operation.func(); }
            catch (...) { reportDeferredFailure("timer"); }
            std::lock_guard<std::mutex> guard(m_delay_mutex);
            m_delay_cancel_set.erase(operation.index);
        }
        else
        {
            std::lock_guard<std::mutex> guard(m_delay_mutex);
            nativeDelayTasks.splice(nativeDelayTasks.end(), delay_local, current);
        }
    }
#else
    m_sync_mutex.lock();
    auto local = m_sync_functions;
    m_sync_functions.clear();
    m_sync_mutex.unlock();

    for (auto& f : local)
    {
        try
        {
            f();
        }
        catch (std::exception& e)
        {
            brls::Logger::error("error: performSyncTasks: {}", e.what());
        }
    }

    m_delay_mutex.lock();
    auto delay_local = m_delay_tasks;
    m_delay_tasks.clear();
    m_delay_mutex.unlock();

    for (auto& d : delay_local)
    {
        // Check cancel
        m_delay_mutex.lock();
        if (m_delay_cancel_set.count(d.index))
        {
            m_delay_cancel_set.erase(d.index);
            m_delay_mutex.unlock();
            continue;
        }
        m_delay_mutex.unlock();

#ifdef PS4
        uint64_t duration = sceKernelGetProcessTime() - d.startPoint;
#else
        auto timeNow  = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(timeNow - d.startPoint).count();
#endif

        if (duration >= d.delayMilliseconds)
        {
            try
            {
                d.func();
            }
            catch (std::exception& e)
            {
                brls::Logger::error("error: performSyncTasks(delay): {}", e.what());
            }

            m_delay_mutex.lock();
            if (m_delay_cancel_set.count(d.index)) m_delay_cancel_set.erase(d.index);
            m_delay_mutex.unlock();

        }
        else
        {
            m_delay_mutex.lock();
            m_delay_tasks.push_back(d);
            m_delay_mutex.unlock();
        }
    }
#endif
}

void Threading::start()
{
#ifdef PS5_NATIVE_GPU
    nativeTaskLoopActive = true;
#else
    task_loop_active = true;
#endif
    start_task_loop();
}

void Threading::stop()
{
#ifdef PS5_NATIVE_GPU
    nativeTaskLoopActive = false;
#else
    task_loop_active = false;
#endif

#ifdef BOREALIS_USE_STD_THREAD
    task_loop_thread->join();
    delete task_loop_thread;
    task_loop_thread = nullptr;
#else
    pthread_join(task_loop_thread, NULL);
#endif
}
void Threading::std_task_loop() {
    task_loop(nullptr);
}
void* Threading::task_loop(void* a)
{
#ifdef PS5_NATIVE_GPU
    while (nativeTaskLoopActive)
#else
    while (task_loop_active)
#endif
    {
        std::vector<std::function<void()>> m_tasks_copy;
        {
            std::lock_guard<std::mutex> guard(m_async_mutex);
#ifdef PS5_NATIVE_GPU
            m_tasks_copy.swap(m_async_tasks);
#else
            m_tasks_copy = m_async_tasks;
            m_async_tasks.clear();
#endif
        }

#ifdef PS5_NATIVE_GPU
        for (auto& task : m_tasks_copy)
        {
            try { task(); }
            catch (...) { reportDeferredFailure("background"); }
        }
#else
        for (auto task : m_tasks_copy)
        {
            task();
        }
#endif

        retro_sleep(500);
    }
    return NULL;
}

void Threading::start_task_loop()
{
#ifdef BOREALIS_USE_STD_THREAD
    task_loop_thread = new std::thread(std_task_loop);
#else
    pthread_create(&task_loop_thread, NULL, task_loop, NULL);
#endif
}

} // namespace brls
