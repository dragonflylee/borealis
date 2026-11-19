/*
Copyright 2026 Switchfin contributors

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

#include <SDL2/SDL.h>

#include <cmath>
#include <cstdlib>
#include <utility>

#include <glad/glad.h>

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/platforms/ps5/ps5_platform.hpp>
#include <borealis/platforms/ps5/native_display.hpp>

// The native SDK supplies import stubs; declare the system service ABI here.
extern "C"
{
    int sceSystemServiceParamGetInt(int paramId, int* value);
    int sceSystemServiceHideSplashScreen(void);
}

// System software parameter ids and values.  Shared with the PS4 ABI.
#define SCE_SYSTEM_SERVICE_PARAM_ID_LANG 1
#define SCE_SYSTEM_SERVICE_PARAM_ID_ENTER_BUTTON_ASSIGN 1000

#define SCE_SYSTEM_PARAM_LANG_JAPANESE 0
#define SCE_SYSTEM_PARAM_LANG_ITALIAN 5
#define SCE_SYSTEM_PARAM_LANG_KOREAN 9
#define SCE_SYSTEM_PARAM_LANG_CHINESE_T 10
#define SCE_SYSTEM_PARAM_LANG_CHINESE_S 11

#define SCE_SYSTEM_PARAM_ENTER_BUTTON_ASSIGN_CIRCLE 0

namespace brls
{

Ps5Platform::Ps5Platform()
{
    // The application owns the visible window in both installation modes.
    sceSystemServiceHideSplashScreen();

    int value = 0;

    // SDL_GetPreferredLocales() has no PS5 implementation, so SDLPlatform's
    // locale detection always falls through. Read the system language instead.
    if (Platform::APP_LOCALE_DEFAULT == LOCALE_AUTO || Platform::APP_LOCALE_DEFAULT.empty())
    {
        if (sceSystemServiceParamGetInt(SCE_SYSTEM_SERVICE_PARAM_ID_LANG, &value) == 0)
        {
            switch (value)
            {
                case SCE_SYSTEM_PARAM_LANG_CHINESE_S:
                    this->locale = LOCALE_ZH_HANS;
                    break;
                case SCE_SYSTEM_PARAM_LANG_CHINESE_T:
                    this->locale = LOCALE_ZH_HANT;
                    break;
                case SCE_SYSTEM_PARAM_LANG_JAPANESE:
                    this->locale = LOCALE_JA;
                    break;
                case SCE_SYSTEM_PARAM_LANG_KOREAN:
                    this->locale = LOCALE_Ko;
                    break;
                case SCE_SYSTEM_PARAM_LANG_ITALIAN:
                    this->locale = LOCALE_IT;
                    break;
                default:
                    this->locale = LOCALE_DEFAULT;
            }
        }
        brls::Logger::info("App locale: {}", this->locale);
    }

    // Respect the system's ×/○ enter-button assignment.
    value = 0;
    if (sceSystemServiceParamGetInt(SCE_SYSTEM_SERVICE_PARAM_ID_ENTER_BUTTON_ASSIGN, &value) == 0 &&
        value == SCE_SYSTEM_PARAM_ENTER_BUTTON_ASSIGN_CIRCLE)
    {
        brls::Application::setSwapInputKeys(true);
    }


}

Ps5Platform::~Ps5Platform() = default;

std::string Ps5Platform::getName()
{
    return "PS5 Native";
}

void Ps5Platform::createWindow(std::string windowTitle, uint32_t windowWidth, uint32_t windowHeight, float windowXPos, float windowYPos)
{
    VideoContext::FULLSCREEN = false;
    Logger::info("ps5 native: configured {}x{} nominal {}Hz EGL Core3.3; runtime resize disabled",
        ps5_native_display::width, ps5_native_display::height, ps5_native_display::refreshHz);
    SDLPlatform::createWindow(std::move(windowTitle), ps5_native_display::width,
        ps5_native_display::height, NAN, NAN);
}

bool Ps5Platform::canShowWirelessLevel()
{
    return false;
}

bool Ps5Platform::hasWirelessConnection()
{
    return false;
}

bool Ps5Platform::hasEthernetConnection()
{
    return this->getIpAddress() != "-";
}

} // namespace brls
