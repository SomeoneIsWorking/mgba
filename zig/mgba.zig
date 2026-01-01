const std = @import("std");

pub const c = @cImport({
    @cDefine("ENABLE_VFS", "1");
    @cDefine("ENABLE_DIRECTORIES", "1");
    @cInclude("../include/mgba/core/core.h");
    @cInclude("../include/mgba/core/interface.h");
    @cInclude("../include/mgba/core/config.h");
    @cInclude("../include/mgba/core/thread.h");
    @cInclude("../include/mgba/internal/gba/input.h");
    @cInclude("../include/mgba-util/vfs.h");
    @cInclude("../include/mgba-util/audio-buffer.h");
    @cInclude("SDL.h");
});

pub const Platform = enum(c_int) {
    gba = c.mPLATFORM_GBA,
    gb = c.mPLATFORM_GB,
};

pub const Key = enum(u5) {
    a = @intCast(c.GBA_KEY_A),
    b = @intCast(c.GBA_KEY_B),
    select = @intCast(c.GBA_KEY_SELECT),
    start = @intCast(c.GBA_KEY_START),
    right = @intCast(c.GBA_KEY_RIGHT),
    left = @intCast(c.GBA_KEY_LEFT),
    up = @intCast(c.GBA_KEY_UP),
    down = @intCast(c.GBA_KEY_DOWN),
    r = @intCast(c.GBA_KEY_R),
    l = @intCast(c.GBA_KEY_L),
};

pub const Core = struct {
    handle: *c.struct_mCore,

    pub fn init(platform: Platform) !Core {
        const handle_c = c.mCoreCreate(@intFromEnum(platform)) orelse return error.CoreCreationFailed;
        const handle: *c.struct_mCore = @ptrCast(handle_c);
        c.mCoreConfigInit(&handle.config, "zig");
        if (handle.init) |init_fn| {
            if (!init_fn(handle)) {
                return error.CoreInitFailed;
            }
        }
        return Core{ .handle = handle };
    }

    pub fn deinit(self: *Core) void {
        if (self.handle.deinit) |deinit_fn| {
            deinit_fn(self.handle);
        }
    }

    pub fn loadRom(self: *Core, path: []const u8) !void {
        const vf = c.VFileOpen(path.ptr, c.O_RDONLY) orelse return error.RomOpenFailed;
        if (self.handle.loadROM) |load_fn| {
            if (!load_fn(self.handle, vf)) {
                return error.RomLoadFailed;
            }
        }
    }

    pub fn reset(self: *Core) void {
        if (self.handle.reset) |reset_fn| {
            reset_fn(self.handle);
        }
    }

    pub fn runFrame(self: *Core) void {
        if (self.handle.runFrame) |run_fn| {
            run_fn(self.handle);
        }
    }

    pub fn getDesiredVideoSize(self: *Core) struct { width: u32, height: u32 } {
        var w: u32 = 0;
        var h: u32 = 0;
        if (self.handle.baseVideoSize) |size_fn| {
            size_fn(self.handle, &w, &h);
        }
        return .{ .width = w, .height = h };
    }

    pub fn setVideoBuffer(self: *Core, buffer: [*]u32, stride: usize) void {
        if (self.handle.setVideoBuffer) |set_fn| {
            set_fn(self.handle, @ptrCast(buffer), stride);
        }
    }

    pub fn setKeys(self: *Core, keys: u32) void {
        if (self.handle.setKeys) |set_fn| {
            set_fn(self.handle, keys);
        }
    }

    pub fn reloadConfigOption(self: *Core, option: [:0]const u8) void {
        if (self.handle.reloadConfigOption) |reload_fn| {
            reload_fn(self.handle, option.ptr, null);
        }
    }

    pub fn getAudioBuffer(self: *Core) ?*c.struct_mAudioBuffer {
        if (self.handle.getAudioBuffer) |get_fn| {
            return get_fn(self.handle);
        }
        return null;
    }
};
