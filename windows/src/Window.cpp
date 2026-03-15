#include "Window.h"
#include "App.h"
#include "WGLContext.h"
#include "Surface.h"
#include "Input.h"
#include <cassert>

Window::Window(App* app)
    : m_app(app)
{}

Window::~Window()
{
    // Surface must be freed before the GL context.
    m_surface.reset();
    m_glContext.reset();
}

bool Window::RegisterWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = &Window::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = ClassName();
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(1)); // optional app icon
    return RegisterClassExW(&wc) != 0;
}

bool Window::Init(int nCmdShow)
{
    // Register window class once.
    static bool registered = RegisterWindowClass(m_app->HInstance());
    if (!registered) return false;

    m_hwnd = CreateWindowExW(
        0,
        ClassName(),
        L"Ghostty",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1200, 800,
        nullptr,
        nullptr,
        m_app->HInstance(),
        this  // Pass Window* as lpCreateParams for WndProc
    );
    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, nCmdShow);
    UpdateWindow(m_hwnd);
    return true;
}

bool Window::ContainsSurface(ghostty_surface_t surface) const
{
    return m_surface && m_surface->GhosttyHandle() == surface;
}

LRESULT Window::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CREATE:
        OnCreate();
        return 0;

    case WM_DESTROY:
        OnDestroy();
        return 0;

    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_PAINT:
        OnPaint();
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        OnKeyDown(wParam, lParam);
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        OnKeyUp(wParam, lParam);
        return 0;

    case WM_CHAR:
    case WM_SYSCHAR:
        OnChar(static_cast<wchar_t>(wParam));
        return 0;

    case WM_MOUSEMOVE:
        OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_LBUTTONDOWN: case WM_LBUTTONUP:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        OnMouseButton(msg, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), wParam);
        return 0;

    case WM_MOUSEWHEEL:
        OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam),
                     GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam),
                     GET_KEYSTATE_WPARAM(wParam));
        return 0;

    case WM_SETFOCUS:
        if (m_surface) {
            ghostty_surface_set_focus(m_surface->GhosttyHandle(), true);
        }
        return 0;

    case WM_KILLFOCUS:
        if (m_surface) {
            ghostty_surface_set_focus(m_surface->GhosttyHandle(), false);
        }
        return 0;

    default:
        return DefWindowProcW(m_hwnd, msg, wParam, lParam);
    }
}

void Window::OnCreate()
{
    // Initialize OpenGL context for this window.
    m_glContext = std::make_unique<WGLContext>();
    if (!m_glContext->Init(m_hwnd)) {
        // Fatal: we can't render without a GL context.
        PostQuitMessage(1);
        return;
    }
    m_glContext->MakeCurrent();

    // Create the libghostty surface.
    m_surface = std::make_unique<Surface>(this);
    if (!m_surface->Init(m_app->GhosttyApp())) {
        PostQuitMessage(1);
        return;
    }

    // Set initial size.
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    m_surface->SetSize(rc.right - rc.left, rc.bottom - rc.top);
}

void Window::OnDestroy()
{
    m_app->RemoveWindow(this);
    // If this was the last window, RemoveWindow() posts WM_QUIT.
}

void Window::OnSize(int width, int height)
{
    if (m_surface) m_surface->SetSize(width, height);
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Window::OnPaint()
{
    PAINTSTRUCT ps;
    BeginPaint(m_hwnd, &ps);

    if (m_glContext && m_surface) {
        m_glContext->MakeCurrent();
        m_surface->Draw();
        m_glContext->SwapBuffers();
    }

    EndPaint(m_hwnd, &ps);
}

void Window::OnKeyDown(WPARAM vkey, LPARAM lParam)
{
    if (!m_surface) return;
    auto [key, mods] = Input::TranslateKey(vkey, lParam);
    if (key != GHOSTTY_KEY_UNIDENTIFIED) {
        m_surface->KeyEvent(key, GHOSTTY_ACTION_PRESS, mods, 0);
    }
}

void Window::OnKeyUp(WPARAM vkey, LPARAM lParam)
{
    if (!m_surface) return;
    auto [key, mods] = Input::TranslateKey(vkey, lParam);
    if (key != GHOSTTY_KEY_UNIDENTIFIED) {
        m_surface->KeyEvent(key, GHOSTTY_ACTION_RELEASE, mods, 0);
    }
}

void Window::OnChar(wchar_t ch)
{
    if (!m_surface) return;
    // Filter out control characters that are handled as key events.
    if (ch >= 0x20 || ch == '\t' || ch == '\r' || ch == '\n' || ch == 0x08) {
        wchar_t text[2] = { ch, L'\0' };
        m_surface->TextInput(text);
    }
}

void Window::OnMouseMove(int x, int y, DWORD keyState)
{
    if (!m_surface) return;
    m_surface->MousePos(static_cast<double>(x),
                        static_cast<double>(y),
                        Input::TranslateMods(keyState));
}

void Window::OnMouseButton(UINT msg, int x, int y, DWORD keyState)
{
    if (!m_surface) return;
    (void)x; (void)y;

    ghostty_input_mouse_button_e button = GHOSTTY_MOUSE_LEFT;
    ghostty_input_mouse_state_e  state  = GHOSTTY_MOUSE_PRESS;

    switch (msg) {
    case WM_LBUTTONDOWN: button = GHOSTTY_MOUSE_LEFT;   state = GHOSTTY_MOUSE_PRESS;   break;
    case WM_LBUTTONUP:   button = GHOSTTY_MOUSE_LEFT;   state = GHOSTTY_MOUSE_RELEASE; break;
    case WM_RBUTTONDOWN: button = GHOSTTY_MOUSE_RIGHT;  state = GHOSTTY_MOUSE_PRESS;   break;
    case WM_RBUTTONUP:   button = GHOSTTY_MOUSE_RIGHT;  state = GHOSTTY_MOUSE_RELEASE; break;
    case WM_MBUTTONDOWN: button = GHOSTTY_MOUSE_MIDDLE; state = GHOSTTY_MOUSE_PRESS;   break;
    case WM_MBUTTONUP:   button = GHOSTTY_MOUSE_MIDDLE; state = GHOSTTY_MOUSE_RELEASE; break;
    }
    m_surface->MouseButton(button, state, Input::TranslateMods(keyState));
}

void Window::OnMouseWheel(int delta, int x, int y, DWORD keyState)
{
    if (!m_surface) return;
    (void)x; (void)y;
    double dy = static_cast<double>(delta) / WHEEL_DELTA;
    // ghostty_input_scroll_mods_t is a packed int; for now pass mods as-is.
    m_surface->MouseScroll(0.0, dy,
        static_cast<ghostty_input_scroll_mods_t>(Input::TranslateMods(keyState)));
}

LRESULT CALLBACK Window::WndProc(HWND hwnd, UINT msg,
                                   WPARAM wParam, LPARAM lParam)
{
    Window* self = nullptr;

    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
