#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <string>
#include <SDL.h>

struct GuiHandle {
    SDL_Window* win;
    SDL_Renderer* ren;
    SDL_Texture* tex;
};

bool gui_init(GuiHandle& handle, int winW, int winH);
void gui_shutdown(GuiHandle& handle, bool closeAudio, int audioDev);
void gui_draw_frame(GuiHandle& handle, const std::vector<unsigned char>& pixels);

// Set a short status string visible to the user (window title + clears the window)
void gui_set_status(GuiHandle& handle, const std::string& status);
