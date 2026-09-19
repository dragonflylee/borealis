/*
    Copyright 2019-2021 natinusala
    Copyright 2019 p-sam

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

#ifdef PS5_NATIVE_GPU
#include <string>
#endif

#ifndef BRLS_RESOURCES
#error BRLS_RESOURCES define missing
#endif

#ifdef PS5_NATIVE_GPU
namespace brls {
// The base directory assets are resolved against. It defaults to the
// compile-time BRLS_RESOURCES, so behaviour is unchanged until something calls
// setResourceBase(). A sandbox-escaping build sets it to the real path its
// assets live at once the process leaves the jail (e.g. the /mnt/sandbox
// location) -- the compile-time literal /app0 no longer resolves there.
const std::string& resourceBase();
void setResourceBase(std::string base);
std::string resourcePath(const std::string& relative);
}  // namespace brls
#endif

#ifdef USE_LIBROMFS
#define BRLS_ASSET(_str) "@res/" _str
#elif defined(PS5_NATIVE_GPU)
#define BRLS_ASSET(_str) (::brls::resourcePath(_str))
#else
#define BRLS_ASSET(_str) BRLS_RESOURCES _str
#endif
