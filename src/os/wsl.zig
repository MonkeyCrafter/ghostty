/// WSL (Windows Subsystem for Linux) detection and utilities.
///
/// This module provides utilities for detecting WSL installations and
/// translating between Windows and WSL paths. It is only compiled on Windows.
const std = @import("std");
const builtin = @import("builtin");
const Allocator = std.mem.Allocator;

const log = std.log.scoped(.wsl);

/// A WSL distribution entry.
pub const Distribution = struct {
    /// The distribution name (e.g. "Ubuntu-22.04").
    name: []const u8,

    /// Whether this is the default distribution.
    default: bool,
};

/// Returns true if WSL is available on this system by checking for wsl.exe
/// in %SystemRoot%\System32\.
pub fn isWslAvailable() bool {
    if (comptime builtin.os.tag != .windows) return false;

    const sysroot = std.process.getEnvVarOwned(
        std.heap.page_allocator,
        "SYSTEMROOT",
    ) catch return false;
    defer std.heap.page_allocator.free(sysroot);

    var path_buf: [std.fs.max_path_bytes]u8 = undefined;
    const wsl_path = std.fmt.bufPrint(
        &path_buf,
        "{s}\\System32\\wsl.exe",
        .{sysroot},
    ) catch return false;

    std.fs.accessAbsolute(wsl_path, .{}) catch return false;
    return true;
}

/// Translate a Windows absolute path (e.g. "C:\Users\foo\bar") to the
/// corresponding WSL path (e.g. "/mnt/c/Users/foo/bar").
///
/// The caller owns the returned slice and must free it with the provided
/// allocator.
pub fn windowsPathToWsl(alloc: Allocator, path: []const u8) ![]const u8 {
    // Windows paths beginning with a drive letter like "C:\" are mounted
    // inside WSL at /mnt/<lowercase-drive-letter>/.
    //
    // Network paths (\\server\share) are not translated — return them as-is.
    if (path.len >= 3 and
        std.ascii.isAlphabetic(path[0]) and
        path[1] == ':' and
        (path[2] == '\\' or path[2] == '/'))
    {
        const drive_letter = std.ascii.toLower(path[0]);
        const rest = path[3..]; // strip "C:\"

        // Translate remaining backslashes to forward slashes.
        // Use defer (not errdefer) so the intermediate buffer is always freed;
        // allocPrint below copies the data into the final allocation.
        const translated = try alloc.alloc(u8, rest.len);
        defer alloc.free(translated);
        for (rest, 0..) |ch, i| {
            translated[i] = if (ch == '\\') '/' else ch;
        }

        return std.fmt.allocPrint(
            alloc,
            "/mnt/{c}/{s}",
            .{ drive_letter, translated },
        );
    }

    // WSL paths (//wsl$/DistroName/...) are already in URI-like form.
    // Return a normalized forward-slash version.
    if (std.mem.startsWith(u8, path, "\\\\wsl$\\") or
        std.mem.startsWith(u8, path, "//wsl$/"))
    {
        // Strip the "\\wsl$\" prefix and convert backslashes.
        const prefix_len: usize = if (path[0] == '\\') "\\\\wsl$\\".len else "//wsl$/".len;
        const rest = path[prefix_len..];

        const translated = try alloc.alloc(u8, rest.len + 1);
        errdefer alloc.free(translated);
        translated[0] = '/';
        for (rest, 1..) |ch, i| {
            translated[i] = if (ch == '\\') '/' else ch;
        }
        return translated;
    }

    // Unknown format — return a copy unchanged.
    return alloc.dupe(u8, path);
}

/// List installed WSL distributions by running `wsl.exe --list --quiet`.
/// The caller owns the returned slice and each Distribution.name string;
/// free them all with the provided allocator.
pub fn wslDistributions(alloc: Allocator) ![]Distribution {
    if (comptime builtin.os.tag != .windows) return &.{};
    if (!isWslAvailable()) return &.{};

    // Run: wsl.exe --list --verbose
    // Output format (tab-separated):
    //   * Ubuntu-22.04  Running  2
    //     Debian        Stopped  2
    // The "*" prefix marks the default distribution.
    var child = std.process.Child.init(
        &.{ "wsl.exe", "--list", "--verbose" },
        alloc,
    );
    child.stdout_behavior = .Pipe;
    child.stderr_behavior = .Ignore;
    try child.spawn();

    var stdout = std.ArrayList(u8).init(alloc);
    defer stdout.deinit();

    try child.stdout.?.reader().readAllArrayList(&stdout, 64 * 1024);
    _ = try child.wait();

    var distributions = std.ArrayList(Distribution).init(alloc);
    errdefer {
        for (distributions.items) |d| alloc.free(d.name);
        distributions.deinit();
    }

    // Skip the header line ("  NAME  STATE  VERSION").
    var lines = std.mem.splitScalar(u8, stdout.items, '\n');
    _ = lines.next(); // header

    while (lines.next()) |line| {
        const trimmed = std.mem.trim(u8, line, " \t\r");
        if (trimmed.len == 0) continue;

        const is_default = trimmed[0] == '*';
        const without_star = if (is_default) trimmed[1..] else trimmed;
        const name_end = std.mem.indexOfAny(u8, without_star, " \t") orelse without_star.len;
        const name = std.mem.trim(u8, without_star[0..name_end], " \t");
        if (name.len == 0) continue;

        try distributions.append(.{
            .name = try alloc.dupe(u8, name),
            .default = is_default,
        });
    }

    return distributions.toOwnedSlice();
}

test "windowsPathToWsl: drive letter" {
    const testing = std.testing;
    const alloc = testing.allocator;

    const result = try windowsPathToWsl(alloc, "C:\\Users\\foo\\bar");
    defer alloc.free(result);
    try testing.expectEqualStrings("/mnt/c/Users/foo/bar", result);
}

test "windowsPathToWsl: lowercase drive" {
    const testing = std.testing;
    const alloc = testing.allocator;

    const result = try windowsPathToWsl(alloc, "d:\\projects\\ghostty");
    defer alloc.free(result);
    try testing.expectEqualStrings("/mnt/d/projects/ghostty", result);
}

test "windowsPathToWsl: wsl$ path" {
    const testing = std.testing;
    const alloc = testing.allocator;

    const result = try windowsPathToWsl(alloc, "\\\\wsl$\\Ubuntu\\home\\user");
    defer alloc.free(result);
    try testing.expectEqualStrings("/Ubuntu/home/user", result);
}

test "windowsPathToWsl: unknown path passthrough" {
    const testing = std.testing;
    const alloc = testing.allocator;

    const result = try windowsPathToWsl(alloc, "/already/unix/path");
    defer alloc.free(result);
    try testing.expectEqualStrings("/already/unix/path", result);
}
