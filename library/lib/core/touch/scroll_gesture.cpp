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

#include <borealis.hpp>

#ifndef NO_TOUCH_SCROLLING
#define NO_TOUCH_SCROLLING false
#endif

namespace brls
{

ScrollGestureRecognizer::ScrollGestureRecognizer(PanGestureEvent::Callback respond, PanAxis axis)
    : PanGestureRecognizer(respond, axis)
{
}

GestureState ScrollGestureRecognizer::recognitionLoop(std::array<TouchState, TOUCHES_MAX> touches, MouseState mouse, View* view, Sound* soundToPlay)
{
    if (!enabled)
        return GestureState::FAILED;

    if (touches[0].phase != TouchPhase::NONE)
        return PanGestureRecognizer::recognitionLoop(touches, mouse, view, soundToPlay);

    GestureState result;
    if (NO_TOUCH_SCROLLING)
        result = GestureState::FAILED;
    else if (mouse.scroll.x == 0 && mouse.scroll.y == 0)
        result = PanGestureRecognizer::recognitionLoop(touches, mouse, view, soundToPlay);
    else
    {
        result = GestureState::STAY;
        PanGestureStatus status {
            .state         = GestureState::STAY,
            .position      = Point(),
            .startPosition = Point(),
            .delta         = mouse.scroll,
            .deltaOnly     = true,
        };
        this->getPanGestureEvent().fire(status, soundToPlay);
    }

    return result;
}

};
