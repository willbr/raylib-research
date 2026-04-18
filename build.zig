const std = @import("std");

const projects = .{ "fps", "rally", "3rd-person", "rts", "soccer", "kart", "platformer", "zelda", "micromachines", "skate", "resi", "wipeout", "editor", "snowboard", "rpgbattle", "biplane", "goldensun", "starfox", "boat", "pokemon", "fighter", "util-tests" };

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{ .preferred_optimize_mode = .ReleaseFast });

    const build_all_step = b.step("all", "Build all projects");

    inline for (projects) |name| {
        const mod = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        });

        mod.addCSourceFile(.{
            .file = b.path(name ++ "/main.c"),
            .flags = &.{"-std=c99"},
        });

        mod.linkSystemLibrary("raylib", .{});

        // Platform-specific frameworks/libraries
        if (target.result.os.tag == .macos) {
            mod.linkFramework("OpenGL", .{});
            mod.linkFramework("Cocoa", .{});
            mod.linkFramework("IOKit", .{});
            mod.linkFramework("CoreVideo", .{});
        } else if (target.result.os.tag == .windows) {
            const vcpkg_root = "C:/Users/wjbr/scoop/apps/vcpkg/current/installed/x64-windows";
            mod.addIncludePath(.{ .cwd_relative = vcpkg_root ++ "/include" });
            mod.addLibraryPath(.{ .cwd_relative = vcpkg_root ++ "/lib" });
            mod.linkSystemLibrary("gdi32", .{});
            mod.linkSystemLibrary("winmm", .{});
            mod.linkSystemLibrary("user32", .{});
            mod.linkSystemLibrary("shell32", .{});
        }

        const exe = b.addExecutable(.{
            .name = name,
            .root_module = mod,
        });

        const install = b.addInstallArtifact(exe, .{});
        build_all_step.dependOn(&install.step);

        // Per-project build step: zig build fps, zig build rally, etc.
        const build_step = b.step(name, "Build " ++ name);
        build_step.dependOn(&install.step);

        // Per-project run step: zig build run-fps, zig build run-rally, etc.
        const run_cmd = b.addRunArtifact(exe);
        run_cmd.step.dependOn(b.getInstallStep());
        if (target.result.os.tag == .windows) {
            run_cmd.addPathDir("C:/Users/wjbr/scoop/apps/vcpkg/current/installed/x64-windows/bin");
        }
        if (b.args) |args| run_cmd.addArgs(args);

        const run_step = b.step("run-" ++ name, "Run " ++ name);
        run_step.dependOn(&run_cmd.step);
    }

    b.default_step = build_all_step;
}
