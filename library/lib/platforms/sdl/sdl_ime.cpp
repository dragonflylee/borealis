/*
Copyright 2023 zeromake

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

#include <borealis/platforms/sdl/sdl_ime.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/event.hpp>
#include <borealis/views/edit_text_dialog.hpp>
#include <borealis/core/application.hpp>
#ifdef PS5_NATIVE_GPU
#include <borealis/platforms/ps5/native_ime_diagnostics.hpp>
#endif

#ifdef __PSV__
extern "C" uint8_t * vita_ime_init_text;
__attribute__((weak)) uint8_t* vita_ime_init_text = nullptr;

extern "C" uint32_t vita_ime_max_text;
__attribute__((weak)) uint32_t vita_ime_max_text = 32;

extern "C" uint32_t vita_ime_type;
__attribute__((weak)) uint32_t vita_ime_type = 0;
#endif

namespace brls
{
#ifdef PS5_NATIVE_GPU
    static void nativeImeCompletion(std::uint64_t request, ps5_native_ime::Stage stage)
    {
        ps5_native_ime::Event observation;
        observation.request_id = request;
        observation.stage = stage;
        observation.error = ps5_native_ime::classify_error(SDL_GetError());
        observation.focus = SDL_GetKeyboardFocus() != nullptr;
        observation.text_events_active = SDL_IsTextInputActive() == SDL_TRUE;
        // Do not add an IsShown call here: it invokes the native provider and
        // can overwrite the result of Stop. Wrapper completion is not proof
        // that native cleanup succeeded, nor proof of physical input origin.
        ps5_native_ime::emit(observation);
    }
#endif

    SDLImeManager::SDLImeManager(Event<SDL_Event*> *event):
    event(event),
    cursor(-1){}

    static int utf8_len(std::string &s) {
        int result = 0;
        for (auto &it: s) {
            if ((it & 0xc0) != 0x80) {
                result += 1;
            }
        }
        return result;
    }

    static int utf8_find_prev(std::string &s, int size) {
        int result = 0;
        for (int i = s.size() - 1; i >= 0; i--) {
            char p = s.at(i);
            result += 1;
            if ((p & 0xc0) != 0x80) {
                size--;
            }
            if (size <= 0) {
                break;
            }
        }
        return result;
    }

    static int utf8_find_next(std::string &s, int offset, int size) {
        int result = 0;
        if (size == 0) {
            return 0;
        }
        for (size_t i = offset; i < s.size(); i++) {
            char p = s.at(i);
            if ((p & 0xc0) != 0x80) {
                size--;
            }
            if (size < 0) {
                break;
            }
            result += 1;
        }
        return result;
    }

    void SDLImeManager::openInputDialog(
        std::function<void(std::string)> cb,
        std::string headerText,
        std::string subText,
        size_t maxStringLength,
        std::string initialText,
        bool isPassword) {
#ifdef PS5_NATIVE_GPU
        const auto imeRequest = ps5_native_ime::next_request_id();
#endif
        EditTextDialog* dialog = new EditTextDialog();
        dialog->setPasswordStyle(isPassword);
        this->inputBuffer = initialText;
#ifdef __PSV__
        vita_ime_init_text = reinterpret_cast<uint8_t *>(const_cast<char *>(this->inputBuffer.c_str()));
        vita_ime_max_text = maxStringLength;
#endif
        auto updateText = [this, dialog, maxStringLength]() {
            std::string text = this->inputBuffer;
            if (this->editingBuffer.size() > 0) {
                std::string editing = fmt::format("[{}]", this->editingBuffer);
                if (this->cursor >= 0) {
                    int start = utf8_find_next(this->inputBuffer, 0, this->cursor);
                    text.insert(start, editing);
                } else {
                    text += editing;
                }
            }
            dialog->setText(text);
            dialog->setCountText(fmt::format("{}/{}", utf8_len(this->inputBuffer), maxStringLength));
        };
        auto updateTextCursor = [this, dialog]() {
            int cursor = this->cursor;
            if (cursor >= (int)CursorPosition::START && this->editingBuffer.size() > 0) {
                cursor += utf8_len(this->editingBuffer) + 2;
            }
            dialog->setCursor(cursor);
        };
        auto updateTextAndCursor = [this, updateText, updateTextCursor, maxStringLength](std::string text) {
            size_t prev_n = utf8_len(this->inputBuffer);
            if (prev_n >= maxStringLength) {
                return;
            }
            size_t n = utf8_len(text);
            if (prev_n + n > maxStringLength) {
                n = maxStringLength - prev_n;
                int end = utf8_find_next(text, 0, n);
                text = text.substr(0, end);
            }
            if (this->cursor >= 0) {
                int start = utf8_find_next(this->inputBuffer, 0, this->cursor);
                this->inputBuffer.insert(start, text);
                this->cursor += n;
            } else {
                this->inputBuffer += text;
            }
            updateTextCursor();
            updateText();
        };
        dialog->setHeaderText(headerText);
        dialog->setHintText(subText);
        cursor = -1;
        updateText();
        if(!initialText.empty()) updateTextCursor();
#if defined(BOREALIS_USE_D3D11)
        float scale = Application::windowScale;
 #else
        float scale = Application::windowScale / Application::getPlatform()->getVideoContext()->getScaleFactor();
#endif
        // 更新输入法条位置
        dialog->getLayoutEvent()->subscribe([scale](Point p) {
#ifndef PS4
            const SDL_Rect rect = {(int)(p.x* scale), (int)(p.y * scale), 100, 20};
            SDL_SetTextInputRect(&rect);
#endif
        });

        dialog->getClipboardEvent()->subscribe([this, updateTextAndCursor](const std::string& str)
            {
                if(this->isEditing || str.empty()) return;
                updateTextAndCursor(str);
            });

        auto eventID1 = event->subscribe([this, updateTextAndCursor, updateText, updateTextCursor](SDL_Event *e) {
            switch (e->type) {
            case SDL_TEXTINPUT:
                // Content is deliberately not logged -- this same path handles
                // password fields. The length is what tells you it arrived.
                this->isEditing = false;
                updateTextAndCursor(e->text.text);
                break;
            case SDL_TEXTEDITING:
                if (strlen(e->edit.text) == 0) {
                    this->isEditing = false;
                } else {
                    this->isEditing = true;
                }
                this->editingBuffer = e->edit.text;
                updateTextCursor();
                updateText();
                break;
            }
        });
        
        dialog->getApplet()->registerAction(
            "hints/left"_i18n, BUTTON_LEFT, [this, updateTextCursor](...){
                if (this->isEditing) return true;
                if (this->cursor == (int)CursorPosition::END) {
                    this->cursor = utf8_len(this->inputBuffer) - 1;
                    if(this->cursor < 0) this->cursor = 0;
                    updateTextCursor();
                } else if (this->cursor > (int)CursorPosition::START) {
                    this->cursor--;
                    updateTextCursor();
                }
                return true;
            }, true, true
        );
        dialog->getApplet()->registerAction(
            "hints/right"_i18n, BUTTON_RIGHT, [this, updateTextCursor](...){
                if (this->isEditing) return true;
                if (this->cursor >= (int)CursorPosition::START) {
                    if (this->cursor < utf8_len(this->inputBuffer)) {
                        this->cursor++;
                        updateTextCursor();
                    }
                }
                return true;
            }, true, true
        );

        // delete text
        dialog->getBackspaceEvent()->subscribe([this, updateText, updateTextCursor](...) {
            if(inputBuffer.empty()) return true;
            if (this->cursor == (int)CursorPosition::START) return true;
            if (this->cursor > (int)CursorPosition::START) {
                int start = utf8_find_next(inputBuffer, 0, this->cursor-1);
                int n = utf8_find_next(inputBuffer, start, 1);
                inputBuffer.erase(start, n);
                this->cursor -= 1;
                updateTextCursor();
            } else {
                int offset = utf8_find_prev(inputBuffer, 1);
                inputBuffer.erase(inputBuffer.size()-offset, offset);
            }
            updateText();
            return true;
        });

        // cancel
        dialog->getCancelEvent()->subscribe([this, eventID1
#ifdef PS5_NATIVE_GPU
            , imeRequest
#endif
        ]() {
#ifdef PS5_NATIVE_GPU
            nativeImeCompletion(imeRequest, ps5_native_ime::Stage::WrapperCancel);
            SDL_ClearError();
#endif
            SDL_StopTextInput();
#ifdef PS5_NATIVE_GPU
            nativeImeCompletion(imeRequest, ps5_native_ime::Stage::AfterStop);
#endif
            event->unsubscribe(eventID1);
        });

        // submit
        dialog->getSubmitEvent()->subscribe([this, eventID1, cb
#ifdef PS5_NATIVE_GPU
            , imeRequest
#endif
        ]() {
#ifdef PS5_NATIVE_GPU
            nativeImeCompletion(imeRequest, ps5_native_ime::Stage::WrapperSubmit);
            SDL_ClearError();
#endif
            SDL_StopTextInput();
#ifdef PS5_NATIVE_GPU
            nativeImeCompletion(imeRequest, ps5_native_ime::Stage::AfterStop);
#endif
            event->unsubscribe(eventID1);
            cb(this->inputBuffer);
            return true;
        });

        // Whether a platform's native IME actually appears is not something the
        // caller can see, and on some backends (PS5) it is the open question.
        // SDL_StartTextInput is what asks for it, via ShowScreenKeyboard.
#ifdef PS5_NATIVE_GPU
        SDL_Window* focus = SDL_GetKeyboardFocus();
#endif
#ifdef PS5_NATIVE_GPU
        ps5_native_ime::Event imeObservation;
        imeObservation.request_id = imeRequest;
        imeObservation.focus = focus != nullptr;
        imeObservation.screen_keyboard_enabled = SDL_GetHintBoolean(SDL_HINT_ENABLE_SCREEN_KEYBOARD, SDL_TRUE) == SDL_TRUE;
#endif

#ifdef __PS5__
        // The PS5 IME dialog is modal and owns the whole editing session: it
        // opens with its own buffer and hands the finished string back in one
        // SDL_TEXTINPUT. So seed it with what is already in the field, and start
        // ours empty, or the returned text is appended to the old value and a
        // long string cannot be corrected without clearing it first.
        //
        // Cancelling is safe: the submit callback is what propagates the value,
        // and cancel does not call it, so the caller keeps what it had.
#ifdef PS5_NATIVE_GPU
        imeObservation.initial_hint_set = SDL_SetHint(SDL_HINT_PS5_IME_INITIAL_TEXT, this->inputBuffer.c_str()) == SDL_TRUE;
        imeObservation.password_hint_set = SDL_SetHint(SDL_HINT_PS5_IME_PASSWORD, isPassword ? "1" : "0") == SDL_TRUE;
        imeObservation.limit_hint_set = SDL_SetHint(SDL_HINT_PS5_IME_MAX_TEXT_LENGTH, std::to_string(maxStringLength).c_str()) == SDL_TRUE;
#else
        SDL_SetHint(SDL_HINT_PS5_IME_INITIAL_TEXT, this->inputBuffer.c_str());
#endif
        this->inputBuffer.clear();
        this->cursor = -1;
        updateText();
#endif

#ifdef PS5_NATIVE_GPU
        imeObservation.error = ps5_native_ime::classify_error(SDL_GetError());
        imeObservation.text_events_active = SDL_IsTextInputActive() == SDL_TRUE;
        ps5_native_ime::emit(imeObservation);
        SDL_ClearError();
#endif
        SDL_StartTextInput();

#ifdef PS5_NATIVE_GPU
        // Capture before any status query or diagnostic sink can replace the
        // request-local SDL error. Only fixed categories and numeric codes leave
        // this function; this observation does not change the provider result.
        imeObservation.error = ps5_native_ime::classify_error(SDL_GetError());
        imeObservation.stage = ps5_native_ime::Stage::AfterStart;
        imeObservation.text_events_active = SDL_IsTextInputActive() == SDL_TRUE;
        ps5_native_ime::emit(imeObservation);
        imeObservation.shown_queried = focus != nullptr;
        imeObservation.shown = focus && SDL_IsScreenKeyboardShown(focus) == SDL_TRUE;
        imeObservation.error = ps5_native_ime::classify_error(SDL_GetError());
        imeObservation.stage = ps5_native_ime::Stage::AfterShown;
        ps5_native_ime::emit(imeObservation);
#endif

        dialog->open();
    }

    bool SDLImeManager::openForText(std::function<void(std::string)> f, std::string headerText,
        std::string subText, int maxStringLength, std::string initialText,
        int kbdDisableBitmask)
    {
#ifdef __PSV__
        vita_ime_type = 0;
#endif
        this->openInputDialog([f](const std::string& text)
            { f(text); },
            headerText, subText, maxStringLength, initialText);
        return true;
    }

    bool SDLImeManager::openForPassword(std::function<void(std::string)> f, std::string headerText,
        std::string subText, int maxStringLength, std::string initialText)
    {
        this->openInputDialog([f](const std::string& text)
            {if(!text.empty()) f(text); },
            headerText, subText, maxStringLength, initialText, true);
        return true;
    }

    bool SDLImeManager::openForNumber(std::function<void(long)> f, std::string headerText,
        std::string subText, int maxStringLength, std::string initialText,
        std::string leftButton, std::string rightButton,
        int kbdDisableBitmask)
    {
#ifdef __PSV__
        vita_ime_type = 2;
#endif
        this->openInputDialog([f](const std::string& text) {
            if(text.empty()) return ;
            try
            {
                f(stoll(text));
            }
            catch (const std::invalid_argument& e)
            {
                Logger::error("Could not parse input, did you enter a valid integer? {}", e.what());
            }
            catch (const std::out_of_range& e) {
                Logger::error("Out of range: {}", e.what());
            }
            catch (const std::exception& e)
            {
                Logger::error("Unexpected error occurred: {}", e.what());
            }
        },headerText, subText, maxStringLength, initialText);
        return true;
    }
}
