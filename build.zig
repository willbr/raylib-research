const std = @import("std");

const projects = .{ "fps", "rally", "3rd-person", "rts", "soccer", "kart", "platformer", "zelda", "micromachines", "skate", "resi", "wipeout", "editor", "snowboard", "rpgbattle" };

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const build_all_step = b.step("all", "Build all projects");

    inline for (projects) |name| {
        const exe = b.addExecutable(.{
            .name = name,
            .target = target,
            .optimize = optimize,
        });

        exe.addCSourceFile(.{
            .file = b.path(name ++ "/main.c"),
            .flags = &.{"-std=c99"},
        });

        exe.linkSystemLibrary("raylib");
        exe.linkLibC();

        // Platform-specific frameworks/libraries
        if (exe.rootModuleTarget().os.tag == .macos) {
            exe.linkFramework("OpenGL");
            exe.linkFramework("Cocoa");
            exe.linkFramework("IOKit");
            exe.linkFramework("CoreVideo");
        } else if (exe.rootModuleTarget().os.tag == .windows) {
            exe.linkSystemLibrary("gdi32");
            exe.linkSystemLibrary("winmm");
            exe.linkSystemLibrary("user32");
            exe.linkSystemLibrary("shell32");
        }

        const install = b.addInstallArtifact(exe, .{});
        build_all_step.dependOn(&install.step);

        // Per-project build step: zig build fps, zig build rally, etc.
        const build_step = b.step(name, "Build " ++ name);
        build_step.dependOn(&install.step);

        // Per-project run step: zig build run-fps, zig build run-rally, etc.
        const run_cmd = b.addRunArtifact(exe);
        run_cmd.step.dependOn(b.getInstallStep());
        if (b.args) |args| run_cmd.addArgs(args);

        const run_step = b.step("run-" ++ name, "Run " ++ name);
        run_step.dependOn(&run_cmd.step);
    }

    b.default_step = build_all_step;
}
