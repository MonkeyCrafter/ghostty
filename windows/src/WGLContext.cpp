#include "WGLContext.h"
#include <cstdio>

// WGL attribute constants for wglCreateContextAttribsARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB       0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB       0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB        0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB    0x0001
#define WGL_CONTEXT_FLAGS_ARB               0x2094
#define WGL_CONTEXT_DEBUG_BIT_ARB           0x0001

// WGL pixel format attribute constants for wglChoosePixelFormatARB
#define WGL_DRAW_TO_WINDOW_ARB              0x2001
#define WGL_SUPPORT_OPENGL_ARB              0x2010
#define WGL_DOUBLE_BUFFER_ARB               0x2011
#define WGL_PIXEL_TYPE_ARB                  0x2013
#define WGL_TYPE_RGBA_ARB                   0x202B
#define WGL_COLOR_BITS_ARB                  0x2014
#define WGL_ALPHA_BITS_ARB                  0x201B
#define WGL_DEPTH_BITS_ARB                  0x2022
#define WGL_STENCIL_BITS_ARB                0x2023
#define WGL_SAMPLE_BUFFERS_ARB              0x2041
#define WGL_SAMPLES_ARB                     0x2042
#define WGL_ACCELERATION_ARB                0x2003
#define WGL_FULL_ACCELERATION_ARB           0x2027

WGLContext::WGLContext() = default;

WGLContext::~WGLContext()
{
    ReleaseCurrent();
    if (m_hglrc) {
        wglDeleteContext(m_hglrc);
        m_hglrc = nullptr;
    }
    if (m_hdc && m_hwnd) {
        ReleaseDC(m_hwnd, m_hdc);
        m_hdc = nullptr;
    }
}

bool WGLContext::Init(HWND hwnd)
{
    m_hwnd = hwnd;
    m_hdc = GetDC(hwnd);
    if (!m_hdc) return false;

    // Step 1: Create a dummy window/context to load WGL extensions.
    // This is required because wglChoosePixelFormatARB and
    // wglCreateContextAttribsARB are extensions that need a valid context.
    WNDCLASSEXW dummyClass = {};
    dummyClass.cbSize = sizeof(dummyClass);
    dummyClass.lpfnWndProc = DefWindowProcW;
    dummyClass.hInstance = GetModuleHandleW(nullptr);
    dummyClass.lpszClassName = L"GhosttyWGLDummy";
    RegisterClassExW(&dummyClass);

    HWND dummyHwnd = CreateWindowExW(0, L"GhosttyWGLDummy", L"",
        WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!dummyHwnd) return false;

    HDC dummyDC = GetDC(dummyHwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int dummyFormat = ChoosePixelFormat(dummyDC, &pfd);
    SetPixelFormat(dummyDC, dummyFormat, &pfd);
    HGLRC dummyRC = wglCreateContext(dummyDC);
    wglMakeCurrent(dummyDC, dummyRC);

    bool ok = LoadWGLExtensions(dummyDC);

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(dummyRC);
    ReleaseDC(dummyHwnd, dummyDC);
    DestroyWindow(dummyHwnd);
    UnregisterClassW(L"GhosttyWGLDummy", GetModuleHandleW(nullptr));

    if (!ok) return false;

    // Step 2: Choose a pixel format for the real window using the ARB extension.
    const int pixelAttribs[] = {
        WGL_DRAW_TO_WINDOW_ARB, TRUE,
        WGL_SUPPORT_OPENGL_ARB, TRUE,
        WGL_DOUBLE_BUFFER_ARB,  TRUE,
        WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
        WGL_COLOR_BITS_ARB,     32,
        WGL_ALPHA_BITS_ARB,     8,
        WGL_DEPTH_BITS_ARB,     24,
        WGL_STENCIL_BITS_ARB,   8,
        WGL_ACCELERATION_ARB,   WGL_FULL_ACCELERATION_ARB,
        0
    };
    int pixelFormat = 0;
    UINT numFormats = 0;
    if (!m_wglChoosePixelFormatARB(m_hdc, pixelAttribs, nullptr, 1,
                                    &pixelFormat, &numFormats) || numFormats == 0) {
        // Fall back to legacy pixel format selection.
        pixelFormat = ChoosePixelFormat(m_hdc, &pfd);
    }

    if (!SetPixelFormat(m_hdc, pixelFormat, &pfd)) return false;

    // Step 3: Create an OpenGL 4.3 Core Profile context.
    const int contextAttribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 3,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
#ifdef _DEBUG
        WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_DEBUG_BIT_ARB,
#endif
        0
    };

    m_hglrc = m_wglCreateContextAttribsARB(m_hdc, nullptr, contextAttribs);
    if (!m_hglrc) {
        // Some older drivers don't support 4.3 core; try compatibility mode.
        PIXELFORMATDESCRIPTOR pfd2 = pfd;
        m_hglrc = wglCreateContext(m_hdc);
    }

    return m_hglrc != nullptr;
}

bool WGLContext::MakeCurrent()
{
    return wglMakeCurrent(m_hdc, m_hglrc) == TRUE;
}

void WGLContext::ReleaseCurrent()
{
    wglMakeCurrent(nullptr, nullptr);
}

bool WGLContext::SwapBuffers()
{
    return ::SwapBuffers(m_hdc) == TRUE;
}

bool WGLContext::LoadWGLExtensions(HDC hdc)
{
    (void)hdc;
    m_wglCreateContextAttribsARB =
        reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
            wglGetProcAddress("wglCreateContextAttribsARB"));
    m_wglChoosePixelFormatARB =
        reinterpret_cast<PFNWGLCHOOSEPIXELFORMATARBPROC>(
            wglGetProcAddress("wglChoosePixelFormatARB"));

    // wglCreateContextAttribsARB is required; wglChoosePixelFormatARB is optional.
    return m_wglCreateContextAttribsARB != nullptr;
}
