#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>
#include <memory>

#include "ghostty.h"

class Window;

/// App wraps the ghostty_app_s lifecycle and the Win32 message loop.
/// It implements the ghostty_runtime_config_s callbacks required by libghostty.
class App {
public:
    App(HINSTANCE hInstance, int nCmdShow);
    ~App();

    /// Initialize the libghostty app and create the initial window.
    /// Returns false on failure.
    bool Init();

    /// Run the Win32 message loop. Returns the exit code.
    int Run();

    /// Wakeup the event loop from another thread (used by libghostty).
    void Wakeup();

    /// Perform an action dispatched by libghostty (e.g. new_window, close, etc.)
    /// Returns true if the action was handled.
    bool PerformAction(ghostty_target_s target, ghostty_action_s action);

    /// Get the underlying ghostty app handle.
    ghostty_app_t GhosttyApp() const { return m_app; }

    HINSTANCE HInstance() const { return m_hInstance; }

    // ── libghostty runtime callbacks (static trampolines) ──────────────────
    // These functions are registered in ghostty_runtime_config_s.
    // Clipboard/close callbacks receive the *surface* userdata (Surface*).
    // Wakeup receives the *app* userdata (App*).
    // Action receives ghostty_app_t (use ghostty_app_userdata() to get App*).

    static void OnWakeup(void* userdata);

    /// ghostty_runtime_action_cb: (ghostty_app_t, ghostty_target_s, ghostty_action_s) -> bool
    static bool OnAction(ghostty_app_t app,
                         ghostty_target_s target,
                         ghostty_action_s action);

    /// ghostty_runtime_read_clipboard_cb: (surface_ud, clipboard_e, request) -> bool
    static bool OnReadClipboard(void* userdata,
                                ghostty_clipboard_e type,
                                void* request);

    /// ghostty_runtime_confirm_read_clipboard_cb: (surface_ud, text, request, kind)
    static void OnConfirmReadClipboard(void* userdata,
                                       const char* text,
                                       void* request,
                                       ghostty_clipboard_request_e kind);

    /// ghostty_runtime_write_clipboard_cb: (surface_ud, clipboard_e, content*, count, confirm)
    static void OnWriteClipboard(void* userdata,
                                 ghostty_clipboard_e type,
                                 const ghostty_clipboard_content_s* content,
                                 size_t count,
                                 bool confirm);

    /// ghostty_runtime_close_surface_cb: (surface_ud, process_alive)
    static void OnCloseSurface(void* userdata, bool process_alive);

private:
    HINSTANCE m_hInstance;
    int m_nCmdShow;
    ghostty_app_t m_app = nullptr;
    std::vector<std::unique_ptr<Window>> m_windows;

    /// Create the initial terminal window.
    bool CreateInitialWindow();

    /// Find and remove a window from m_windows.
    void RemoveWindow(Window* window);

    friend class Window;
};
