#include "InputHandler.h"
#include "../web/StreamingCommon.h"
#include <algorithm>
#include <stdio.h>

InputHandler::InputHandler() {
    // Ensure gamecontroller subsystem is initialized so events are generated
    if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) == 0) {
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    SDL_GameControllerEventState(SDL_ENABLE);

    // Open any currently connected controllers
    int n = SDL_NumJoysticks();
    for (int i = 0; i < n; ++i) {
        if (SDL_IsGameController(i)) openController(i);
    }
}

InputHandler::~InputHandler() {
    for (auto c : m_controllers) if (c) SDL_GameControllerClose(c);
    SDL_GameControllerEventState(SDL_DISABLE);
    // Do not SDL_QuitSubSystem here; leave SDL main lifecycle to the app
}

void InputHandler::setSendCallback(SendInputCb cb) { m_cb = cb; }

void InputHandler::openController(int deviceIndex) {
    if (!SDL_IsGameController(deviceIndex)) return;
    SDL_GameController* gc = SDL_GameControllerOpen(deviceIndex);
    if (!gc) return;
    m_controllers.push_back(gc);
    fprintf(stdout, "InputHandler: opened controller %s\n", SDL_GameControllerName(gc));
}

void InputHandler::closeController(SDL_JoystickID instanceId) {
    auto it = std::find_if(m_controllers.begin(), m_controllers.end(), [&](SDL_GameController* c){
        if (!c) return false;
        SDL_Joystick* j = SDL_GameControllerGetJoystick(c);
        return j && SDL_JoystickInstanceID(j) == instanceId;
    });
    if (it != m_controllers.end()) {
        SDL_GameControllerClose(*it);
        m_controllers.erase(it);
    }
}

uint8_t InputHandler::mapControllerButton(SDL_GameControllerButton b) {
    // Map typical controller layout to mGBA keys
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return mgba::KEY_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return mgba::KEY_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return mgba::KEY_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return mgba::KEY_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: return mgba::KEY_Z; // A -> Z
    case SDL_CONTROLLER_BUTTON_B: return mgba::KEY_X; // B -> X
    case SDL_CONTROLLER_BUTTON_X: return mgba::KEY_A; // X -> A
    case SDL_CONTROLLER_BUTTON_Y: return mgba::KEY_S; // Y -> S
    case SDL_CONTROLLER_BUTTON_START: return mgba::KEY_ENTER;
    case SDL_CONTROLLER_BUTTON_BACK: return mgba::KEY_TAB;
    default: return 0;
    }
}

bool InputHandler::processEvent(const SDL_Event& ev) {
    if (ev.type == SDL_CONTROLLERDEVICEADDED) {
        openController(ev.cdevice.which);
        return true;
    }
    if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
        closeController(ev.cdevice.which);
        return true;
    }
    if (ev.type == SDL_CONTROLLERBUTTONDOWN || ev.type == SDL_CONTROLLERBUTTONUP) {
        if (!m_cb) return true;
        uint8_t action = (ev.type == SDL_CONTROLLERBUTTONDOWN) ? mgba::ACTION_PRESS : mgba::ACTION_RELEASE;
        uint8_t key = mapControllerButton((SDL_GameControllerButton)ev.cbutton.button);
        if (key != 0) {
            fprintf(stdout, "InputHandler: controller button %d -> key %d action %d\n", ev.cbutton.button, key, action);
            m_cb(action, key);
        } else {
            fprintf(stdout, "InputHandler: controller button %d unmapped\n", ev.cbutton.button);
        }
        return true;
    }

    // Keyboard handling
    if (ev.type == SDL_KEYDOWN || ev.type == SDL_KEYUP) {
        if (!m_cb) return true;
        uint8_t action = (ev.type == SDL_KEYDOWN) ? mgba::ACTION_PRESS : mgba::ACTION_RELEASE;
        uint8_t keycode = 0;
        switch (ev.key.keysym.sym) {
        case SDLK_UP: keycode = mgba::KEY_UP; break;
        case SDLK_DOWN: keycode = mgba::KEY_DOWN; break;
        case SDLK_LEFT: keycode = mgba::KEY_LEFT; break;
        case SDLK_RIGHT: keycode = mgba::KEY_RIGHT; break;
        case SDLK_z: case SDLK_z - 32: keycode = mgba::KEY_Z; break;
        case SDLK_x: keycode = mgba::KEY_X; break;
        case SDLK_a: keycode = mgba::KEY_A; break;
        case SDLK_s: keycode = mgba::KEY_S; break;
        case SDLK_RETURN: keycode = mgba::KEY_ENTER; break;
        case SDLK_TAB: keycode = mgba::KEY_TAB; break;
        default: keycode = 0; break;
        }
        if (keycode != 0) m_cb(action, keycode);
        return true;
    }

    return false;
}

void InputHandler::poll() {
    // Could handle analog sticks here and generate press/release events for thresholds
}
