#pragma once

#include <SDL.h>
#include <functional>
#include <vector>
#include <memory>

// InputHandler forwards input events (keyboard + controller) to a callback
class InputHandler {
public:
    // action: mgba::ACTION_PRESS or ACTION_RELEASE, key: mgba keycode
    using SendInputCb = std::function<void(uint8_t action, uint8_t key)>;

    InputHandler();
    ~InputHandler();

    // Set the callback used to deliver inputs
    void setSendCallback(SendInputCb cb);

    // Process an SDL_Event; returns true if handled
    bool processEvent(const SDL_Event& ev);

    // Poll per-frame to handle axes if needed
    void poll();

private:
    SendInputCb m_cb;
    // track opened game controllers
    std::vector<SDL_GameController*> m_controllers;

    void openController(int deviceIndex);
    void closeController(SDL_JoystickID instanceId);
    uint8_t mapControllerButton(SDL_GameControllerButton b);
};
