const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const libmgba = b.addLibrary(.{
        .name = "mgba",
        .root_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
        }),
    });

    libmgba.addIncludePath(b.path("../include"));
    libmgba.addIncludePath(b.path("../src"));
    libmgba.addIncludePath(b.path("."));

    const mgba_defines = [_][2][]const u8{
        .{ "ENABLE_VFS", "1" },
        .{ "ENABLE_VFS_FD", "1" },
        .{ "ENABLE_DIRECTORIES", "1" },
        .{ "M_CORE_GBA", "1" },
        .{ "M_CORE_GB", "1" },
        .{ "BUILD_STATIC", "1" },
    };

    const c_flags = [_][]const u8{
        "-std=gnu11",
        "-fno-sanitize=undefined",
        "-Wno-implicit-function-declaration",
    };

    for (mgba_defines) |define| {
        libmgba.root_module.addCMacro(define[0], define[1]);
    }

    if (target.result.os.tag == .macos) {
        libmgba.root_module.addCMacro("HAVE_STRNDUP", "1");
        libmgba.root_module.addCMacro("HAVE_STRDUP", "1");
        libmgba.root_module.addCMacro("HAVE_STRLCPY", "1");
        libmgba.root_module.addCMacro("HAVE_XLOCALE", "1");
        libmgba.root_module.addCMacro("HAVE_LOCALTIME_R", "1");
    }

    const core_src = [_][]const u8{
        "../src/core/bitmap-cache.c",
        "../src/core/cache-set.c",
        "../src/core/cheats.c",
        "../src/core/config.c",
        "../src/core/core.c",
        "../src/core/directories.c",
        "../src/core/input.c",
        "../src/core/interface.c",
        "../src/core/library.c",
        "../src/core/lockstep.c",
        "../src/core/log.c",
        "../src/core/map-cache.c",
        "../src/core/mem-search.c",
        "../src/core/rewind.c",
        "../src/core/serialize.c",
        "../src/core/sync.c",
        "../src/core/thread.c",
        "../src/core/tile-cache.c",
        "../src/core/timing.c",
    };

    const gba_src = [_][]const u8{
        "../src/gba/audio.c",
        "../src/gba/bios.c",
        "../src/gba/cart/ereader.c",
        "../src/gba/cart/gpio.c",
        "../src/gba/cart/matrix.c",
        "../src/gba/cart/unlicensed.c",
        "../src/gba/cart/vfame.c",
        "../src/gba/cheats.c",
        "../src/gba/cheats/codebreaker.c",
        "../src/gba/cheats/gameshark.c",
        "../src/gba/cheats/parv3.c",
        "../src/gba/core.c",
        "../src/gba/dma.c",
        "../src/gba/gba.c",
        "../src/gba/hle-bios.c",
        "../src/gba/input.c",
        "../src/gba/io.c",
        "../src/gba/memory.c",
        "../src/gba/overrides.c",
        "../src/gba/renderers/cache-set.c",
        "../src/gba/renderers/common.c",
        "../src/gba/renderers/software-bg.c",
        "../src/gba/renderers/software-mode0.c",
        "../src/gba/renderers/software-obj.c",
        "../src/gba/renderers/video-software.c",
        "../src/gba/savedata.c",
        "../src/gba/serialize.c",
        "../src/gba/sharkport.c",
        "../src/gba/sio.c",
        "../src/gba/sio/gbp.c",
        "../src/gba/timer.c",
        "../src/gba/video.c",
    };

    const gb_src = [_][]const u8{
        "../src/gb/audio.c",
        "../src/gb/cheats.c",
        "../src/gb/core.c",
        "../src/gb/gb.c",
        "../src/gb/input.c",
        "../src/gb/io.c",
        "../src/gb/mbc.c",
        "../src/gb/mbc/huc-3.c",
        "../src/gb/mbc/licensed.c",
        "../src/gb/mbc/mbc.c",
        "../src/gb/mbc/pocket-cam.c",
        "../src/gb/mbc/tama5.c",
        "../src/gb/mbc/unlicensed.c",
        "../src/gb/memory.c",
        "../src/gb/overrides.c",
        "../src/gb/serialize.c",
        "../src/gb/renderers/cache-set.c",
        "../src/gb/renderers/software.c",
        "../src/gb/sio.c",
        "../src/gb/timer.c",
        "../src/gb/video.c",
    };

    const arm_src = [_][]const u8{
        "../src/arm/arm.c",
        "../src/arm/decoder-arm.c",
        "../src/arm/decoder.c",
        "../src/arm/decoder-thumb.c",
        "../src/arm/isa-arm.c",
        "../src/arm/isa-thumb.c",
    };

    const sm83_src = [_][]const u8{
        "../src/sm83/decoder.c",
        "../src/sm83/isa-sm83.c",
        "../src/sm83/sm83.c",
    };

    const util_src = [_][]const u8{
        "../src/util/audio-buffer.c",
        "../src/util/audio-resampler.c",
        "../src/util/circle-buffer.c",
        "../src/util/configuration.c",
        "../src/util/convolve.c",
        "../src/util/crc32.c",
        "../src/util/elf-read.c",
        "../src/util/formatting.c",
        "../src/util/gbk-table.c",
        "../src/util/geometry.c",
        "../src/util/hash.c",
        "../src/util/image.c",
        "../src/util/image/export.c",
        "../src/util/image/font.c",
        "../src/util/interpolator.c",
        "../src/util/md5.c",
        "../src/util/patch.c",
        "../src/util/patch-fast.c",
        "../src/util/patch-ips.c",
        "../src/util/patch-ups.c",
        "../src/util/ring-fifo.c",
        "../src/util/sfo.c",
        "../src/util/sha1.c",
        "../src/util/string.c",
        "../src/util/table.c",
        "../src/util/text-codec.c",
        "../src/util/vector.c",
        "../src/util/vfs.c",
        "../src/util/vfs/vfs-mem.c",
        "../src/util/vfs/vfs-fifo.c",
        "../src/util/vfs/vfs-fd.c",
        "../src/util/vfs/vfs-dirent.c",
    };

    libmgba.addCSourceFiles(.{ .files = &core_src, .flags = &c_flags });
    libmgba.addCSourceFiles(.{ .files = &gba_src, .flags = &c_flags });
    libmgba.addCSourceFiles(.{ .files = &gb_src, .flags = &c_flags });
    libmgba.addCSourceFiles(.{ .files = &arm_src, .flags = &c_flags });
    libmgba.addCSourceFiles(.{ .files = &sm83_src, .flags = &c_flags });
    libmgba.addCSourceFiles(.{ .files = &util_src, .flags = &c_flags });
    libmgba.addCSourceFile(.{ .file = b.path("../src/platform/posix/memory.c"), .flags = &c_flags });
    libmgba.addCSourceFile(.{ .file = b.path("../src/third-party/inih/ini.c"), .flags = &c_flags });
    libmgba.addCSourceFile(.{ .file = b.path("../src/feature/video-logger.c"), .flags = &c_flags });
    libmgba.addCSourceFile(.{ .file = b.path("../src/gba/extra/proxy.c"), .flags = &c_flags });
    libmgba.addCSourceFile(.{ .file = b.path("../src/gb/extra/proxy.c"), .flags = &c_flags });
    libmgba.addCSourceFile(.{ .file = b.path("version.c"), .flags = &c_flags });

    libmgba.linkLibC();

    const exe = b.addExecutable(.{
        .name = "mgba-zig",
        .root_module = b.createModule(.{
            .root_source_file = b.path("main.zig"),
            .target = target,
            .optimize = optimize,
        }),
    });

    // SDL2
    exe.linkSystemLibrary("sdl2");
    exe.addIncludePath(b.path("../include"));
    exe.addIncludePath(b.path("../src"));

    if (target.result.os.tag == .macos) {
        exe.linkFramework("CoreFoundation");
    }

    // Link libmgba
    exe.linkLibrary(libmgba);

    b.installArtifact(exe);

    const run_cmd = b.addRunArtifact(exe);
    run_cmd.step.dependOn(b.getInstallStep());
    if (b.args) |args| {
        run_cmd.addArgs(args);
    }

    const run_step = b.step("run", "Run the app");
    run_step.dependOn(&run_cmd.step);
}
