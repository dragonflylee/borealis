#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

namespace brls {

// UI-owned, constant-size admission recovery. No request bytes, view pins,
// allocated callback, timer, or registry entry are retained here.
class ArtworkRetry {
public:
    using Clock = std::chrono::steady_clock;
    enum class Result { Idle, Accepted, Temporary, Permanent };
    static constexpr unsigned maxAttempts = 3;

    uint64_t begin(const std::shared_ptr<std::atomic_bool>& account) noexcept {
        if (!dispatching || generation != dispatchGeneration) {
            ++generation;
            attempts = 0;
            cancellation = account;
        }
        result = Result::Permanent; // Exceptions fail closed.
        return generation;
    }

    void complete(uint64_t token, Result value, Clock::time_point now = Clock::now()) noexcept {
        if (token != generation) return;
        result = value;
        if (value == Result::Temporary && attempts < maxAttempts) {
            constexpr unsigned delays[] = {250, 1000, 4000};
            deadline = now + std::chrono::milliseconds(delays[attempts]);
        } else {
            if (value == Result::Temporary) result = Result::Permanent;
            cancellation.reset();
        }
    }

    void cancel() noexcept {
        ++generation;
        result = Result::Idle;
        attempts = 0;
        cancellation.reset();
    }

    template<class Retry>
    void poll(Retry&& retry, Clock::time_point now = Clock::now()) noexcept {
        if (dispatching || result != Result::Temporary || now < deadline) return;
        if (cancellation && cancellation->load()) { cancel(); return; }
        const auto token = generation;
        ++attempts;
        result = Result::Permanent; // Missing model/identity is terminal.
        dispatching = true;
        dispatchGeneration = token;
        try { retry(); }
        catch (...) { if (generation == token) result = Result::Permanent; }
        dispatching = false;
        if (result != Result::Temporary) cancellation.reset();
    }

    Result status() const noexcept { return result; }
    unsigned retryAttempts() const noexcept { return attempts; }
    uint64_t token() const noexcept { return generation; }

private:
    std::shared_ptr<std::atomic_bool> cancellation;
    Clock::time_point deadline{};
    uint64_t generation = 0, dispatchGeneration = 0;
    unsigned attempts = 0;
    Result result = Result::Idle;
    bool dispatching = false;
};

} // namespace brls
