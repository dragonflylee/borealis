#include <borealis/views/edit_text_dialog.hpp>
#include <borealis/core/i18n.hpp>
#include <borealis/core/thread.hpp>
#include <borealis/core/touch/tap_gesture.hpp>
#include <borealis/core/application.hpp>
#include <libretro-common/encodings/utf.h>

#ifdef __PSV__
#define EDIT_TEXT_DIALOG_POP_ANIMATION TransitionAnimation::NONE
#define EDIT_TEXT_DIALOG_BACKGROUND_TRANSLUCENT false
#include <SDL2/SDL.h>
#include "borealis/platforms/sdl/sdl_video.hpp"
#else
#define EDIT_TEXT_DIALOG_POP_ANIMATION TransitionAnimation::FADE
#define EDIT_TEXT_DIALOG_BACKGROUND_TRANSLUCENT true
#endif

namespace brls
{

    static constexpr float KEY_MARGIN = 2.0f;

    enum KeyRole
    {
        KEY_TEXT = 0,
        KEY_BACKSPACE,
        KEY_SHIFT,
        KEY_SYMBOLS,
        KEY_COMMIT,
    };

    const std::string editTextDialogXML = R"xml(
    <brls:Box
        id="brls/container"
        width="auto"
        height="auto"
        axis="column"
        justifyContent="flexStart"
        alignItems="center"
        backgroundColor="@theme/brls/backdrop">

        <brls:Label
            id="brls/dialog/header"
            fontSize="24")xml"
#ifdef __PSV__
            R"xml(marginTop="30")xml"
#else
            R"xml(marginTop="50")xml"
