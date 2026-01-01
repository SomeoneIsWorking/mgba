const std = @import("std");
const mgba = @import("mgba.zig");
const c = mgba.c;

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer _ = gpa.deinit();
    const allocator = gpa.allocator();

    const args = try std.process.argsAlloc(allocator);
    defer std.process.argsFree(allocator, args);

    if (args.len < 2) {
        std.debug.print("Usage: {s} <rom_path>\n", .{args[0]});
        return;
    }

    if (c.SDL_Init(c.SDL_INIT_VIDEO | c.SDL_INIT_AUDIO) < 0) {
        std.debug.print("SDL_Init failed: {s}\n", .{c.SDL_GetError()});
        return error.SDLInitFailed;
    }
    defer c.SDL_Quit();

    var core = try mgba.Core.init(.gba);
    defer core.deinit();

    try core.loadRom(args[1]);
    core.reset();

    const sample_rate = core.handle.audioSampleRate.?(core.handle) * 2;
    std.debug.print("Audio sample rate (doubled): {d}\n", .{sample_rate});

    const audio_spec = c.SDL_AudioSpec{
        .freq = @intCast(sample_rate),
        .format = c.AUDIO_S16SYS,
        .channels = 2,
        .samples = 1024,
        .callback = null,
        .userdata = null,
    };
    const audio_device = c.SDL_OpenAudioDevice(null, 0, &audio_spec, null, 0);
    if (audio_device != 0) {
        c.SDL_PauseAudioDevice(audio_device, 0);
    }
    defer if (audio_device != 0) c.SDL_CloseAudioDevice(audio_device);

    const size = core.getDesiredVideoSize();
    const window = c.SDL_CreateWindow("mGBA Zig", c.SDL_WINDOWPOS_CENTERED, c.SDL_WINDOWPOS_CENTERED, @intCast(size.width * 2), @intCast(size.height * 2), c.SDL_WINDOW_SHOWN) orelse {
        std.debug.print("SDL_CreateWindow failed: {s}\n", .{c.SDL_GetError()});
        return error.SDLWindowFailed;
    };
    defer c.SDL_DestroyWindow(window);

    const renderer = c.SDL_CreateRenderer(window, -1, c.SDL_RENDERER_ACCELERATED | c.SDL_RENDERER_PRESENTVSYNC) orelse {
        std.debug.print("SDL_CreateRenderer failed: {s}\n", .{c.SDL_GetError()});
        return error.SDLRendererFailed;
    };
    defer c.SDL_DestroyRenderer(renderer);

    const texture = c.SDL_CreateTexture(renderer, c.SDL_PIXELFORMAT_ABGR8888, c.SDL_TEXTUREACCESS_STREAMING, @intCast(size.width), @intCast(size.height)) orelse {
        std.debug.print("SDL_CreateTexture failed: {s}\n", .{c.SDL_GetError()});
        return error.SDLTextureFailed;
    };
    defer c.SDL_DestroyTexture(texture);

    var quit = false;
    var keys: u32 = 0;
    var renderer_initialized = false;
    while (!quit) {
        var event: c.SDL_Event = undefined;
        while (c.SDL_PollEvent(&event) != 0) {
            if (event.type == c.SDL_QUIT) {
                quit = true;
            } else if (event.type == c.SDL_KEYDOWN or event.type == c.SDL_KEYUP) {
                const down = event.type == c.SDL_KEYDOWN;
                const bit: ?u5 = switch (event.key.keysym.sym) {
                    c.SDLK_z => @intFromEnum(mgba.Key.a),
                    c.SDLK_x => @intFromEnum(mgba.Key.b),
                    c.SDLK_RETURN => @intFromEnum(mgba.Key.start),
                    c.SDLK_RSHIFT => @intFromEnum(mgba.Key.select),
                    c.SDLK_UP => @intFromEnum(mgba.Key.up),
                    c.SDLK_DOWN => @intFromEnum(mgba.Key.down),
                    c.SDLK_LEFT => @intFromEnum(mgba.Key.left),
                    c.SDLK_RIGHT => @intFromEnum(mgba.Key.right),
                    c.SDLK_a => @intFromEnum(mgba.Key.l),
                    c.SDLK_s => @intFromEnum(mgba.Key.r),
                    else => null,
                };
                if (bit) |b| {
                    if (down) {
                        keys |= (@as(u32, 1) << b);
                    } else {
                        keys &= ~(@as(u32, 1) << b);
                    }
                }
            }
        }

        core.setKeys(keys);

        var pixels: ?*anyopaque = undefined;
        var pitch: i32 = 0;
        if (c.SDL_LockTexture(texture, null, &pixels, &pitch) < 0) {
            std.debug.print("SDL_LockTexture failed: {s}\n", .{c.SDL_GetError()});
            break;
        }

        core.setVideoBuffer(@ptrCast(@alignCast(pixels)), @intCast(@divExact(pitch, 4)));
        if (!renderer_initialized) {
            core.reloadConfigOption("hwaccelVideo");
            renderer_initialized = true;
        }
        core.runFrame();

        c.SDL_UnlockTexture(texture);
        _ = c.SDL_RenderClear(renderer);
        _ = c.SDL_RenderCopy(renderer, texture, null, null);
        c.SDL_RenderPresent(renderer);

        if (core.getAudioBuffer()) |audio_buf| {
            var avail = c.mAudioBufferAvailable(audio_buf);
            while (avail > 0) {
                var samples: [4096]i16 = undefined;
                const to_read = @min(avail, samples.len / 2);
                const read = c.mAudioBufferRead(audio_buf, &samples, to_read);
                if (read > 0) {
                    _ = c.SDL_QueueAudio(audio_device, &samples, @intCast(read * 2 * 2));
                }
                avail -= read;
                if (read == 0) break;
            }
        }
    }
}
