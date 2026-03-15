const std = @import("std");
const builtin = @import("builtin");
const assert = @import("../quirks.zig").inlineAssert;
const Allocator = std.mem.Allocator;
const internal_os = @import("../os/main.zig");

const log = std.log.scoped(.config);

/// Default path for the XDG home configuration file. Returned value
/// must be freed by the caller.
pub fn defaultXdgPath(alloc: Allocator) ![]const u8 {
    return try internal_os.xdg.config(
        alloc,
        .{ .subdir = "ghostty/config.ghostty" },
    );
}

/// Ghostty <1.3.0 default path for the XDG home configuration file.
/// Returned value must be freed by the caller.
pub fn legacyDefaultXdgPath(alloc: Allocator) ![]const u8 {
    return try internal_os.xdg.config(
        alloc,
        .{ .subdir = "ghostty/config" },
    );
}

/// Preferred default path for the XDG home configuration file.
/// Returned value must be freed by the caller.
pub fn preferredXdgPath(alloc: Allocator) ![]const u8 {
    // If the XDG path exists, use that.
    const xdg_path = try defaultXdgPath(alloc);
    if (open(xdg_path)) |f| {
        f.close();
        return xdg_path;
    } else |_| {}

    // Try the legacy path
    errdefer alloc.free(xdg_path);
    const legacy_xdg_path = try legacyDefaultXdgPath(alloc);
    if (open(legacy_xdg_path)) |f| {
        f.close();
        alloc.free(xdg_path);
        return legacy_xdg_path;
    } else |_| {}

    // Legacy path and XDG path both don't exist. Return the
    // new one.
    alloc.free(legacy_xdg_path);
    return xdg_path;
}

/// Default path for the macOS Application Support configuration file.
/// Returned value must be freed by the caller.
pub fn defaultAppSupportPath(alloc: Allocator) ![]const u8 {
    return try internal_os.macos.appSupportDir(alloc, "config.ghostty");
}

/// Ghostty <1.3.0 default path for the macOS Application Support
/// configuration file. Returned value must be freed by the caller.
pub fn legacyDefaultAppSupportPath(alloc: Allocator) ![]const u8 {
    return try internal_os.macos.appSupportDir(alloc, "config");
}

/// Preferred default path for the macOS Application Support configuration file.
/// Returned value must be freed by the caller.
pub fn preferredAppSupportPath(alloc: Allocator) ![]const u8 {
    // If the app support path exists, use that.
    const app_support_path = try defaultAppSupportPath(alloc);
    if (open(app_support_path)) |f| {
        f.close();
        return app_support_path;
    } else |_| {}

    // Try the legacy path
    errdefer alloc.free(app_support_path);
    const legacy_app_support_path = try legacyDefaultAppSupportPath(alloc);
    if (open(legacy_app_support_path)) |f| {
        f.close();
        alloc.free(app_support_path);
        return legacy_app_support_path;
    } else |_| {}

    // Legacy path and app support path both don't exist. Return the
    // new one.
    alloc.free(legacy_app_support_path);
    return app_support_path;
}

/// Default path for the Windows %APPDATA% (Roaming) configuration file.
/// This is `%APPDATA%\ghostty\config.ghostty`, e.g.
/// `C:\Users\<user>\AppData\Roaming\ghostty\config.ghostty`.
/// Returned value must be freed by the caller.
pub fn defaultWindowsAppDataPath(alloc: Allocator) ![]const u8 {
    const appdata = std.process.getEnvVarOwned(alloc, "APPDATA") catch
        return error.AppDataNotSet;
    defer alloc.free(appdata);
    return std.fs.path.join(alloc, &.{ appdata, "ghostty", "config.ghostty" });
}

/// Returns the path to the preferred default configuration file.
/// This is the file where users should place their configuration.
///
/// This doesn't create or populate the file with any default
/// contents; downstream callers must handle this.
///
/// The returned value must be freed by the caller.
pub fn preferredDefaultFilePath(alloc: Allocator) ![]const u8 {
    switch (builtin.os.tag) {
        .macos => {
            // macOS prefers the Application Support directory
            // if it exists.
            const app_support_path = try preferredAppSupportPath(alloc);
            const app_support_file = open(app_support_path) catch {
                // Try the XDG path if it exists
                const xdg_path = try preferredXdgPath(alloc);
                const xdg_file = open(xdg_path) catch {
                    // If neither file exists, use app support
                    alloc.free(xdg_path);
                    return app_support_path;
                };
                xdg_file.close();
                alloc.free(app_support_path);
                return xdg_path;
            };
            app_support_file.close();
            return app_support_path;
        },

        .windows => {
            // On Windows prefer %APPDATA%\ghostty\config.ghostty (Roaming).
            // If that file exists, use it. Otherwise fall back to the XDG
            // path (%LOCALAPPDATA%\ghostty\...) for users who set XDG_CONFIG_HOME.
            const appdata_path = try defaultWindowsAppDataPath(alloc);
            if (open(appdata_path)) |f| {
                f.close();
                return appdata_path;
            } else |_| {}
            errdefer alloc.free(appdata_path);
            const xdg_path = try preferredXdgPath(alloc);
            if (open(xdg_path)) |f| {
                f.close();
                alloc.free(appdata_path);
                return xdg_path;
            } else |_| {}
            // Neither exists — return the conventional Roaming path so new
            // users get their template written there.
            alloc.free(xdg_path);
            return appdata_path;
        },

        // All other platforms use XDG only
        else => return try preferredXdgPath(alloc),
    }
}

const OpenFileError = error{
    FileNotFound,
    FileIsEmpty,
    FileOpenFailed,
    NotAFile,
};

/// Opens the file at the given path and returns the file handle
/// if it exists and is non-empty. This also constrains the possible
/// errors to a smaller set that we can explicitly handle.
pub fn open(path: []const u8) OpenFileError!std.fs.File {
    assert(std.fs.path.isAbsolute(path));

    var file = std.fs.openFileAbsolute(
        path,
        .{},
    ) catch |err| switch (err) {
        error.FileNotFound => return OpenFileError.FileNotFound,
        else => {
            log.warn("unexpected file open error path={s} err={}", .{
                path,
                err,
            });
            return OpenFileError.FileOpenFailed;
        },
    };
    errdefer file.close();

    const stat = file.stat() catch |err| {
        log.warn("error getting file stat path={s} err={}", .{
            path,
            err,
        });
        return OpenFileError.FileOpenFailed;
    };
    switch (stat.kind) {
        .file => {},
        else => return OpenFileError.NotAFile,
    }

    if (stat.size == 0) return OpenFileError.FileIsEmpty;

    return file;
}
