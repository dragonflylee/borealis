/*
    Copyright 2019 WerWolv
    Copyright 2019 p-sam
    Copyright 2023 xfangfang

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

#include <functional>
#include <string>

namespace brls
{

enum KeyboardKeyDisableBitmask
{
    KEYBOARD_DISABLE_NONE         = 0,
    KEYBOARD_DISABLE_SPACE        = 1,
    KEYBOARD_DISABLE_AT           = 1 << 1,
    KEYBOARD_DISABLE_PERCENT      = 1 << 2,
    KEYBOARD_DISABLE_FORWSLASH    = 1 << 3,
    KEYBOARD_DISABLE_BACKSLASH    = 1 << 4,
    KEYBOARD_DISABLE_NUMBERS      = 1 << 5,
    KEYBOARD_DISABLE_DOWNLOADCODE = 1 << 6,
    KEYBOARD_DISABLE_USERNAME     = 1 << 7,
};

class ImeManager
{
  public:
    virtual ~ImeManager() { }

    virtual bool openForText(std::function<void(std::string)> f, std::string headerText = "",
        std::string subText = "", int maxStringLength = 32, std::string initialText = "",
        int kbdDisableBitmask = KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_NONE)
        = 0;

    virtual bool openForPassword(std::function<void(std::string)> f, std::string headerText,
        std::string subText = "", int maxStringLength = 32, std::string initialText = "")
        = 0;

    virtual bool openForNumber(std::function<void(long)> f, std::string headerText = "",
        std::string subText = "", int maxStringLength = 18, std::string initialText = "",
        std::string leftButton = "", std::string rightButton = "",
        int kbdDisableBitmask = KeyboardKeyDisableBitmask::KEYBOARD_DISABLE_NONE)
        = 0;

    /**
     * Desktop (GLFW/SDL) only: text can be edited in place without a dialog.
     */
    virtual bool supportsInlineEditing()
    {
        return false;
    }

    /**
     * onText receives committed UTF-8 fragments, onCompose the live preview (empty when
     * composition ends). Committing or cancelling is the caller's job. disableIme keeps
     * composition away from the IME, for password-like input.
     */
    virtual void startInlineEditing(std::function<void(const std::string&)> onText,
        std::function<void(const std::string&)> onCompose = nullptr, bool disableIme = false)
    {
    }

    /**
     * Caret area in scaled window coordinates, used to place the IME candidate window.
     */
    virtual void setInlineEditingRect(float x, float y, float width, float height)
    {
    }

    /**
     * End the session started by startInlineEditing().
     */
    virtual void stopInlineEditing()
    {
    }
};

};