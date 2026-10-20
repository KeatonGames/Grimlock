#include "input/input.hpp"

#include <iostream>
#include <stdexcept>

using namespace grimlock;

namespace {

void require(bool value, const char* text) {
    if (!value) {
        throw std::runtime_error(text);
    }
}

} // namespace

int main() {
    SDL_Window* window = nullptr;

    try {
        require(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        window = SDL_CreateWindow("Input checks", 640, 480, SDL_WINDOW_HIDDEN);
        require(window != nullptr, SDL_GetError());

        {
            platform::Input input{window};
            const auto id = SDL_GetWindowID(window);
            SDL_Event event{};
            event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            event.window.windowID = id;
            input.handleEvent(event);
            input.beginFrame();

            event = {};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.windowID = id;
            event.button.button = SDL_BUTTON_LEFT;
            event.button.x = 50;
            event.button.y = 60;
            input.handleEvent(event);
            require(input.state().button(MouseButton::left).pressed && input.state().button(MouseButton::left).held,
                    "mouse pressed/held missing");
            require(input.state().leftPressPosition == glm::vec2{50, 60}, "press coordinates missing");

            SDL_Event moved{};
            moved.type = SDL_EVENT_MOUSE_MOTION;
            moved.motion.windowID = id;
            moved.motion.x = 500;
            moved.motion.y = 400;
            input.handleEvent(moved);
            require(input.state().leftPressPosition == glm::vec2{50, 60}, "later motion changed press coordinates");

            input.beginFrame();
            input.handleEvent(event);
            require(!input.state().button(MouseButton::left).pressed, "duplicate press edge");
            require(!input.state().leftPressPosition, "held mouse repeated picking coordinates");

            event.type = SDL_EVENT_MOUSE_BUTTON_UP;
            input.handleEvent(event);
            require(input.state().button(MouseButton::left).released && !input.state().button(MouseButton::left).held,
                    "release missing");

            event = {};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.windowID = id;
            event.key.scancode = SDL_SCANCODE_W;
            input.handleEvent(event);
            require(input.state().key(SDL_SCANCODE_W).pressed, "key pressed missing");

            input.beginFrame();
            event.key.repeat = true;
            input.handleEvent(event);
            require(input.state().key(SDL_SCANCODE_W).held && !input.state().key(SDL_SCANCODE_W).pressed,
                    "key repeat created press edge");

            event.type = SDL_EVENT_KEY_UP;
            input.handleEvent(event);
            require(input.state().key(SDL_SCANCODE_W).released, "key release missing");

            event = {};
            event.type = SDL_EVENT_MOUSE_MOTION;
            event.motion.windowID = id;
            event.motion.x = 100;
            event.motion.y = 200;
            event.motion.xrel = 2;
            event.motion.yrel = -3;
            input.handleEvent(event);
            input.handleEvent(event);
            require(input.state().mouseMotion == glm::vec2{4, -6} && input.state().mousePosition == glm::vec2{100, 200},
                    "motion accumulation failed");

            event = {};
            event.type = SDL_EVENT_MOUSE_WHEEL;
            event.wheel.windowID = id;
            event.wheel.y = 0.25F;
            event.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
            input.handleEvent(event);
            require(input.state().mouseWheel.y == -0.25F, "fractional/flipped wheel failed");

            event.wheel.windowID = id + 1;
            input.handleEvent(event);
            require(input.state().mouseWheel.y == -0.25F, "foreign window event leaked");

            event = {};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.windowID = id;
            event.button.button = SDL_BUTTON_RIGHT;
            input.handleEvent(event);

            event = {};
            event.type = SDL_EVENT_KEY_DOWN;
            event.key.windowID = id;
            event.key.scancode = SDL_SCANCODE_A;
            input.handleEvent(event);

            event = {};
            event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
            event.window.windowID = id;
            input.handleEvent(event);
            require(!input.state().focused && !input.state().button(MouseButton::right).held &&
                        !input.state().key(SDL_SCANCODE_A).held,
                    "focus loss stuck input");
            require(input.state().mouseMotion == glm::vec2{0} && input.state().mouseWheel == glm::vec2{0},
                    "focus loss retained deltas");

            input.beginFrame();
            require(!input.state().button(MouseButton::right).released, "release did not expire");

            event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
            input.handleEvent(event);

            event = {};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.windowID = id;
            event.button.button = SDL_BUTTON_MIDDLE;
            input.handleEvent(event);

            event = {};
            event.type = SDL_EVENT_WINDOW_RESIZED;
            event.window.windowID = id;
            input.handleEvent(event);
            input.finishFrame(true);
            require(!input.state().button(MouseButton::middle).held && !SDL_GetWindowRelativeMouseMode(window),
                    "resize retained drag/capture");

            event = {};
            event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.windowID = id;
            event.button.button = SDL_BUTTON_RIGHT;
            input.handleEvent(event);
            input.finishFrame(true);
            require(SDL_GetWindowRelativeMouseMode(window), "drag mode was not enabled");
        }

        require(!SDL_GetWindowRelativeMouseMode(window), "Input destructor retained relative mode");

        SDL_DestroyWindow(window);
        window = nullptr;
        SDL_Quit();
        std::cout << "PASS input: edges, repeats, motion/position, fractional/flipped wheel, "
                     "foreign windows, focus and resize cancellation.\n";
        return 0;
    } catch (const std::exception& e) {
        if (window) {
            SDL_DestroyWindow(window);
        }

        SDL_Quit();
        std::cerr << "FAIL input: " << e.what() << '\n';

        return 1;
    }
}
