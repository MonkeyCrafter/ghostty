#include "App.h"
#include "Window.h"
#include <cassert>

App::App(HINSTANCE hInstance, int nCmdShow)
    : m_hInstance(hInstance)
    , m_nCmdShow(nCmdShow)
{}

App::~App()
{
    m_windows.clear();
    if (m_app) {
        ghostty_app_free(m_app);
        m_app = nullptr;
    }
}

bool App::Init()
{
    // Build the runtime configuration with our callback trampolines.
    ghostty_runtime_config_s rt_config = {};
    rt_config.userdata = this;
    rt_config.wakeup_cb = &App::OnWakeup;
    rt_config.action_cb = &App::OnAction;

    // Create the libghostty app.
    m_app = ghostty_app_new(ghostty_config_new(), &rt_config);
    if (!m_app) {
        MessageBoxW(nullptr,
            L"Failed to create Ghostty application instance.",
            L"Ghostty Error",
            MB_ICONERROR | MB_OK);
        return false;
    }

    return CreateInitialWindow();
}

int App::Run()
{
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

void App::Wakeup()
{
    // Post a no-op message to wake up the Win32 message loop from another
    // thread. libghostty may call OnWakeup from its I/O threads.
    if (!m_windows.empty()) {
        PostMessageW(m_windows[0]->Hwnd(), WM_NULL, 0, 0);
    }
}

void App::PerformAction(ghostty_target_s target, ghostty_action_s action)
{
    switch (action.tag) {
    case GHOSTTY_ACTION_NEW_WINDOW:
        CreateInitialWindow();
        break;

    case GHOSTTY_ACTION_CLOSE_WINDOW:
        // Find and close the window associated with this target.
        if (target.tag == GHOSTTY_TARGET_SURFACE) {
            for (auto& w : m_windows) {
                if (w->ContainsSurface(target.surface)) {
                    DestroyWindow(w->Hwnd());
                    break;
                }
            }
        }
        break;

    case GHOSTTY_ACTION_QUIT:
        PostQuitMessage(0);
        break;

    case GHOSTTY_ACTION_SET_TITLE:
        if (target.tag == GHOSTTY_TARGET_SURFACE && action.set_title.title) {
            for (auto& w : m_windows) {
                if (w->ContainsSurface(target.surface)) {
                    // Convert UTF-8 title to UTF-16 for Win32.
                    int len = MultiByteToWideChar(CP_UTF8, 0,
                        action.set_title.title, -1, nullptr, 0);
                    if (len > 0) {
                        std::vector<wchar_t> wbuf(len);
                        MultiByteToWideChar(CP_UTF8, 0,
                            action.set_title.title, -1, wbuf.data(), len);
                        SetWindowTextW(w->Hwnd(), wbuf.data());
                    }
                    break;
                }
            }
        }
        break;

    case GHOSTTY_ACTION_RENDER:
        // Trigger a repaint on all windows.
        for (auto& w : m_windows) {
            InvalidateRect(w->Hwnd(), nullptr, FALSE);
        }
        break;

    case GHOSTTY_ACTION_OPEN_URL:
        if (action.open_url.url) {
            // Convert URL to wide string and open with the default handler.
            int len = MultiByteToWideChar(CP_UTF8, 0,
                action.open_url.url, -1, nullptr, 0);
            if (len > 0) {
                std::vector<wchar_t> wurl(len);
                MultiByteToWideChar(CP_UTF8, 0,
                    action.open_url.url, -1, wurl.data(), len);
                ShellExecuteW(nullptr, L"open", wurl.data(),
                    nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        break;

    case GHOSTTY_ACTION_RING_BELL:
        MessageBeep(MB_OK);
        break;

    case GHOSTTY_ACTION_MOUSE_SHAPE:
        // Map Ghostty mouse shapes to Win32 system cursors.
        {
            LPWSTR cursor_id = IDC_ARROW;
            switch (action.mouse_shape) {
            case GHOSTTY_MOUSE_SHAPE_TEXT:      cursor_id = IDC_IBEAM;    break;
            case GHOSTTY_MOUSE_SHAPE_POINTER:   cursor_id = IDC_HAND;     break;
            case GHOSTTY_MOUSE_SHAPE_CROSSHAIR: cursor_id = IDC_CROSS;    break;
            case GHOSTTY_MOUSE_SHAPE_MOVE:      cursor_id = IDC_SIZEALL;  break;
            case GHOSTTY_MOUSE_SHAPE_NOT_ALLOWED: cursor_id = IDC_NO;     break;
            default: break;
            }
            SetCursor(LoadCursorW(nullptr, cursor_id));
        }
        break;

    default:
        // Unimplemented actions are silently ignored for now.
        break;
    }
}

bool App::CreateInitialWindow()
{
    auto window = std::make_unique<Window>(this);
    if (!window->Init(m_nCmdShow)) {
        return false;
    }
    m_windows.push_back(std::move(window));
    return true;
}

void App::RemoveWindow(Window* window)
{
    m_windows.erase(
        std::remove_if(m_windows.begin(), m_windows.end(),
            [window](const std::unique_ptr<Window>& w) {
                return w.get() == window;
            }),
        m_windows.end()
    );

    // If all windows are closed, quit the application.
    if (m_windows.empty()) {
        PostQuitMessage(0);
    }
}

// ── Static callback trampolines ───────────────────────────────────────────

void App::OnWakeup(void* userdata)
{
    auto* app = static_cast<App*>(userdata);
    app->Wakeup();
}

void App::OnAction(ghostty_app_t /*app*/,
                   ghostty_target_s target,
                   ghostty_action_s action,
                   void* userdata)
{
    auto* self = static_cast<App*>(userdata);
    self->PerformAction(target, action);
}
