/*
    Copyright 2021 XITRIX

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

#include "borealis/views/cells/cell_input.hpp"
#include "borealis/views/dropdown.hpp"
#include <borealis/core/thread.hpp>
#include <borealis/core/util.hpp>
#include <libretro-common/encodings/utf.h>
#include <nanovg.h>

namespace brls
{

InputCell::InputCell()
{
    detail->setTextColor(Application::getTheme()["brls/list/listItem_value_color"]);

    BRLS_REGISTER_ENUM_XML_ATTRIBUTE(
        "type", InputCellType, this->setType,
        {
            { "text", InputCellType::TEXT },
            { "url", InputCellType::URL },
            { "password", InputCellType::PASSWORD },
        });

    this->registerClickAction([this](View* view) {
        if (Application::getImeManager()->supportsInlineEditing())
        {
            if (this->inlineEditing)
            {
                // End the session before setValue(): the callback may rebuild the list and
                // re-enter via onFocusLost(), committing the stale buffer over the new value.
                std::string text = this->editBuffer;
                this->endInlineEdit(false);
                if (text != this->value)
                    this->setValue(text);
                else
                    this->updateUI();
            }
            else
                this->beginInlineEdit();
            return true;
        }

        switch (this->type) {
            case InputCellType::PASSWORD: {
                return Application::getImeManager()->openForPassword([&](std::string text) { this->setValue(text); },
                    this->title->getFullText(), this->hint, this->maxInputLength, this->value);
            }
            default: {
                return Application::getImeManager()->openForText([&](std::string text) { this->setValue(text); },
                    this->title->getFullText(), this->hint, this->maxInputLength, this->value, this->kbdDisableBitmask);
            }
        }
    });
}

void InputCell::beginInlineEdit()
{
    this->inlineEditing   = true;
    this->inlineShowPlain = false;
    this->editBuffer      = this->value;
    this->editCursor      = (int)utf8len(this->editBuffer.c_str());
    this->composeBuffer.clear();
    this->title->setGrow(0);
    this->detail->setHorizontalAlign(HorizontalAlign::RIGHT);
    // TOP: the caret is drawn from y downwards, matching the text.
    this->detail->setVerticalAlign(VerticalAlign::TOP);
    this->detail->setSingleLine(true);
    this->detail->setAnimated(false);
    this->detail->setAutoAnimate(false);
    this->detail->setGrow(1);
    this->detail->setMaxWidth(View::AUTO);
    this->detail->setMinHeight(this->detail->getFontSize());
    this->refreshInlineActions();

    // Passwords keep composition away from the IME.
    Application::getImeManager()->startInlineEditing(
        [this](const std::string& text) { this->inlineInsert(text); },
        [this](const std::string& preview) {
            this->composeBuffer = preview;
            this->renderInline();
        },
        this->type == InputCellType::PASSWORD);

    this->renderInline();
    Application::giveFocus(this);

    // getFrame() is stale until the layout settles.
    auto alive = this->alive;
    brls::sync([this, alive]() {
        if (*alive && this->inlineEditing)
            this->updateCaretRectangle();
    });
}

void InputCell::updateCaretRectangle()
{
    float scale = Application::windowScale;
#ifndef BOREALIS_USE_D3D11
    scale /= Application::getPlatform()->getVideoContext()->getScaleFactor();
#endif

    Rect frame = this->detail->getFrame();

    // Right aligned: measure up to the caret, then walk left from the right edge.
    std::string caretText = this->editBuffer.substr(0, utf8ByteOffset(this->editBuffer, this->editCursor));
    caretText += this->composeBuffer;

    float caretX = frame.origin.x + frame.size.width;
    if (!caretText.empty())
    {
        NVGcontext* vg = Application::getNVGContext();
        nvgFontSize(vg, this->detail->getFontSize());
        nvgFontFaceId(vg, this->detail->getFont());
        float bounds[4];
        nvgTextBounds(vg, 0, 0, caretText.c_str(), nullptr, bounds);
        caretX -= (bounds[2] - bounds[0]);
    }

    // 1px wide, font height tall: puts the candidate window by the glyphs, not the cell.
    Application::getImeManager()->setInlineEditingRect(
        caretX * scale, frame.origin.y * scale, scale, this->detail->getFontSize() * scale);
}

void InputCell::endInlineEdit(bool commit)
{
    if (!this->inlineEditing)
        return;
    this->inlineEditing   = false;
    this->inlineShowPlain = false;
    Application::getImeManager()->stopInlineEditing();
    this->composeBuffer.clear();
    this->title->setGrow(1);
    this->detail->setVerticalAlign(VerticalAlign::CENTER);
    this->detail->setSingleLine(false);
    this->detail->setAutoAnimate(true);
    this->detail->setGrow(0);
    this->detail->setMaxWidth(300);
    this->detail->setMinHeight(View::AUTO);
    this->detail->setCursor((int)CursorPosition::UNSET);

    // Deferred: unregistering now would invalidate the vector handleAction() iterates.
    auto alive   = this->alive;
    auto actions = std::move(this->inlineActions);
    this->inlineActions.clear();
    brls::sync([this, alive, actions]() {
        if (!*alive)
            return;
        for (auto id : actions)
            this->unregisterAction(id);
    });

    if (commit && this->editBuffer != this->value)
        this->setValue(this->editBuffer);
    else
        this->updateUI();
}

void InputCell::renderInline()
{
    Theme theme = Application::getTheme();
    this->detail->setTextColor(theme["brls/list/listItem_value_color"]);

    // One mask per codepoint, so caret indices stay valid in both modes.
    std::string display = this->editBuffer;
    bool masked        = this->type == InputCellType::PASSWORD && !this->inlineShowPlain;
    if (masked)
        display = maskCodepoints(this->editBuffer);

    if (this->editBuffer.empty() && this->composeBuffer.empty())
    {
        // A lone space keeps the row width stable.
        display = " ";
    }
    else if (!this->composeBuffer.empty())
    {
        display.insert(utf8ByteOffset(display, this->editCursor), this->composeBuffer);
    }
    this->detail->setText(display);
    // Caret after the composition preview, not at editCursor.
    this->detail->setCursor(this->editCursor + (int)utf8len(this->composeBuffer.c_str()));
    this->updateCaretRectangle();
}

void InputCell::refreshInlineActions()
{
    auto addKey = [this](BrlsKeyboardScancode key, const ActionListener& cb, bool repeating) {
        this->inlineActions.push_back(this->registerAction(BrlsKeyCombination{ key }, cb, repeating));
    };
    auto addButton = [this](ControllerButton button, const ActionListener& cb) {
        // Non-repeating: handleAction() drops repeats, so holding an arrow would step
        // one NAV move per key-repeat and skip codepoints.
        this->inlineActions.push_back(this->registerAction("", button, cb, true, false));
    };

    addButton(BUTTON_B, [this](View*) {
        this->endInlineEdit(false);
        return true;
    });
    // Arrows raise both BUTTON_LEFT/RIGHT and BUTTON_NAV_*. Move on NAV_*, and consume the
    // plain ones so they cannot navigate; they only move the caret when the keyboard is
    // not the source (a platform that never derives NAV_* from arrows).
    addButton(BUTTON_LEFT, [this](View*) {
        if (!Application::getPlatform()->getInputManager()->getKeyboardKeyState(BRLS_KBD_KEY_LEFT))
            this->inlineMoveCursor(-1);
        return true;
    });
    addButton(BUTTON_RIGHT, [this](View*) {
        if (!Application::getPlatform()->getInputManager()->getKeyboardKeyState(BRLS_KBD_KEY_RIGHT))
            this->inlineMoveCursor(1);
        return true;
    });
    addButton(BUTTON_NAV_LEFT, [this](View*) {
        this->inlineMoveCursor(-1);
        return true;
    });
    addButton(BUTTON_NAV_RIGHT, [this](View*) {
        this->inlineMoveCursor(1);
        return true;
    });

    // Mirrors EditTextDialog: DELETE and BACKSPACE both delete.
    addKey(BRLS_KBD_KEY_BACKSPACE, [this](View*) {
        this->inlineBackspace();
        return true; },
        true);
    addKey(BRLS_KBD_KEY_DELETE, [this](View*) {
        this->inlineBackspace();
        return true; },
        true);

    int shortcutMod = BRLS_KBD_MODIFIER_CTRL;
#ifdef __APPLE__
    shortcutMod = BRLS_KBD_MODIFIER_META;
#endif

    this->inlineActions.push_back(this->registerAction(
        BrlsKeyCombination{ BRLS_KBD_KEY_V, shortcutMod },
        [this](View*) {
            this->inlineInsert(Application::getPlatform()->pasteFromClipboard());
            return true;
        },
        false));

    // Ctrl+H reveals a password buffer; desktop has no BUTTON_GUIDE.
    if (this->type == InputCellType::PASSWORD)
    {
        this->inlineActions.push_back(this->registerAction(
            BrlsKeyCombination{ BRLS_KBD_KEY_H, shortcutMod },
            [this](View*) {
                this->inlineShowPlain = !this->inlineShowPlain;
                this->renderInline();
                return true;
            },
            false));
    }
}

void InputCell::inlineInsert(const std::string& text)
{
    std::string add = text;
    if (this->maxInputLength > 0)
    {
        int cur    = (int)utf8len(this->editBuffer.c_str());
        int remain = this->maxInputLength - cur;
        if (remain <= 0)
            return;
        // Truncate the incoming text only, so a mid-string caret keeps the tail.
        if ((int)utf8len(add.c_str()) > remain)
            add = add.substr(0, utf8ByteOffset(add, remain));
    }
    // Committing ends the composition, so drop the preview first.
    size_t offset = utf8ByteOffset(this->editBuffer, this->editCursor);
    this->editBuffer.insert(offset, add);
    this->editCursor += (int)utf8len(add.c_str());
    this->composeBuffer.clear();
    this->renderInline();
}

void InputCell::inlineBackspace()
{
    if (this->editCursor <= 0)
        return;
    size_t prevOffset = utf8ByteOffset(this->editBuffer, this->editCursor - 1);
    size_t curOffset  = utf8ByteOffset(this->editBuffer, this->editCursor);
    this->editBuffer.erase(prevOffset, curOffset - prevOffset);
    this->editCursor--;
    this->renderInline();
}

void InputCell::inlineMoveCursor(int delta)
{
    int next = this->editCursor + delta;
    int max  = (int)utf8len(this->editBuffer.c_str());
    if (next < 0)
        next = 0;
    if (next > max)
        next = max;
    this->editCursor = next;
    this->renderInline();
}

void InputCell::onFocusLost()
{
    DetailCell::onFocusLost();
    if (this->inlineEditing)
        this->endInlineEdit(true);
}

InputCell::~InputCell()
{
    *this->alive = false;
    if (this->inlineEditing)
    {
        this->inlineEditing = false;
        Application::getImeManager()->stopInlineEditing();
    }
    for (auto id : this->inlineActions)
        this->unregisterAction(id);
    this->inlineActions.clear();
}

void InputCell::init(std::string title, std::string value, Event<std::string>::Callback callback, std::string placeholder, std::string hint, int maxInputLength, int kbdDisableBitmask)
{
    this->hint  = hint;
    this->value = value;
    this->title->setText(title);
    this->placeholder       = placeholder;
    this->maxInputLength    = maxInputLength;
    this->kbdDisableBitmask = kbdDisableBitmask;
    this->event.subscribe(callback);
    updateUI();
}

void InputCell::setValue(std::string value)
{
    this->event.fire(value);
    this->value = value;
    updateUI();
}

void InputCell::setPlaceholder(std::string placeholder)
{
    this->placeholder = placeholder;
    updateUI();
}

void InputCell::setType(InputCellType type)
{
    this->type = type;
}

void InputCell::updateUI()
{
    Theme theme = Application::getTheme();
    if (this->value.empty())
    {
        this->detail->setText(placeholder);
        this->detail->setTextColor(theme["brls/text_disabled"]);
    }
    else if (this->type == InputCellType::PASSWORD)
    {
        this->detail->setText(maskCodepoints(this->value));
        this->detail->setTextColor(theme["brls/list/listItem_value_color"]);
    }
    else
    {
        this->detail->setText(value);
        this->detail->setTextColor(theme["brls/list/listItem_value_color"]);
    }
}

View* InputCell::create()
{
    return new InputCell();
}

InputNumericCell::InputNumericCell()
{
    detail->setTextColor(Application::getTheme()["brls/list/listItem_value_color"]);

    this->registerClickAction([this](View* view)
        {
        Application::getImeManager()->openForNumber([&](long number) {
            this->setValue(number);
        },
            this->title->getFullText(), this->hint, this->maxInputLength, std::to_string(this->value), "", "", this->kbdDisableBitmask);

        return true; });
}

void InputNumericCell::init(std::string title, long value, Event<long>::Callback callback, std::string hint, int maxInputLength, int kbdDisableBitmask)
{
    this->hint  = hint;
    this->value = value;
    this->title->setText(title);
    this->maxInputLength    = maxInputLength;
    this->kbdDisableBitmask = kbdDisableBitmask;
    this->event.subscribe(callback);
    updateUI();
}

void InputNumericCell::setValue(long value)
{
    this->event.fire(value);
    this->value = value;
    updateUI();
}

void InputNumericCell::updateUI()
{
    Theme theme = Application::getTheme();
    this->detail->setText(std::to_string(value));
    this->detail->setTextColor(theme["brls/list/listItem_value_color"]);
}

View* InputNumericCell::create()
{
    return new InputNumericCell();
}

} // namespace brls