#endif
      R"xml(marginBottom="30"
            textColor="#FFFFFF"/>

        <brls:Box
            id="brls/dialog/applet"
            width="720"
            cornerRadius="4"
            alignItems="flexEnd"
            axis="column"
            focusable="true"
            hideHighlight="true"
            backgroundColor="@theme/brls/background">

            <brls:Label
                id="brls/dialog/label"
                grow="1"
                width="680"
                cursor="-1"
                minHeight="30"
                margin="20"
                autoAnimate="false"
                verticalAlign="top"/>

            <brls:Label
                id="brls/dialog/count"
                width="680"
                horizontalAlign="right"
                fontSize="18"
                textColor="@theme/brls/text_disabled"
                marginRight="30"
                marginBottom="10"/>

            <brls:Hints
                allowAButtonTouch="true"
                forceShown="true"
                addBaseAction="false"
                marginBottom="20"
                marginRight="10"
                width="auto"
                height="auto"/>
        </brls:Box>

        <brls:Box
            id="brls/dialog/keyboard"
            width="auto"
            axis="column"
            marginTop="16"
            marginBottom="24"
            alignItems="center">
        </brls:Box>

    </brls:Box>
    )xml";

    EditTextDialog::EditTextDialog()
    {
        this->inflateFromXMLString(editTextDialogXML);

        // submit text
        this->applet->registerAction(
            "hints/ok"_i18n, BUTTON_A, [this](...)
            {
                Application::popActivity(EDIT_TEXT_DIALOG_POP_ANIMATION, [this](){
                        this->summitEvent.fire();
                    });
                return true; });
        this->registerAction(
            "hints/ok"_i18n, BUTTON_START, [this](...)
            {
                Application::popActivity(EDIT_TEXT_DIALOG_POP_ANIMATION, [this](){
                        this->summitEvent.fire();
                    });
                return true; }, true);

        // register paste action
        int pasteKey = BRLS_KBD_MODIFIER_CTRL;
#ifdef __APPLE__
        pasteKey = BRLS_KBD_MODIFIER_META;
#endif
        this->registerAction({ BRLS_KBD_KEY_V, pasteKey }, [this](...)
        {
            this->clipboardEvent.fire(Application::getPlatform()->pasteFromClipboard());
            return true;
        });

        // register delete action
        this->registerAction({ BRLS_KBD_KEY_DELETE }, [this](...)
        {
            // This is to ensure that the delete operation of the PSV ime will not be ignored
            this->backspaceEvent.fire();
            return true;
        }, true);
        this->registerAction({ BRLS_KBD_KEY_BACKSPACE }, [this](...)
        {
            this->backspaceEvent.fire();
            return true;
        }, true);

        this->registerAction("hints/delete"_i18n, BUTTON_BACK, [this](...)
            {
                this->backspaceEvent.fire();
                return true; }, true, true);

        // cancel input
        this->registerAction(
            "hints/back"_i18n, BUTTON_B, [this](...)
            {
                Application::popActivity(EDIT_TEXT_DIALOG_POP_ANIMATION, [this](){
                        this->cancelEvent.fire();
                    });
                return true; });

#if defined(__PSV__) and !defined(BOREALIS_USE_GXM)
        // After turning off the on-screen keyboard, tap on the input area to reopen
        this->addGestureRecognizer(new brls::TapGestureRecognizer([](brls::TapGestureStatus status, brls::Sound*) {
            if (status.state == brls::GestureState::END) {
                auto* videoContext = (SDLVideoContext*)Application::getPlatform()->getVideoContext();
                if (!SDL_IsScreenKeyboardShown(videoContext->getSDLWindow()))
                {
                    SDL_StopTextInput();
                    SDL_StartTextInput();
                }
                Application::setInputType(InputType::GAMEPAD);
            }
        }));
        // Force to set to gamepad mode to avoid missing the `Enter` keyboard event
        Application::setInputType(InputType::GAMEPAD);
#endif

        this->init = true;

        this->registerAction("Shift", BUTTON_Y, [this](...)
        {
            this->pressKey(KEY_SHIFT);
            return true; }, true, true);

        this->registerAction("hints/delete"_i18n, BUTTON_X, [this](...)
            {
                this->backspaceEvent.fire();
                return true; }, true, true);

        this->setKeyboardEnabled(Application::getInputType() != InputType::TOUCH);
    }

    EditTextDialog::~EditTextDialog() = default;

    void EditTextDialog::open()
    {
        Application::pushActivity(new Activity(this));
    }

    void EditTextDialog::setText(const std::string& value)
    {
        this->content = value;
        this->updateUI();
    }

    void EditTextDialog::setHeaderText(const std::string& value)
    {
        this->header->setText(value);
    }

    void EditTextDialog::setHintText(const std::string& value)
    {
        if (value.empty())
        {
            this->hint = "hints/input"_i18n;
        }
        else
        {
            this->hint = value;
        }
        this->updateUI();
    }

    void EditTextDialog::setCountText(const std::string& value)
    {
        this->count->setText(value);
    }

    bool EditTextDialog::isTranslucent()
    {
        return EDIT_TEXT_DIALOG_BACKGROUND_TRANSLUCENT;
    }

    void EditTextDialog::onLayout()
    {
        if (!init)
            return;
        this->layoutEvent.fire(Point { this->label->getX(),
            this->label->getY() + this->label->getHeight() });
    }

    brls::Box *EditTextDialog::getApplet()
    {
        return this->applet;
    }

    Event<Point>* EditTextDialog::getLayoutEvent()
    {
        return &this->layoutEvent;
    }

    Event<>* EditTextDialog::getBackspaceEvent()
    {
        return &this->backspaceEvent;
    }

    Event<>* EditTextDialog::getCancelEvent()
    {
        return &this->cancelEvent;
    }

    Event<>* EditTextDialog::getSubmitEvent()
    {
        return &this->summitEvent;
    }

    Event<std::string>* EditTextDialog::getClipboardEvent()
    {
        return &this->clipboardEvent;
    }

    void EditTextDialog::updateUI()
    {
        if (content.empty())
        {
            label->setTextColor(Application::getTheme().getColor("brls/text_disabled"));
            label->setText(hint);
            label->setCursor((int)CursorPosition::START);
        }
        else if (this->passwordStyle)
        {
            std::string filled;
            size_t len = utf8len(this->content.c_str());
            for (size_t i = 0; i < len; i++) filled += "●";
            label->setTextColor(Application::getTheme().getColor("brls/text"));
            label->setText(filled);
        }
        else
        {
            label->setTextColor(Application::getTheme().getColor("brls/text"));
            label->setText(content);
        }
    }

    void EditTextDialog::setCursor(int cursor) {
        label->setCursor(cursor);
    }

    void EditTextDialog::setPasswordStyle(bool value)
    {
        this->passwordStyle = value;
        if (value)
        {
            this->passwordEvent = this->registerAction(
                "hints/password"_i18n, BUTTON_GUIDE, [this](...)
                { 
                    this->passwordStyle = !this->passwordStyle;
                    this->updateUI();
                    return true; });
        }
        else if (this->passwordEvent)
        {
            this->unregisterAction(this->passwordEvent);
        }
    }

    void EditTextDialog::setKeyboardEnabled(bool enabled)
    {
        this->keyboardShown = enabled;
        this->keyboard->setVisibility(enabled ? Visibility::VISIBLE : Visibility::GONE);
        this->buildKeyboard();
    }

    void EditTextDialog::pressKey(int role)
    {
        switch (role)
        {
            case KEY_BACKSPACE:
                this->backspaceEvent.fire();
                break;
            case KEY_SHIFT:
                this->keyboardMode = this->keyboardMode == KeyboardMode::UPPERCASE ? KeyboardMode::LOWERCASE
                                                                                 : KeyboardMode::UPPERCASE;
                this->rebuildKeyboard();
                break;
            case KEY_SYMBOLS:
                this->keyboardMode = this->keyboardMode == KeyboardMode::SYMBOLS ? KeyboardMode::LOWERCASE
                                                                                : KeyboardMode::SYMBOLS;
                this->rebuildKeyboard();
                break;
            case KEY_COMMIT:
                Application::popActivity(EDIT_TEXT_DIALOG_POP_ANIMATION, [this](){
                        this->summitEvent.fire();
                    });
                break;
            default:
                break;
        }
    }

    void EditTextDialog::rebuildKeyboard()
    {
        ASYNC_RETAIN
        brls::sync([ASYNC_TOKEN]()
            {
                ASYNC_RELEASE
                if (!this->keyboardShown)
                    return;
                this->buildKeyboard();
            });
    }

    void EditTextDialog::buildKeyboard()
    {
        View* focused = Application::getCurrentFocus();

        int focusRow = -1, focusCol = -1;
        std::vector<View*>& rows = this->keyboard->getChildren();
        for (size_t r = 0; r < rows.size() && focusRow < 0; r++)
        {
            std::vector<View*>& keys = ((Box*)rows[r])->getChildren();
            for (size_t c = 0; c < keys.size(); c++)
            {
                if (keys[c] == focused)
                {
                    focusRow = (int)r;
                    focusCol = (int)c;
                    break;
                }
            }
        }

        this->keyboard->clearViews();

        if (!this->keyboardShown)
        {
            Application::giveFocus(this->applet);
            return;
        }

        bool symbols = this->keyboardMode == KeyboardMode::SYMBOLS;
        bool upper   = this->keyboardMode == KeyboardMode::UPPERCASE;

        struct Key
        {
            std::string label;
            float units;
            int role;
            bool highlight;
        };
        using Row = std::vector<Key>;

        auto letters = [upper](const std::string& keys) -> Row
        {
            Row row;
            for (char c : keys)
            {
                if (upper)
                    c = (char)std::toupper((unsigned char)c);
                row.push_back({ std::string(1, c), 1.0f, KEY_TEXT, false });
            }
            return row;
        };

        std::vector<Row> layout;
        if (symbols)
        {
            layout = { letters("1234567890"), letters("@#$%&*-+()"), letters("!\"':;,/?=_") };
        }
        else
        {
            layout = { letters("qwertyuiop"), letters("asdfghjkl;"), letters("zxcvbnm,./") };
        }

        layout.push_back({
            { symbols ? "ABC" : "?123", 1.5f, KEY_SYMBOLS, symbols },
            { " ", 4.0f, KEY_TEXT, false },
            { "\uE0E1", 1.5f, KEY_BACKSPACE, false },
            { "\uE0E3", 1.5f, KEY_SHIFT, upper },
            { "OK", 2.5f, KEY_COMMIT, true },
        });

        float widest = 0;
        for (auto& row : layout)
        {
            float units = 0;
            for (auto& key : row)
                units += key.units;
            widest = std::max(widest, units);
        }
        float available = Application::contentWidth > 0 ? Application::contentWidth : Application::ORIGINAL_WINDOW_WIDTH;
        int unit        = (int)(available * 0.94f / widest) - KEY_MARGIN * 2;
        unit            = std::min(std::max(unit, 24), 100);
        int keyHeight   = std::max(unit * 65 / 100, 26);
        int fontSize    = std::max(unit * 25 / 100, 13);

        Theme theme = Application::getTheme();

        auto addKey = [this, &theme, unit, keyHeight, fontSize](Box* row, const std::string& text,
            float units, int role, bool highlight)
        {
            Box* key = new Box();
            key->setWidth(unit * units);
            key->setHeight(keyHeight);
            key->setMargins(KEY_MARGIN, KEY_MARGIN, KEY_MARGIN, KEY_MARGIN);
            key->setCornerRadius(keyHeight / 4.0f);
            key->setFocusable(true);
            key->setAxis(Axis::ROW);
            key->setJustifyContent(JustifyContent::CENTER);
            key->setAlignItems(AlignItems::CENTER);
            row->addView(key);

            Label* keyLabel = new Label();
            keyLabel->setText(text);
            keyLabel->setFontSize(fontSize);
            keyLabel->setHorizontalAlign(HorizontalAlign::CENTER);
            key->addView(keyLabel);

            if (highlight)
            {
                key->setBackgroundColor(theme["brls/button/primary_enabled_background"]);
                keyLabel->setTextColor(theme["brls/button/primary_enabled_text"]);
            }
            else
            {
                key->setBackgroundColor(theme["brls/button/default_enabled_background"]);
                keyLabel->setTextColor(theme["brls/button/default_enabled_text"]);
            }

            key->registerClickAction([this, text, role](View*)
                {
                    if (role == KEY_TEXT)
                        this->clipboardEvent.fire(text);
                    else
                        this->pressKey(role);
                    return true; });
            key->addGestureRecognizer(new TapGestureRecognizer(key));

            return key;
        };

        std::vector<std::vector<View*>> grid;
        for (auto& row : layout)
        {
            Box* rowBox = new Box(Axis::ROW);
            rowBox->setFocusable(false);
            rowBox->setJustifyContent(JustifyContent::CENTER);
            this->keyboard->addView(rowBox);

            std::vector<View*> built;
            for (auto& key : row)
                built.push_back(addKey(rowBox, key.label, key.units, key.role, key.highlight));
            grid.push_back(built);
        }

        for (auto& row : grid)
        {
            if (row.size() < 2)
                continue;
            row.back()->setCustomNavigationRoute(FocusDirection::RIGHT, row.front());
            row.front()->setCustomNavigationRoute(FocusDirection::LEFT, row.back());
        }

        auto nearest = [](size_t c, size_t from, size_t to) -> size_t {
            if (from <= 1 || to == 0)
                return 0;
            return std::min(to - 1, (size_t)((double)c / (from - 1) * (to - 1) + 0.5));
        };
        for (size_t r = 0; r + 1 < grid.size(); r++)
        {
            for (size_t c = 0; c < grid[r].size(); c++)
                grid[r][c]->setCustomNavigationRoute(FocusDirection::DOWN,
                    grid[r + 1][nearest(c, grid[r].size(), grid[r + 1].size())]);
            for (size_t c = 0; c < grid[r + 1].size(); c++)
                grid[r + 1][c]->setCustomNavigationRoute(FocusDirection::UP,
                    grid[r][nearest(c, grid[r + 1].size(), grid[r].size())]);
        }

        for (View* key : grid.front())
            key->setCustomNavigationRoute(FocusDirection::UP, this->applet);
        this->applet->setCustomNavigationRoute(FocusDirection::DOWN, grid.front().front());

        if (focusRow >= 0 && !grid.empty())
        {
            size_t r = std::min((size_t)focusRow, grid.size() - 1);
            size_t c = std::min((size_t)focusCol, grid[r].size() - 1);
            Application::giveFocus(grid[r][c]);
        }
        else if (!grid.empty() && !grid.front().empty())
            Application::giveFocus(grid.front().front());
        else
            Application::giveFocus(this->applet);
    }

}
