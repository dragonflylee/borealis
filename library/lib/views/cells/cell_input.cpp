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
#include <libretro-common/encodings/utf.h>
#include <nanovg.h>

namespace brls
{

// Byte offset of a codepoint index. utf8skip() keeps us on a codepoint boundary;
// it doesn't stop at '\0', so clamp the index first.
static size_t utf8ByteOffset(const std::string& s, int codepoints)
{
    if (codepoints <= 0)
        return 0;
    if (codepoints > (int)utf8len(s.c_str()))
        return s.size();
    return (size_t)(utf8skip(s.c_str(), (size_t)codepoints) - s.c_str());
}

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
        // Password cells also edit inline, but with the IME off (see beginInlineEdit).
        if (Application::getImeManager()->supportsInlineEditing())
        {
            // Enter maps to BUTTON_A, so one click both starts and commits.
            if (this->inlineEditing)
                this->endInlineEdit(true);
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
    // TOP alignment keeps the caret (drawn from y downwards) in sync with the text.
    this->detail->setVerticalAlign(VerticalAlign::TOP);
    this->detail->setSingleLine(true);
    this->detail->setAnimated(false);
    this->detail->setAutoAnimate(false);
    this->detail->setGrow(1);
    this->detail->setMaxWidth(View::AUTO);
    // Keep the line tall enough that the caret is not drawn squashed when empty.
    this->detail->setMinHeight(this->detail->getFontSize());
    this->refreshInlineActions();

    // Passwords never hand composition to the IME, so it cannot learn or suggest them.
    Application::getImeManager()->startInlineEditing(
        [this](const std::string& text) { this->inlineInsert(text); },
        [this](const std::string& preview) {
            this->composeBuffer = preview;
            this->renderInline();
        },
        this->type == InputCellType::PASSWORD);

    this->renderInline();
    Application::giveFocus(this);

    // Layout changed this frame, so getFrame() is stale: report once it settles.
    auto alive = this->alive;
    brls::sync([this, alive]() {
        if (*alive && this->inlineEditing)
            this->updateCaretRectangle();
    });
}

void InputCell::updateCaretRectangle()
{
    // Same view->window scaling as EditTextDialog::openInputDialog.
    float scale = Application::windowScale;
#ifndef BOREALIS_USE_D3D11
    scale /= Application::getPlatform()->getVideoContext()->getScaleFactor();
#endif

    Rect frame = this->detail->getFrame();

    // The label is right aligned and wider than its text, so measure up to the caret
    // and walk left from the right edge to find the real insertion point.
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

    // 1px wide on the text line; height is the font size so the candidate window sits
    // next to the glyphs, not the whole cell.
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

    // Unregistering now would invalidate the vector handleAction() is iterating.
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

    // Mask keeps one dot per codepoint, so codepoint indices still line up with the
    // clear text and the caret math below works for both modes.
    bool masked = this->type == InputCellType::PASSWORD && !this->inlineShowPlain;
    std::string display = this->editBuffer;
    if (masked)
    {
        display.clear();
        for (size_t i = 0; i < utf8len(this->editBuffer.c_str()); i++)
            display += "●";
    }

    if (this->editBuffer.empty() && this->composeBuffer.empty())
    {
        // A lone space keeps the row width stable while the buffer is empty.
        display = " ";
    }
    else if (!this->composeBuffer.empty())
    {
        // Composition is always shown in clear so the user can see what they type.
        display.insert(utf8ByteOffset(display, this->editCursor), this->composeBuffer);
    }
    this->detail->setText(display);
    // Caret sits after the composition preview, not at editCursor (which precedes it).
    this->detail->setCursor(this->editCursor + (int)utf8len(this->composeBuffer.c_str()));
    this->updateCaretRectangle();
}

void InputCell::refreshInlineActions()
{
    auto addKey = [this](BrlsKeyboardScancode key, const ActionListener& cb, bool repeating) {
        this->inlineActions.push_back(this->registerAction(BrlsKeyCombination{ key }, cb, repeating));
    };
    auto addButton = [this](ControllerButton button, const ActionListener& cb) {
        this->inlineActions.push_back(this->registerAction("", button, cb, true, true));
    };

    // Enter is BUTTON_A (handled by the click action); Escape is BUTTON_B.
    addButton(BUTTON_B, [this](View*) {
        this->endInlineEdit(false);
        return true;
    });
    // Arrow keys set both BUTTON_LEFT/RIGHT and BUTTON_NAV_LEFT/RIGHT, and each button is
    // dispatched separately. Moving the caret on one and letting the other navigate either
    // skipped two cells or stole focus, so: move on NAV_*, and only consume the plain ones.
    addButton(BUTTON_LEFT, [](View*) { return true; });
    addButton(BUTTON_RIGHT, [](View*) { return true; });
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

    // Shortcut modifier: Ctrl elsewhere, Cmd on macOS.
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

    // Ctrl+H reveals a password buffer. EditTextDialog uses BUTTON_GUIDE for this, but
    // desktop keyboards have no such key, so a key combo is the only reachable option.
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
        // Truncate only the incoming text so a mid-string caret keeps the tail intact.
        if ((int)utf8len(add.c_str()) > remain)
            add = add.substr(0, utf8ByteOffset(add, remain));
    }
    // A commit ends the composition: clear the preview first, or it gets spliced back
    // into the text and the caret overshoots.
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
    // Re-render so the caret blink and candidate window both follow.
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
        std::string filled;
        size_t len = utf8len(this->value.c_str());
        for (size_t i = 0; i < len; i++) filled += "●";
        this->detail->setText(filled);
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
