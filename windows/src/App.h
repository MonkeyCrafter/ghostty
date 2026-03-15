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
    void PerformAction(ghostty_target_s target, ghostty_action_s action);

    /// Called by libghostty when a new surface (terminal pane) should be created.
    ghostty_surface_t CreateSurface(const ghostty_surface_config_s* config);

    /// Called by libghostty when a surface should be destroyed.
    void DestroySurface(ghostty_surface_t surface);

    /// Get the underlying ghostty app handle.
    ghostty_app_t GhosttyApp() const { return m_app; }

    HINSTANCE HInstance() const { return m_hInstance; }

    // ── libghostty runtime callbacks (static trampolines) ──────────────────

    static void OnWakeup(void* userdata);
    static void OnAction(ghostty_app_t app,
                         ghostty_target_s target,
                         ghostty_action_s action,
                         void* userdata);

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
