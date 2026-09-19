#pragma once

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <sys/stat.h>

namespace brls {
// Called under Logger's existing mutex. The limit applies to the file sink;
// the on-screen log event still receives messages after the file is full.
class Ps5LogOutput {
public:
    static constexpr char limitNotice[] = "[PS5] Application log limit reached; capture logs before another session.\n";

    // Before workers start: append within the budget, or retain the exhausted
    // segment as .previous and start a new one. A failed rotation disables the
    // sink instead of overwriting evidence. A terminal notice also counts as
    // exhausted even when the last line did not fit the remaining budget.
    static size_t remainingForFile(const char* path, size_t limit) {
        struct stat info{};
        if (stat(path, &info) != 0) return limit;
        if (info.st_size < 0) return 0;
        bool exhausted = static_cast<uint64_t>(info.st_size) >= limit;
        if (!exhausted && info.st_size >= static_cast<off_t>(sizeof(limitNotice) - 1)) {
            if (FILE* input = std::fopen(path, "rb")) {
                char tail[sizeof(limitNotice) - 1];
                if (std::fseek(input, -static_cast<long>(sizeof(tail)), SEEK_END) == 0
                        && std::fread(tail, 1, sizeof(tail), input) == sizeof(tail))
                    exhausted = std::string(tail, sizeof(tail)) == limitNotice;
                std::fclose(input);
            }
        }
        // A tiny pre-existing tail budget cannot even record the notice.
        if (!exhausted && limit - static_cast<size_t>(info.st_size) > sizeof(limitNotice) - 1)
            return limit - static_cast<size_t>(info.st_size);
        const std::string previous = std::string(path) + ".previous";
        return std::rename(path, previous.c_str()) == 0 ? limit : 0;
    }

    void limit(size_t remaining) { budget = remaining; }

    void write(FILE* output, const std::string& line) {
        if (!budget) return;
        if (line.size() <= budget && sizeof(limitNotice) - 1 <= budget - line.size()) {
            const auto written = std::fwrite(line.data(), 1, line.size(), output);
            budget -= written;
            if (written != line.size()) budget = 0;
            return;
        }
        if (budget >= sizeof(limitNotice) - 1)
            std::fwrite(limitNotice, 1, sizeof(limitNotice) - 1, output);
        budget = 0;
    }

private:
    size_t budget = std::numeric_limits<size_t>::max();
};
} // namespace brls
