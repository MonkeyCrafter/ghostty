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
    // Use the library helper to get a zero-initialized config with defaults.
    ghostty_surface_config_s config = ghostty_surface_config_new();

    // Surface userdata = this Surface*, used by App-level clipboard/close
    // callbacks registered in ghostty_runtime_config_s (see App.cpp).
    config.userdata = this;

    m_surface = ghostty_surface_new(app, &config);
    if (!m_surface) return false;

    // Push the initial content scale (DPI) to libghostty.
    if (m_window) {
        HMONITOR mon = MonitorFromWindow(m_window->Hwnd(),
                                         MONITOR_DEFAULTTONEAREST);
        UINT dpiX = 96, dpiY = 96;
        GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        double scale = static_cast<double>(dpiX) / 96.0;
        ghostty_surface_set_content_scale(m_surface, scale, scale);
    }

    return true;
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
    if (!m_surface) return;
    if (key == GHOSTTY_KEY_UNIDENTIFIED) return;

    ghostty_input_key_s ev = {};
    ev.action              = action;
    ev.mods                = mods;
    ev.consumed_mods       = GHOSTTY_MODS_NONE;
    ev.keycode             = static_cast<uint32_t>(key);
    ev.text                = nullptr;
    ev.unshifted_codepoint = codepoint;
    ev.composing           = false;

    ghostty_surface_key(m_surface, ev);
}

void Surface::TextInput(const wchar_t* text)
{
    if (!m_surface || !text) return;
    // Convert UTF-16 text to UTF-8 for libghostty.
    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, text, -1,
                                       nullptr, 0, nullptr, nullptr);
    if (utf8_len <= 1) return; // 1 means only the null terminator
    std::string utf8(utf8_len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1,
                        utf8.data(), utf8_len, nullptr, nullptr);
    ghostty_surface_text(m_surface,
                         utf8.c_str(),
                         static_cast<uintptr_t>(utf8.size()));
}

void Surface::MousePos(double x, double y, ghostty_input_mods_e mods)
{
    if (m_surface) {
        ghostty_surface_mouse_pos(m_surface, x, y, mods);
    }
}

void Surface::MouseButton(ghostty_input_mouse_button_e button,
                           ghostty_input_mouse_state_e state,
                           ghostty_input_mods_e mods)
{
    if (m_surface) {
        // ghostty_surface_mouse_button(surface, state, button, mods)
        ghostty_surface_mouse_button(m_surface, state, button, mods);
    }
}

void Surface::MouseScroll(double dx, double dy, ghostty_input_scroll_mods_t mods)
{
    if (m_surface) {
        ghostty_surface_mouse_scroll(m_surface, dx, dy, mods);
    }
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
