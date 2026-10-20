#pragma once

#ifdef PS5_NATIVE_GPU
namespace brls::ps5_native_i18n
{
// Translation initialization runs on the main thread before UI workers start.
// Record only fixed operations; never retain paths, locale names or exceptions.
enum class Operation
{
    NotStarted, DefaultLocale, LocalePath, DirectoryExists, DirectoryType,
    MissingLocaleLog, NotDirectoryLog, DirectoryOpen, DirectoryEntry,
    EntryType, EntryName, EntryPath, FileStream, FileOpen, JsonParse,
    ParseErrorLog, FileClose, Publish, DirectoryIncrement, CurrentLocale,
    SelectedLocale, ResourceList, ResourceEntry, ResourceParse, Complete
};

namespace detail
{
inline Operation operation = Operation::NotStarted;
}

inline void checkpoint(Operation operation) noexcept { detail::operation = operation; }

inline const char* current_operation() noexcept
{
    switch (detail::operation)
    {
        case Operation::NotStarted: return "i18n-not-started";
        case Operation::DefaultLocale: return "i18n-default-locale";
        case Operation::LocalePath: return "i18n-locale-path";
        case Operation::DirectoryExists: return "i18n-directory-exists";
        case Operation::DirectoryType: return "i18n-directory-type";
        case Operation::MissingLocaleLog: return "i18n-missing-locale-log";
        case Operation::NotDirectoryLog: return "i18n-not-directory-log";
        case Operation::DirectoryOpen: return "i18n-directory-open";
        case Operation::DirectoryEntry: return "i18n-directory-entry";
        case Operation::EntryType: return "i18n-entry-type";
        case Operation::EntryName: return "i18n-entry-name";
        case Operation::EntryPath: return "i18n-entry-path";
        case Operation::FileStream: return "i18n-file-stream";
        case Operation::FileOpen: return "i18n-file-open";
        case Operation::JsonParse: return "i18n-json-parse";
        case Operation::ParseErrorLog: return "i18n-parse-error-log";
        case Operation::FileClose: return "i18n-file-close";
        case Operation::Publish: return "i18n-publish";
        case Operation::DirectoryIncrement: return "i18n-next-entry";
        case Operation::CurrentLocale: return "i18n-current-locale";
        case Operation::SelectedLocale: return "i18n-selected-locale";
        case Operation::ResourceList: return "i18n-resource-list";
        case Operation::ResourceEntry: return "i18n-resource-entry";
        case Operation::ResourceParse: return "i18n-resource-parse";
        case Operation::Complete: return "i18n-complete";
    }
    return "i18n-unknown-operation";
}
} // namespace brls::ps5_native_i18n
#endif
