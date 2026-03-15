#include "Surface.h"
#include "Window.h"
#include "App.h"
#include <cassert>

Surface::Surface(Window* window)
    : m_window(window)
{}

Surface::~Surface()
{
    if (m_surface) {
        ghostty_surface_free(m_surface);
        m_surface = nullptr;
    }
}

bool Surface::Init(ghostty_app_t app)
{
    ghostty_surface_config_s config = {};
    config.userdata                 = this;
    config.read_clipboard_cb        = &Surface::OnReadClipboard;
    config.write_clipboard_cb       = &Surface::OnWriteClipboard;
    config.close_surface_cb         = &Surface::OnCloseSurface;
    config.focus_surface_cb         = &Surface::OnFocusSurface;
    config.toggle_fullscreen_cb     = &Surface::OnToggledFullscreen;
    config.get_size_cb              = &Surface::OnGetSize;
    config.get_content_scale_cb     = &Surface::OnGetContentScale;

    m_surface = ghostty_surface_new(app, &config);
    return m_surface != nullptr;
}

void Surface::SetSize(int width, int height)
{
    m_width  = width;
    m_height = height;
    if (m_surface) {
        ghostty_surface_set_size(m_surface,
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height));
    }
}

void Surface::Draw()
{
    if (m_surface) {
        ghostty_surface_draw(m_surface);
    }
}

void Surface::KeyEvent(ghostty_input_key_e key,
                       ghostty_input_action_e action,
                       ghostty_input_mods_e mods,
                       uint32_t codepoint)
{
    if (m_surface) {
        ghostty_surface_key(m_surface, key, action, mods, codepoint);
    }
}

void Surface::TextInput(const wchar_t* text)
{
    if (!m_surface || !text) return;
    // Convert UTF-16 text to UTF-8 for libghostty.
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 0) return;
    std::string utf8(utf8_len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8.data(), utf8_len, nullptr, nullptr);
    ghostty_surface_text(m_surface, utf8.c_str(), static_cast<uint32_t>(utf8.size()));
}

void Surface::MousePos(double x, double y, ghostty_input_mods_e mods)
{
    if (m_surface) {
        ghostty_surface_mouse_pos(m_surface, x, y, mods);
    }
}

void Surface::MouseButton(ghostty_input_mouse_button_e button,
                           ghostty_input_action_e action,
                           ghostty_input_mods_e mods)
{
    if (m_surface) {
        ghostty_surface_mouse_button(m_surface, button, action, mods);
    }
}

void Surface::MouseScroll(double dx, double dy, ghostty_input_mods_e mods)
{
    if (m_surface) {
        ghostty_surface_mouse_scroll(m_surface, dx, dy, mods);
    }
}

// ── Static callback trampolines ───────────────────────────────────────────

void Surface::OnReadClipboard(const char* mime,
                               ghostty_clipboard_request_s req,
                               void* userdata)
{
    (void)mime;
    (void)userdata;
    std::string text = ReadClipboardText();
    ghostty_surface_complete_clipboard_request(req,
        text.c_str(), static_cast<uint32_t>(text.size()));
}

void Surface::OnWriteClipboard(const char* mime,
                                const char* data,
                                ghostty_clipboard_e kind,
                                void* userdata)
{
    (void)mime;
    (void)kind;
    (void)userdata;
    if (data) WriteClipboardText(data);
}

void Surface::OnCloseSurface(void* userdata)
{
    // When libghostty asks us to close a surface, destroy the window.
    auto* self = static_cast<Surface*>(userdata);
    if (self->m_window) {
        DestroyWindow(self->m_window->Hwnd());
    }
}

void Surface::OnFocusSurface(bool focused, void* userdata)
{
    (void)focused;
    (void)userdata;
    // Nothing extra needed — Win32 handles focus painting automatically.
}

void Surface::OnToggledFullscreen(bool fullscreen, void* userdata)
{
    (void)userdata;
    (void)fullscreen;
    // TODO: implement fullscreen toggle via SetWindowLong / SetWindowPos.
}

ghostty_size_s Surface::OnGetSize(void* userdata)
{
    auto* self = static_cast<Surface*>(userdata);
    return { static_cast<uint32_t>(self->m_width),
             static_cast<uint32_t>(self->m_height) };
}

ghostty_content_scale_s Surface::OnGetContentScale(void* userdata)
{
    auto* self = static_cast<Surface*>(userdata);
    // Query DPI from the window's monitor.
    UINT dpi = 96; // default (100%)
    if (self->m_window) {
        HMONITOR mon = MonitorFromWindow(self->m_window->Hwnd(),
                                         MONITOR_DEFAULTTONEAREST);
        UINT dpiX, dpiY;
        if (GetDpiForMonitor) {
            GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
            dpi = dpiX;
        }
    }
    float scale = static_cast<float>(dpi) / 96.0f;
    return { scale, scale };
}

// ── Clipboard helpers ─────────────────────────────────────────────────────

std::string Surface::ReadClipboardText()
{
    std::string result;
    if (!OpenClipboard(nullptr)) return result;

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* wtext = static_cast<const wchar_t*>(GlobalLock(h));
        if (wtext) {
            int len = WideCharToMultiByte(CP_UTF8, 0, wtext, -1,
                nullptr, 0, nullptr, nullptr);
            if (len > 1) {
                result.resize(len - 1);
                WideCharToMultiByte(CP_UTF8, 0, wtext, -1,
                    result.data(), len, nullptr, nullptr);
            }
            GlobalUnlock(h);
        }
    }

    CloseClipboard();
    return result;
}

void Surface::WriteClipboardText(const std::string& text)
{
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();

    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
        static_cast<int>(text.size()), nullptr, 0);
    if (wlen > 0) {
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
        if (hg) {
            wchar_t* wbuf = static_cast<wchar_t*>(GlobalLock(hg));
            if (wbuf) {
                MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                    static_cast<int>(text.size()), wbuf, wlen);
                wbuf[wlen] = L'\0';
                GlobalUnlock(hg);
                SetClipboardData(CF_UNICODETEXT, hg);
            } else {
                GlobalFree(hg);
            }
        }
    }

    CloseClipboard();
}
