const std = @import("std");

/// Runtime is the runtime to use for Ghostty. All runtimes do not provide
/// equivalent feature sets.
pub const Runtime = enum {
    /// Will not produce an executable at all when `zig build` is called.
    /// This is only useful if you're only interested in the lib only (macOS).
    none,

    /// GTK4. Rich windowed application. This uses a full GObject-based
    /// approach to building the application.
    gtk,

    /// Windows Win32 host application. Like macOS (which uses Swift/Xcode),
    /// the Windows GUI is a separate C++ host that embeds libghostty through
    /// its C API. The Zig build produces libghostty.dll; the host application
    /// lives in the windows/ directory.
    ///
    /// Setting this runtime does not currently produce an executable from Zig;
    /// it signals that the build should emit libghostty for Windows linking.
    windows,

    pub fn default(target: std.Target) Runtime {
        return switch (target.os.tag) {
            // The Linux and FreeBSD default is GTK because it is a full
            // featured application.
            .linux, .freebsd => .gtk,
            // Windows uses a separate C++ host app (similar to macOS/Swift).
            // The Zig build produces libghostty.dll for the host to link against.
            .windows => .none,
            // Otherwise, we do NONE so we don't create an exe and we create
            // libghostty. On macOS, Xcode is used to build the app that links
            // to libghostty.
            else => .none,
        };
    }
};

test {
    _ = Runtime;
}
