#pragma once

#ifdef PS5_NATIVE_GPU
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace brls::ps5_native_ime
{
// App main-thread observations only. These values describe the wrapper, not
// proof that the native provider opened or which physical key completed it.
enum class Stage { BeforeStart, AfterStart, AfterShown, WrapperSubmit, WrapperCancel, AfterStop };
enum class ErrorKind
{
    None, Unknown, UserServiceEntry, UserServiceInitialize, ImeProviderEntry,
    CommonProviderEntry, CommonPreviousStart, CommonContextUnavailable,
    CommonContextInvalid, CommonContextSize, CommonLoad, CommonStart,
    CommonExport, CommonInitialize, ImeModuleLoad, ImeEntries, AlreadyOwned,
    ForegroundUser, InitialConversion, ImeInitialize, StatusEntry,
    CleanupEntry, AbortEntry, Abort, Term, ResultEntry, ResultConversion
};

struct Error
{
    ErrorKind kind = ErrorKind::None;
    bool has_code = false;
    std::uint32_t code = 0;
    bool has_second_code = false;
    std::uint32_t second_code = 0;
    bool has_module_handle = false;
    std::uint32_t module_handle = 0;
};

// No field can retain arbitrary SDL messages, input, passwords, titles, paths
// or lengths. The sink may serialize only these fixed observations.
struct Event
{
    std::uint64_t request_id = 0;
    Stage stage = Stage::BeforeStart;
    Error error;
    bool focus = false;
    bool screen_keyboard_enabled = false;
    bool initial_hint_set = false;
    bool password_hint_set = false;
    bool limit_hint_set = false;
    bool text_events_active = false;
    bool shown_queried = false;
    bool shown = false;
};

using Callback = void (*)(const Event&) noexcept;

namespace detail
{
inline Callback callback = nullptr;
inline std::uint64_t request_id = 0;

inline bool hex_code(const char* value, std::uint32_t& code) noexcept
{
    if (value[0] != '0' || value[1] != 'x') return false;
    std::uint32_t parsed = 0;
    for (unsigned i = 2; i != 10; ++i)
    {
        const char c = value[i];
        unsigned digit;
        if (c >= '0' && c <= '9') digit = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') digit = static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') digit = static_cast<unsigned>(c - 'A' + 10);
        else return false;
        parsed = (parsed << 4) | digit;
    }
    code = parsed;
    return true;
}
} // namespace detail

// Exact selected-backend messages only. Reject unknown suffixes, partial or
// oversized codes, and arbitrary content even after a recognizable prefix.
inline Error classify_error(const char* message) noexcept
{
    if (!message || !*message) return {};
    std::size_t length = 0;
    while (length != 160 && message[length]) ++length;
    if (length == 160) return {ErrorKind::Unknown};
    struct Fixed { const char* message; ErrorKind kind; };
    static constexpr Fixed fixed[] = {
        {"UserService initialize entry unavailable", ErrorKind::UserServiceEntry},
        {"IME provider entry unavailable", ErrorKind::ImeProviderEntry},
        {"CommonDialog provider entry unavailable", ErrorKind::CommonProviderEntry},
        {"CommonDialog module startup previously failed", ErrorKind::CommonPreviousStart},
        {"CommonDialog sandbox word unavailable", ErrorKind::CommonContextUnavailable},
        {"CommonDialog sandbox word invalid", ErrorKind::CommonContextInvalid},
        {"CommonDialog sandbox word size invalid", ErrorKind::CommonContextSize},
        {"On-screen keyboard entries unavailable", ErrorKind::ImeEntries},
        {"ImeDialog already owned", ErrorKind::AlreadyOwned},
        {"ImeDialog initial UTF-8 conversion failed", ErrorKind::InitialConversion},
        {"IME status entry unavailable; ownership retained", ErrorKind::StatusEntry},
        {"IME cleanup entry unavailable; ownership retained", ErrorKind::CleanupEntry},
        {"IME abort entry unavailable; ownership retained", ErrorKind::AbortEntry},
        {"ImeDialog abort failed; ownership retained", ErrorKind::Abort},
        {"ImeDialog term failed; ownership retained", ErrorKind::Term},
        {"IME result/cleanup entry unavailable; ownership retained", ErrorKind::ResultEntry},
    };
    for (const auto& entry : fixed)
        if (std::strcmp(message, entry.message) == 0) return {entry.kind};
    static constexpr Fixed coded[] = {
        {"sceUserServiceInitialize: ", ErrorKind::UserServiceInitialize},
        {"CommonDialog load failed: ", ErrorKind::CommonLoad},
        {"CommonDialog start failed: ", ErrorKind::CommonStart},
        {"CommonDialog initialize failed: ", ErrorKind::CommonInitialize},
        {"ImeDialog module load failed: ", ErrorKind::ImeModuleLoad},
        {"ImeDialog foreground user unavailable: ", ErrorKind::ForegroundUser},
        {"sceImeDialogInit failed: ", ErrorKind::ImeInitialize},
        {"ImeDialog result/conversion failed: ", ErrorKind::ResultConversion},
    };
    for (const auto& entry : coded)
    {
        const auto prefix = std::strlen(entry.message);
        std::uint32_t code = 0;
        if (length == prefix + 10 && std::strncmp(message, entry.message, prefix) == 0 &&
            detail::hex_code(message + prefix, code))
            return {entry.kind, true, code};
    }
    constexpr char prefix[] = "CommonDialog export unavailable: method=none name=";
    constexpr char middle[] = " identifier=";
    constexpr auto offset = sizeof(prefix) - 1;
    constexpr auto second = offset + 10 + sizeof(middle) - 1;
    constexpr char module_prefix[] = " module=";
    constexpr auto module_offset = second + 10 + sizeof(module_prefix) - 1;
    std::uint32_t code = 0, second_code = 0, module_handle = 0;
    if ((length == second + 10 || length == module_offset + 10) &&
        std::strncmp(message, prefix, offset) == 0 &&
        std::strncmp(message + offset + 10, middle, sizeof(middle) - 1) == 0 &&
        detail::hex_code(message + offset, code) && detail::hex_code(message + second, second_code))
    {
        if (length == second + 10)
            return {ErrorKind::CommonExport, true, code, true, second_code};
        if (std::strncmp(message + second + 10, module_prefix, sizeof(module_prefix) - 1) == 0 &&
            detail::hex_code(message + module_offset, module_handle))
            return {ErrorKind::CommonExport, true, code, true, second_code, true, module_handle};
    }
    return {ErrorKind::Unknown};
}

