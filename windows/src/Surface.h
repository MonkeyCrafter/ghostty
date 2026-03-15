#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>

#include "ghostty.h"

class Window;

/// Surface wraps a ghostty_surface_t and implements the libghostty surface
/// callbacks (clipboard, close, focus, etc.). Each Surface corresponds to
/// one terminal pane embedded in a Window.
class Surface {
public:
    explicit Surface(Window* window);
    ~Surface();

    /// Create the ghostty_surface_t using the given config.
    bool Init(ghostty_app_t app);

    /// Notify libghostty of new dimensions (e.g. after WM_SIZE).
    void SetSize(int width, int height);

    /// Draw the terminal. The OpenGL context must be current before calling.
    void Draw();

    /// Feed a key event to libghostty.
    void KeyEvent(ghostty_input_key_e key, ghostty_input_action_e action,
                  ghostty_input_mods_e mods, uint32_t codepoint);

    /// Feed a text input event (WM_CHAR).
    void TextInput(const wchar_t* text);

    /// Feed a mouse position event.
    void MousePos(double x, double y, ghostty_input_mods_e mods);

    /// Feed a mouse button event.
    void MouseButton(ghostty_input_mouse_button_e button,
                     ghostty_input_action_e action,
                     ghostty_input_mods_e mods);

    /// Feed a scroll event.
    void MouseScroll(double dx, double dy, ghostty_input_mods_e mods);

    ghostty_surface_t GhosttyHandle() const { return m_surface; }

    // ── Clipboard callbacks (static trampolines) ──────────────────────────

    static void OnReadClipboard(const char* mime, ghostty_clipboard_request_s req,
                                void* userdata);
    static void OnWriteClipboard(const char* mime, const char* data,
                                 ghostty_clipboard_e kind, void* userdata);
    static void OnCloseSurface(void* userdata);
    static void OnFocusSurface(bool focused, void* userdata);
    static void OnToggledFullscreen(bool fullscreen, void* userdata);
    static ghostty_size_s OnGetSize(void* userdata);
    static ghostty_content_scale_s OnGetContentScale(void* userdata);

private:
    Window*           m_window;
    ghostty_surface_t m_surface = nullptr;

    int m_width  = 800;
    int m_height = 600;

    /// Read text from the Win32 clipboard. Returns empty string on failure.
    static std::string ReadClipboardText();

    /// Write text to the Win32 clipboard.
    static void WriteClipboardText(const std::string& text);
};
