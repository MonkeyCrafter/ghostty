#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>

#include "ghostty.h"

class Window;

/// Surface wraps a ghostty_surface_t and its associated OpenGL rendering.
/// Each Surface corresponds to one terminal pane embedded in a Window.
///
/// Clipboard and close callbacks are registered at the App (runtime) level
/// (see App.cpp), but they receive this object's pointer as their userdata
/// because ghostty_surface_config_s.userdata = this.
class Surface {
public:
    explicit Surface(Window* window);
    ~Surface();

    /// Create the ghostty_surface_t. Must be called with an active GL context.
    bool Init(ghostty_app_t app);

    /// Notify libghostty of new dimensions (e.g. after WM_SIZE).
    void SetSize(int width, int height);

    /// Draw the terminal. The OpenGL context must be current before calling.
    void Draw();

    /// Feed a key event to libghostty.
    void KeyEvent(ghostty_input_key_e key,
                  ghostty_input_action_e action,
                  ghostty_input_mods_e mods,
                  uint32_t codepoint);

    /// Feed a text input event (WM_CHAR).
    void TextInput(const wchar_t* text);

    /// Feed a mouse position event.
    void MousePos(double x, double y, ghostty_input_mods_e mods);

    /// Feed a mouse button event.
    void MouseButton(ghostty_input_mouse_button_e button,
                     ghostty_input_mouse_state_e state,
                     ghostty_input_mods_e mods);

    /// Feed a scroll event.
    void MouseScroll(double dx, double dy, ghostty_input_scroll_mods_t mods);

    ghostty_surface_t GhosttyHandle() const { return m_surface; }
    Window*           GetWindow()     const { return m_window; }

    // ── Clipboard helpers (called from App callback trampolines) ───────────
    static std::string ReadClipboardText();
    static void WriteClipboardText(const std::string& text);

private:
    Window*           m_window;
    ghostty_surface_t m_surface = nullptr;

    int m_width  = 800;
    int m_height = 600;
};