inline const char* stage_name(Stage stage) noexcept
{
    switch (stage)
    {
        case Stage::BeforeStart: return "before-start";
        case Stage::AfterStart: return "after-start";
        case Stage::AfterShown: return "after-shown-query";
        case Stage::WrapperSubmit: return "wrapper-submit";
        case Stage::WrapperCancel: return "wrapper-cancel";
        case Stage::AfterStop: return "after-stop";
    }
    return "unknown-stage";
}

inline const char* error_name(ErrorKind kind) noexcept
{
    switch (kind)
    {
        case ErrorKind::None: return "none";
        case ErrorKind::Unknown: return "unknown-withheld";
        case ErrorKind::UserServiceEntry: return "user-service-entry";
        case ErrorKind::UserServiceInitialize: return "user-service-initialize";
        case ErrorKind::ImeProviderEntry: return "ime-provider-entry";
        case ErrorKind::CommonProviderEntry: return "common-provider-entry";
        case ErrorKind::CommonPreviousStart: return "common-previous-start";
        case ErrorKind::CommonContextUnavailable: return "common-context-unavailable";
        case ErrorKind::CommonContextInvalid: return "common-context-invalid";
        case ErrorKind::CommonContextSize: return "common-context-size";
        case ErrorKind::CommonLoad: return "common-load";
        case ErrorKind::CommonStart: return "common-start";
        case ErrorKind::CommonExport: return "common-export";
        case ErrorKind::CommonInitialize: return "common-initialize";
        case ErrorKind::ImeModuleLoad: return "ime-module-load";
        case ErrorKind::ImeEntries: return "ime-entries";
        case ErrorKind::AlreadyOwned: return "already-owned";
        case ErrorKind::ForegroundUser: return "foreground-user";
        case ErrorKind::InitialConversion: return "initial-conversion";
        case ErrorKind::ImeInitialize: return "ime-initialize";
        case ErrorKind::StatusEntry: return "status-entry";
        case ErrorKind::CleanupEntry: return "cleanup-entry";
        case ErrorKind::AbortEntry: return "abort-entry";
        case ErrorKind::Abort: return "abort";
        case ErrorKind::Term: return "term";
        case ErrorKind::ResultEntry: return "result-entry";
        case ErrorKind::ResultConversion: return "result-conversion";
    }
    return "unknown-withheld";
}

// Register before UI processing. All calls remain on the app main thread.
// The sink must not call SDL, dispatch input, throw, or retain the Event address.
inline void set_callback(Callback callback) noexcept { detail::callback = callback; }
inline std::uint64_t next_request_id() noexcept
{
    if (detail::request_id != std::numeric_limits<std::uint64_t>::max()) ++detail::request_id;
    return detail::request_id;
}
inline void emit(const Event& event) noexcept
{
    const int saved_errno = errno;
    if (const auto callback = detail::callback) callback(event);
    errno = saved_errno;
}
} // namespace brls::ps5_native_ime
#endif
