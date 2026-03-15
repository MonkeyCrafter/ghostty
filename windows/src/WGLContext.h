#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

/// WGLContext creates and manages an OpenGL 4.3 rendering context for a
/// Win32 HWND. libghostty's OpenGL renderer uses this context to draw
/// the terminal. The host app makes the context current before calling
/// ghostty_surface_draw().
class WGLContext {
public:
    WGLContext();
    ~WGLContext();

    /// Initialize WGL: create pixel format, request OpenGL 4.3 core context.
    /// Returns false on failure; check GetLastError() for details.
    bool Init(HWND hwnd);

    /// Make this context current on the calling thread.
    bool MakeCurrent();

    /// Release the current context (make no context current on this thread).
    void ReleaseCurrent();

    /// Swap the front and back buffers to display the rendered frame.
    bool SwapBuffers();

    HGLRC Context() const { return m_hglrc; }
    HDC   DeviceContext() const { return m_hdc; }

private:
    HWND  m_hwnd  = nullptr;
    HDC   m_hdc   = nullptr;
    HGLRC m_hglrc = nullptr;

    /// Load WGL extension function pointers needed for core profile creation.
    bool LoadWGLExtensions(HDC hdc);

    // WGL extension function pointer typedefs
    using PFNWGLCREATECONTEXTATTRIBSARBPROC = HGLRC(WINAPI*)(
        HDC hDC, HGLRC hShareContext, const int* attribList);
    using PFNWGLCHOOSEPIXELFORMATARBPROC = BOOL(WINAPI*)(
        HDC hdc, const int* piAttribIList, const FLOAT* pfAttribFList,
        UINT nMaxFormats, int* piFormats, UINT* nNumFormats);

    PFNWGLCREATECONTEXTATTRIBSARBPROC m_wglCreateContextAttribsARB = nullptr;
    PFNWGLCHOOSEPIXELFORMATARBPROC    m_wglChoosePixelFormatARB    = nullptr;
};
