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

#include "activity/main_activity.hpp"

MainActivity::MainActivity()
{
    auto* input = brls::Application::getPlatform()->getInputManager();
    this->enterSubscription = input->getMouseCusorEntered()->subscribe([](bool enterd) {
        brls::Logger::debug("Mouse Cusor {}", enterd ? "enterd" : "leave");
    });
}

MainActivity::~MainActivity()
{
    auto* input = brls::Application::getPlatform()->getInputManager();
    input->getMouseCusorEntered()->unsubscribe(this->enterSubscription);
}