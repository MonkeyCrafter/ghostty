#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <memory>

#include "ghostty.h"

class App;
class WGLContext;
class Surface;

/// Window wraps a Win32 HWND and the libghostty surface (terminal pane).
/// Each top-level Ghostty window has exactly one Surface for now; split panes
/// will be added later.
class Window {
public:
    explicit Window(App* app);
    ~Window();

    /// Create and show the window. Returns false on failure.
    bool Init(int nCmdShow);

    /// Returns true if this window contains the given ghostty surface.
    bool ContainsSurface(ghostty_surface_t surface) const;

    HWND Hwnd() const { return m_hwnd; }

    static const wchar_t* ClassName() { return L"GhosttyWindow"; }

    /// Register the Win32 window class (call once at startup).
    static bool RegisterWindowClass(HINSTANCE hInstance);

private:
    App*            m_app;
    HWND            m_hwnd = nullptr;
    std::unique_ptr<WGLContext> m_glContext;
    std::unique_ptr<Surface>    m_surface;

    // ── Win32 message handlers ──────────────────────────────────────────────
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void    OnCreate();
    void    OnDestroy();
    void    OnSize(int width, int height);
    void    OnPaint();
    void    OnKeyDown(WPARAM vkey, LPARAM lParam);
    void    OnKeyUp(WPARAM vkey, LPARAM lParam);
    void    OnChar(wchar_t ch);
    void    OnMouseMove(int x, int y, DWORD keyState);
    void    OnMouseButton(UINT msg, int x, int y, DWORD keyState);
    void    OnMouseWheel(int delta, int x, int y, DWORD keyState);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);
};
