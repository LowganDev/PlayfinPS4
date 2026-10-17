#include <doctest/doctest.h>

#include <SDL.h>

#include <cstdio>

#include "platform/events.hpp"

using playfin::platform::Action;
using playfin::platform::Event;
using playfin::platform::EventTranslator;

namespace {

SDL_Event key(Uint32 type, SDL_Keycode sym, Uint8 repeat = 0) {
    SDL_Event e{};
    e.type = type;
    e.key.keysym.sym = sym;
    e.key.repeat = repeat;
    return e;
}

SDL_Event button(Uint32 type, Uint8 b) {
    SDL_Event e{};
    e.type = type;
    e.cbutton.button = b;
    return e;
}

SDL_Event axis(Uint8 a, Sint16 value) {
    SDL_Event e{};
    e.type = SDL_CONTROLLERAXISMOTION;
    e.caxis.axis = a;
    e.caxis.value = value;
    return e;
}

}  // namespace

TEST_CASE("keyboard keys map to actions") {
    EventTranslator t;
    auto down = t.translate(key(SDL_KEYDOWN, SDLK_DOWN));
    REQUIRE(down.has_value());
    CHECK(down->type == Event::Type::ActionDown);
    CHECK(down->action == Action::Down);
    auto up = t.translate(key(SDL_KEYUP, SDLK_RETURN));
    REQUIRE(up.has_value());
    CHECK(up->type == Event::Type::ActionUp);
    CHECK(up->action == Action::Confirm);
    CHECK(t.translate(key(SDL_KEYDOWN, SDLK_ESCAPE))->action == Action::Back);
    CHECK(t.translate(key(SDL_KEYDOWN, SDLK_BACKSPACE))->action == Action::Back);
}

TEST_CASE("system key repeat and unknown keys are ignored") {
    EventTranslator t;
    CHECK_FALSE(t.translate(key(SDL_KEYDOWN, SDLK_DOWN, 1)).has_value());
    CHECK_FALSE(t.translate(key(SDL_KEYDOWN, SDLK_F7)).has_value());
}

TEST_CASE("controller buttons map to actions") {
    EventTranslator t;
    CHECK(t.translate(button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_A))->action == Action::Confirm);
    CHECK(t.translate(button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_B))->action == Action::Back);
    CHECK(t.translate(button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT))->action == Action::Left);
    CHECK(t.translate(button(SDL_CONTROLLERBUTTONUP, SDL_CONTROLLER_BUTTON_START))->type == Event::Type::ActionUp);
    CHECK_FALSE(t.translate(button(SDL_CONTROLLERBUTTONDOWN, SDL_CONTROLLER_BUTTON_GUIDE)).has_value());
}

TEST_CASE("triggers press once past the threshold and release below it") {
    EventTranslator t;
    auto pressed = t.translate(axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 20000));
    REQUIRE(pressed.has_value());
    CHECK(pressed->action == Action::L2);
    CHECK(pressed->type == Event::Type::ActionDown);
    CHECK_FALSE(t.translate(axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 30000)).has_value());
    CHECK_FALSE(t.translate(axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 12000)).has_value());
    auto released = t.translate(axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1000));
    REQUIRE(released.has_value());
    CHECK(released->type == Event::Type::ActionUp);
    CHECK_FALSE(t.translate(axis(SDL_CONTROLLER_AXIS_LEFTX, 32000)).has_value());
}

TEST_CASE("quit and device events") {
    EventTranslator t;
    SDL_Event quit{};
    quit.type = SDL_QUIT;
    CHECK(t.translate(quit)->type == Event::Type::Quit);
    SDL_Event added{};
    added.type = SDL_CONTROLLERDEVICEADDED;
    CHECK_FALSE(t.translate(added).has_value());
    SDL_Event removed{};
    removed.type = SDL_CONTROLLERDEVICEREMOVED;
    CHECK_FALSE(t.translate(removed).has_value());
}

TEST_CASE("text mode turns typing into text events and backspace into deletion") {
    EventTranslator t;
    SDL_Event typed{};
    typed.type = SDL_TEXTINPUT;
    std::snprintf(typed.text.text, sizeof(typed.text.text), "%s", "\xC3\xA9");
    CHECK_FALSE(t.translate(typed).has_value());
    t.setTextMode(true);
    const auto text = t.translate(typed);
    REQUIRE(text.has_value());
    CHECK(text->type == Event::Type::Text);
    CHECK(text->text == "\xC3\xA9");
    CHECK(t.translate(key(SDL_KEYDOWN, SDLK_BACKSPACE))->type == Event::Type::EditDelete);
    CHECK(t.translate(key(SDL_KEYDOWN, SDLK_ESCAPE))->action == Action::Back);
    CHECK_FALSE(t.translate(key(SDL_KEYDOWN, SDLK_t)).has_value());  // letters are text, not actions
    t.setTextMode(false);
    CHECK(t.translate(key(SDL_KEYDOWN, SDLK_BACKSPACE))->action == Action::Back);
}
