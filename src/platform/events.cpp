#include "platform/events.hpp"

namespace playfin::platform {

namespace {

std::optional<Action> fromKey(SDL_Keycode key) {
    switch (key) {
    case SDLK_UP:
        return Action::Up;
    case SDLK_DOWN:
        return Action::Down;
    case SDLK_LEFT:
        return Action::Left;
    case SDLK_RIGHT:
        return Action::Right;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        return Action::Confirm;
    case SDLK_ESCAPE:
    case SDLK_BACKSPACE:
        return Action::Back;
    case SDLK_t:
        return Action::Options;
    case SDLK_x:
        return Action::Context;
    case SDLK_q:
        return Action::L1;
    case SDLK_e:
        return Action::R1;
    case SDLK_1:
        return Action::L2;
    case SDLK_3:
        return Action::R2;
    case SDLK_m:
        return Action::Menu;
    default:
        return std::nullopt;
    }
}

std::optional<Action> fromButton(Uint8 button) {
    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return Action::Up;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return Action::Down;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return Action::Left;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return Action::Right;
    case SDL_CONTROLLER_BUTTON_A:
        return Action::Confirm;
    case SDL_CONTROLLER_BUTTON_B:
        return Action::Back;
    case SDL_CONTROLLER_BUTTON_Y:
        return Action::Options;
    case SDL_CONTROLLER_BUTTON_X:
        return Action::Context;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return Action::L1;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return Action::R1;
    case SDL_CONTROLLER_BUTTON_START:
        return Action::Menu;
    default:
        return std::nullopt;
    }
}

std::optional<Event> make(Event::Type type, std::optional<Action> action) {
    if (!action) {
        return std::nullopt;
    }
    return actionEvent(type, *action);
}

}  // namespace

Event actionEvent(Event::Type type, Action action) {
    Event event;
    event.type = type;
    event.action = action;
    return event;
}

std::optional<Event> EventTranslator::translate(const SDL_Event& event) {
    if (textMode_) {
        if (event.type == SDL_TEXTINPUT) {
            Event text;
            text.type = Event::Type::Text;
            text.text = event.text.text;
            return text;
        }
        if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
            const SDL_Keycode sym = event.key.keysym.sym;
            if (sym == SDLK_BACKSPACE) {
                if (event.type == SDL_KEYDOWN) {
                    Event erase;
                    erase.type = Event::Type::EditDelete;
                    return erase;
                }
                return std::nullopt;
            }
            const bool editingKey = sym == SDLK_RETURN || sym == SDLK_KP_ENTER || sym == SDLK_ESCAPE ||
                                    sym == SDLK_LEFT || sym == SDLK_RIGHT || sym == SDLK_UP || sym == SDLK_DOWN;
            if (!editingKey) {
                return std::nullopt;
            }
        }
    }
    switch (event.type) {
    case SDL_QUIT:
        return actionEvent(Event::Type::Quit, Action::Confirm);
    case SDL_KEYDOWN:
        if (event.key.repeat != 0) {
            return std::nullopt;
        }
        return make(Event::Type::ActionDown, fromKey(event.key.keysym.sym));
    case SDL_KEYUP:
        return make(Event::Type::ActionUp, fromKey(event.key.keysym.sym));
    case SDL_CONTROLLERBUTTONDOWN:
        return make(Event::Type::ActionDown, fromButton(event.cbutton.button));
    case SDL_CONTROLLERBUTTONUP:
        return make(Event::Type::ActionUp, fromButton(event.cbutton.button));
    case SDL_CONTROLLERAXISMOTION:
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            return trigger(l2Down_, Action::L2, event.caxis.value);
        }
        if (event.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            return trigger(r2Down_, Action::R2, event.caxis.value);
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

std::optional<Event> EventTranslator::trigger(bool& down, Action action, int value) {
    if (!down && value > 16000) {
        down = true;
        return actionEvent(Event::Type::ActionDown, action);
    }
    if (down && value < 8000) {
        down = false;
        return actionEvent(Event::Type::ActionUp, action);
    }
    return std::nullopt;
}

}  // namespace playfin::platform
