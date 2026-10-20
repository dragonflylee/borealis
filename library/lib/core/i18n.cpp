/*
    Copyright 2020-2021 natinusala

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

#include <borealis/core/application.hpp>
#include <borealis/core/assets.hpp>
#include <borealis/core/i18n.hpp>
#ifdef USE_BOOST_FILESYSTEM
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#elif __has_include(<filesystem>)
#include <filesystem>
namespace fs = std::filesystem;
#elif __has_include("experimental/filesystem")
#include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#error "Failed to include <filesystem> header!"
#endif
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

#ifdef PS5_NATIVE_GPU
#include <borealis/platforms/ps5/native_i18n.hpp>
#ifndef USE_LIBROMFS
#include <ps5_native_i18n_catalog.hpp>
#endif
#define BRLS_NATIVE_I18N_STAGE(value) ps5_native_i18n::checkpoint(ps5_native_i18n::Operation::value);
#else
#define BRLS_NATIVE_I18N_STAGE(value)
#endif

#ifndef BRLS_I18N_PREFIX
#define BRLS_I18N_PREFIX ""
#endif

namespace brls
{

static nlohmann::json defaultLocale = {};
static nlohmann::json currentLocale = {};

static void loadLocale(std::string locale, nlohmann::json* target)
{
    if (locale.empty())
        return;
#ifdef USE_LIBROMFS
    BRLS_NATIVE_I18N_STAGE(ResourceList)
    auto localePath = romfs::list("i18n/" + locale);
    if (localePath.empty())
    {
        BRLS_NATIVE_I18N_STAGE(MissingLocaleLog)
        Logger::error("Cannot load locale {}: directory i18n/{} doesn't exist", locale, locale);
        return;
    }
    for (auto& entry : localePath)
    {
        BRLS_NATIVE_I18N_STAGE(ResourceEntry)
        std::string path = entry.string();
        std::string name = entry.filename().string();
        if (!endsWith(name, ".json"))
            continue;

        BRLS_NATIVE_I18N_STAGE(ResourceParse)
        (*target)[name.substr(0, name.length() - 5)] = nlohmann::json::parse(romfs::get(path).string());
    }
#else
#ifdef PS5_NATIVE_GPU
    // The package recipe records every translation filename at build time.
    // Read only the selected locale's named assets; directory enumeration
    // failed in the observed native startup even though /app0 is mounted.
    BRLS_NATIVE_I18N_STAGE(ResourceList)
    bool found = false;
    for (const auto& entry : ps5_native_i18n::catalog)
    {
        if (locale != entry.locale)
            continue;
        found = true;
        BRLS_NATIVE_I18N_STAGE(ResourceEntry)
        std::string name = entry.filename;
        BRLS_NATIVE_I18N_STAGE(EntryPath)
        std::string path = BRLS_ASSET("i18n/" + locale + "/" + name);
#else
    BRLS_NATIVE_I18N_STAGE(LocalePath)
    std::string localePath = BRLS_ASSET("i18n/" + locale);

    BRLS_NATIVE_I18N_STAGE(DirectoryExists)
    if (!fs::exists(localePath))
    {
        BRLS_NATIVE_I18N_STAGE(MissingLocaleLog)
        Logger::error("Cannot load locale {}: directory {} doesn't exist", locale, localePath);
        return;
    }
    else if (!fs::is_directory(localePath))
    {
        BRLS_NATIVE_I18N_STAGE(NotDirectoryLog)
        Logger::error("Cannot load locale {}: {} isn't a directory", locale, localePath);
        return;
    }

    // Iterate over all JSON files in the directory
    for (const fs::directory_entry& entry : fs::directory_iterator(localePath))
    {
        BRLS_NATIVE_I18N_STAGE(EntryType)
        if (fs::is_directory(entry))
            continue;

        BRLS_NATIVE_I18N_STAGE(EntryName)
        std::string name = entry.path().filename().string();

        if (!endsWith(name, ".json"))
            continue;

        BRLS_NATIVE_I18N_STAGE(EntryPath)
        std::string path = entry.path().string();

#endif
        nlohmann::json strings;

        BRLS_NATIVE_I18N_STAGE(FileStream)
        std::ifstream jsonStream;
        BRLS_NATIVE_I18N_STAGE(FileOpen)
        jsonStream.open(path);

        try
        {
            BRLS_NATIVE_I18N_STAGE(JsonParse)
            jsonStream >> strings;
        }
        catch (const std::exception& e)
        {
            BRLS_NATIVE_I18N_STAGE(ParseErrorLog)
            Logger::error("Error while loading \"{}\": {}", path, e.what());
        }

        BRLS_NATIVE_I18N_STAGE(FileClose)
        jsonStream.close();

        BRLS_NATIVE_I18N_STAGE(Publish)
        (*target)[name.substr(0, name.length() - 5)] = strings;
    }
#ifdef PS5_NATIVE_GPU
    if (!found)
    {
        BRLS_NATIVE_I18N_STAGE(MissingLocaleLog)
        Logger::error("Cannot load locale {}: no packaged translation entries", locale);
    }
#endif
#endif /* USE_LIBROMFS */
}

void loadTranslations()
{
    BRLS_NATIVE_I18N_STAGE(DefaultLocale)
    loadLocale(LOCALE_DEFAULT, &defaultLocale);

    BRLS_NATIVE_I18N_STAGE(CurrentLocale)
    std::string currentLocaleName = Application::getLocale();
    if (currentLocaleName != LOCALE_DEFAULT)
#ifdef PS5_NATIVE_GPU
    {
        BRLS_NATIVE_I18N_STAGE(SelectedLocale)
        loadLocale(currentLocaleName, &currentLocale);
    }
#else
        loadLocale(currentLocaleName, &currentLocale);
#endif
    BRLS_NATIVE_I18N_STAGE(Complete)
}

namespace internal
{
    std::string getRawStr(std::string stringName)
    {
        nlohmann::json::json_pointer pointer;

        try
        {
            pointer = nlohmann::json::json_pointer("/" + std::string(BRLS_I18N_PREFIX) + stringName);
        }
        catch (const std::exception& e)
        {
            Logger::error("Error while getting string \"{}\": {}", stringName, e.what());
            return stringName;
        }

        // First look for translated string in current locale
        try
        {
            return currentLocale[pointer].get<std::string>();
        }
        catch (...)
        {
        }

        // Then look for default locale
        try
        {
            return defaultLocale[pointer].get<std::string>();
        }
        catch (...)
        {
        }

        // Fallback to returning the string name
        return stringName;
    }
} // namespace internal

inline namespace literals
{
    std::string operator""_i18n(const char* str, size_t len)
    {
        return internal::getRawStr(std::string(str, len));
    }

} // namespace literals

} // namespace brls

#undef BRLS_NATIVE_I18N_STAGE
