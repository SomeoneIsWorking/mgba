#include "Gui.h"
#include <SDL.h>
#include <cstdio>
#include "../web/StreamingCommon.h"

bool gui_init(GuiHandle& handle, int winW, int winH) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    handle.win = SDL_CreateWindow("mGBA Stream", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, winW, winH, SDL_WINDOW_RESIZABLE);
    if (!handle.win) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); return false; }
    handle.ren = SDL_CreateRenderer(handle.win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!handle.ren) { fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError()); return false; }
    handle.tex = nullptr;
    return true;
}

// Simple status setter: update window title and clear the window to a dark background.
void gui_set_status(GuiHandle& handle, const std::string& status) {
    if (!handle.win || !handle.ren) return;
    SDL_SetWindowTitle(handle.win, status.c_str());
    // Clear to dark gray background and present so user sees status even without frames
    SDL_SetRenderDrawColor(handle.ren, 30, 30, 30, 255);
    SDL_RenderClear(handle.ren);
    SDL_RenderPresent(handle.ren);
}

void gui_shutdown(GuiHandle& handle, bool closeAudio, int audioDev) {
    (void)closeAudio; (void)audioDev;
    if (handle.tex) SDL_DestroyTexture(handle.tex);
    if (handle.ren) SDL_DestroyRenderer(handle.ren);
    if (handle.win) SDL_DestroyWindow(handle.win);
    SDL_Quit();
}

void gui_draw_frame(GuiHandle& handle, const std::vector<unsigned char>& pixels) {
    if (!handle.ren) return;
    const int hw = mgba::STREAM_WIDTH;
    const int hh = mgba::STREAM_HEIGHT;
    if (!handle.tex) {
        handle.tex = SDL_CreateTexture(handle.ren, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STREAMING, hw, hh);
        if (handle.tex) SDL_SetTextureBlendMode(handle.tex, SDL_BLENDMODE_NONE);
    }
    if (handle.tex) SDL_UpdateTexture(handle.tex, NULL, pixels.data(), hw * 3);
    int wndW, wndH; SDL_GetWindowSize(handle.win, &wndW, &wndH);
    float srcAspect = (float)hw / (float)hh;
    float wndAspect = (float)wndW / (float)wndH;
    int dstW, dstH;
    if (wndAspect > srcAspect) {
        dstH = wndH;
        dstW = (int)(dstH * srcAspect + 0.5f);
    } else {
        dstW = wndW;
        dstH = (int)(dstW / srcAspect + 0.5f);
    }
    SDL_Rect dstRect{ (wndW - dstW)/2, (wndH - dstH)/2, dstW, dstH };
    SDL_RenderClear(handle.ren);
    SDL_RenderCopy(handle.ren, handle.tex, nullptr, &dstRect);
    SDL_RenderPresent(handle.ren);
}
