/*
Borealis, a Nintendo Switch UI Library
Copyright (C) 2019  natinusala
Copyright (C) 2024  xfangfang

This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/notification_manager.hpp>

namespace brls
{

NotificationManager::NotificationManager()
{
    // Toasts are bottom-centered pills: the manager is an invisible
    // full-width column anchored right above the applet frame footer
    // (bottom bar), whose children stack upwards from the bottom.
    Style style        = Application::getStyle();
    float margin       = style.getMetric("brls/notification/margin");
    float footerHeight = style.getMetric("brls/applet_frame/footer_height");

    this->setWidth(Application::ORIGINAL_WINDOW_WIDTH);
    this->setHeight(Application::ORIGINAL_WINDOW_HEIGHT - footerHeight - margin);
    this->setAxis(Axis::COLUMN);
    this->setJustifyContent(JustifyContent::FLEX_END);
    this->setAlignItems(AlignItems::CENTER);
}

void NotificationManager::notify(const std::string& text)
{
    // Create the notification
    brls::Logger::debug("Showing notification \"{}\"", text);

    auto* notification = new Notification(text);
    this->addView(notification); // newest pill closest to the bottom, older ones pushed up

    // Timeout timer: slide -> 0 (fade in + slide up), hold, 0 -> slide (fade out + slide down)
    auto style    = Application::getStyle();
    float timeout = style.getMetric("brls/animations/notification_timeout");
    float show    = style.getMetric("brls/animations/notification_show");
    float slide   = style.getMetric("brls/notification/slide");
    notification->timeoutTimer.reset(slide);
    notification->timeoutTimer.addStep(0.0f, (int)show, EasingFunction::quadraticOut);
    notification->timeoutTimer.addStep(0.0f, (int)timeout, EasingFunction::linear);
    notification->timeoutTimer.addStep(slide, (int)show, EasingFunction::quadraticOut);

    notification->timeoutTimer.setTickCallback([notification, slide]()
        {
            float position = notification->timeoutTimer.getValue();
            notification->setTranslationY(position);
            notification->setAlpha(1.0f - position / slide);
        });

    notification->timeoutTimer.setEndCallback([this, notification](bool finished)
        { this->removeView(notification); });

    notification->timeoutTimer.start();
}

NotificationManager::~NotificationManager()
{
    std::vector<View*> views = this->getChildren();
    for (auto& view : views)
    {
        auto label = dynamic_cast<Notification*>(view);
        label->timeoutTimer.stop();
    }
}

Notification::Notification(const std::string& text)
{
    auto style = Application::getStyle();
    auto theme = Application::getTheme();

    // Pill look: translucent dark rounded background hugging the text,
    // generous lateral padding. The corner radius is kept at height/2 in
    // onLayout() so the shape stays a pill even with multi-line text.
    this->setBackgroundColor(theme["brls/notification/background"]);
    float paddingSides     = style.getMetric("brls/notification/padding_sides");
    float paddingTopBottom = style.getMetric("brls/notification/padding_top_bottom");
    this->setPadding(paddingTopBottom, paddingSides, paddingTopBottom, paddingSides);
    this->setMaxWidth(style.getMetric("brls/notification/max_width"));
    this->setMarginTop(style.getMetric("brls/notification/spacing"));

    this->label = new Label();
    this->label->setText(text);
    this->label->setTextColor(theme["brls/notification/text"]);
    this->label->setHorizontalAlign(HorizontalAlign::CENTER);
    this->addView(label);
}

void Notification::onLayout()
{
    Box::onLayout();
    this->setCornerRadius(this->getHeight() / 2);
}

Notification::~Notification() = default;

}; // namespace brls
