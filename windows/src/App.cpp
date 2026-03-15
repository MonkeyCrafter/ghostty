#include "App.h"
#include "Window.h"
#include "Surface.h"
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
    // NOTE: userdata here (App*) is used only by wakeup_cb.
    // Clipboard/close callbacks receive *surface* userdata (Surface*), set
    // in ghostty_surface_config_s.userdata inside Surface::Init().
    ghostty_runtime_config_s rt_config = {};
    rt_config.userdata                      = this;
    rt_config.wakeup_cb                     = &App::OnWakeup;
    rt_config.action_cb                     = &App::OnAction;
    rt_config.read_clipboard_cb             = &App::OnReadClipboard;
    rt_config.confirm_read_clipboard_cb     = &App::OnConfirmReadClipboard;
    rt_config.write_clipboard_cb            = &App::OnWriteClipboard;
    rt_config.close_surface_cb              = &App::OnCloseSurface;
    rt_config.supports_selection_clipboard  = false; // Win32 has no primary selection

    // ghostty_app_new(runtime_config, ghostty_config) — runtime config is FIRST.
    ghostty_config_t config = ghostty_config_new();
    m_app = ghostty_app_new(&rt_config, config);
    ghostty_config_free(config);

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
        ghostty_app_tick(m_app);
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

bool App::PerformAction(ghostty_target_s target, ghostty_action_s action)
{
    switch (action.tag) {
    case GHOSTTY_ACTION_NEW_WINDOW:
        CreateInitialWindow();
        return true;

    case GHOSTTY_ACTION_CLOSE_WINDOW:
        // Find and close the window associated with this target surface.
        if (target.tag == GHOSTTY_TARGET_SURFACE) {
            for (auto& w : m_windows) {
                if (w->ContainsSurface(target.target.surface)) {
                    DestroyWindow(w->Hwnd());
                    return true;
                }
            }
        }
        return false;

    case GHOSTTY_ACTION_QUIT:
        PostQuitMessage(0);
        return true;

    case GHOSTTY_ACTION_SET_TITLE:
        if (target.tag == GHOSTTY_TARGET_SURFACE &&
            action.action.set_title.title)
        {
            for (auto& w : m_windows) {
                if (w->ContainsSurface(target.target.surface)) {
                    // Convert UTF-8 title to UTF-16 for Win32.
                    int len = MultiByteToWideChar(CP_UTF8, 0,
                        action.action.set_title.title, -1, nullptr, 0);
                    if (len > 0) {
                        std::vector<wchar_t> wbuf(len);
                        MultiByteToWideChar(CP_UTF8, 0,
                            action.action.set_title.title, -1,
                            wbuf.data(), len);
                        SetWindowTextW(w->Hwnd(), wbuf.data());
                    }
                    return true;
                }
            }
        }
        return false;

    case GHOSTTY_ACTION_RENDER:
        // Trigger a repaint on all windows.
        for (auto& w : m_windows) {
            InvalidateRect(w->Hwnd(), nullptr, FALSE);
        }
        return true;

    case GHOSTTY_ACTION_OPEN_URL:
        if (action.action.open_url.url) {
            int len = MultiByteToWideChar(CP_UTF8, 0,
                action.action.open_url.url, -1, nullptr, 0);
            if (len > 0) {
                std::vector<wchar_t> wurl(len);
                MultiByteToWideChar(CP_UTF8, 0,
                    action.action.open_url.url, -1, wurl.data(), len);
                ShellExecuteW(nullptr, L"open", wurl.data(),
                    nullptr, nullptr, SW_SHOWNORMAL);
            }
        }
        return true;

    case GHOSTTY_ACTION_RING_BELL:
        MessageBeep(MB_OK);
        return true;

    case GHOSTTY_ACTION_MOUSE_SHAPE:
        {
            LPWSTR cursor_id = IDC_ARROW;
            switch (action.action.mouse_shape) {
            case GHOSTTY_MOUSE_SHAPE_DEFAULT:   cursor_id = IDC_ARROW;   break;
            case GHOSTTY_MOUSE_SHAPE_TEXT:      cursor_id = IDC_IBEAM;   break;
            case GHOSTTY_MOUSE_SHAPE_POINTER:   cursor_id = IDC_HAND;    break;
            case GHOSTTY_MOUSE_SHAPE_CROSSHAIR: cursor_id = IDC_CROSS;   break;
            case GHOSTTY_MOUSE_SHAPE_MOVE:      cursor_id = IDC_SIZEALL; break;
            case GHOSTTY_MOUSE_SHAPE_NOT_ALLOWED: cursor_id = IDC_NO;    break;
            default: break;
            }
            SetCursor(LoadCursorW(nullptr, cursor_id));
        }
        return true;

    default:
        // Unimplemented actions are silently ignored.
        return false;
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

bool App::OnAction(ghostty_app_t app,
                   ghostty_target_s target,
                   ghostty_action_s action)
{
    // The App* is stored as the app's userdata.
    auto* self = static_cast<App*>(ghostty_app_userdata(app));
    return self->PerformAction(target, action);
}

// Clipboard / close callbacks — first void* is the Surface* (surface userdata).

bool App::OnReadClipboard(void* userdata,
                          ghostty_clipboard_e /*type*/,
                          void* request)
{
    auto* surface = static_cast<Surface*>(userdata);
    std::string text = Surface::ReadClipboardText();
    ghostty_surface_complete_clipboard_request(
        surface->GhosttyHandle(),
        text.c_str(),
        request,
        false); // not yet confirmed
    return true;
}

void App::OnConfirmReadClipboard(void* userdata,
                                 const char* text,
                                 void* request,
                                 ghostty_clipboard_request_e /*kind*/)
{
    // For Win32 we simply confirm all paste operations without prompting.
    auto* surface = static_cast<Surface*>(userdata);
    ghostty_surface_complete_clipboard_request(
        surface->GhosttyHandle(),
        text,
        request,
        true); // confirmed
}

void App::OnWriteClipboard(void* /*userdata*/,
                           ghostty_clipboard_e /*type*/,
                           const ghostty_clipboard_content_s* content,
                           size_t count,
                           bool /*confirm*/)
{
    // Write the first text/plain content to the Win32 clipboard.
    for (size_t i = 0; i < count; ++i) {
        if (content[i].data) {
            Surface::WriteClipboardText(content[i].data);
            return;
        }
    }
}

void App::OnCloseSurface(void* userdata, bool /*process_alive*/)
{
    auto* surface = static_cast<Surface*>(userdata);
    if (surface->GetWindow()) {
        DestroyWindow(surface->GetWindow()->Hwnd());
    }
}
