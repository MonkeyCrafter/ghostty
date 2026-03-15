#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <utility>

#include "ghostty.h"

/// Input provides translation between Win32 virtual key codes and
/// Ghostty's ghostty_input_key_e and ghostty_input_mods_e types.
class Input {
public:
    struct KeyResult {
        ghostty_input_key_e    key;
        ghostty_input_mods_e   mods;
    };

    /// Translate a Win32 virtual key code (WPARAM from WM_KEYDOWN) and the
    /// associated LPARAM flags to a Ghostty key + mods pair.
    static KeyResult TranslateKey(WPARAM vkey, LPARAM lParam);

    /// Translate Win32 key state flags (e.g. from WM_MOUSEMOVE wParam or
    /// WM_KEYDOWN GetKeyState()) to Ghostty modifier flags.
    static ghostty_input_mods_e TranslateMods(DWORD keyState);

    /// Build a modifier mask from the current live key state using GetKeyState().
    static ghostty_input_mods_e CurrentMods();
};
