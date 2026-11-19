/*
    Copyright 2021 natinusala

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

#include <borealis/core/util.hpp>
#include <libretro-common/encodings/utf.h>
#include <fstream>

#ifdef __PSV__
#include <psp2/kernel/processmgr.h>
#endif

namespace brls
{

bool endsWith(const std::string& data, const std::string& suffix)
{
    return data.find(suffix, data.size() - suffix.size()) != std::string::npos;
}

bool startsWith(const std::string& data, const std::string& prefix)
{
    return data.rfind(prefix, 0) == 0;
}

size_t utf8ByteOffset(const std::string& text, int codepoints)
{
    if (codepoints <= 0)
        return 0;
    // utf8skip() doesn't stop at '\0', so clamp before walking.
    if (codepoints > (int)utf8len(text.c_str()))
        return text.size();
    return (size_t)(utf8skip(text.c_str(), (size_t)codepoints) - text.c_str());
}

std::string maskCodepoints(const std::string& text)
{
    std::string masked;
    for (size_t i = 0; i < utf8len(text.c_str()); i++)
        masked += "●";
    return masked;
}

[[noreturn]] void fatal(std::string message)
{
    brls::Logger::error("Fatal error: {}", message);
#ifdef __PSV__
    sceKernelExitProcess(1);
#else
    throw std::logic_error(message);
#endif
}

std::string loadFileContents(const std::string &path) {
    std::ifstream fin;
    fin.open(path, std::ios::in);
    if (!fin.is_open()) {
      brls::Logger::error("cannot open file: {}", path);
      return "";
    }

    std::string res, buff;
    while (getline(fin, buff)) {
        res += buff + "\n";
    }
    fin.close();

    return res;
}

} // namespace brls
