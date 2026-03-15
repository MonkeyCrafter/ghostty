#include "Input.h"

/// Map a Win32 virtual key code to a Ghostty ghostty_input_key_e.
/// Returns GHOSTTY_KEY_UNIDENTIFIED for unmapped keys.
static ghostty_input_key_e VkToGhosttyKey(WPARAM vkey)
{
    switch (vkey) {
    // Function keys
    case VK_F1:  return GHOSTTY_KEY_F1;
    case VK_F2:  return GHOSTTY_KEY_F2;
    case VK_F3:  return GHOSTTY_KEY_F3;
    case VK_F4:  return GHOSTTY_KEY_F4;
    case VK_F5:  return GHOSTTY_KEY_F5;
    case VK_F6:  return GHOSTTY_KEY_F6;
    case VK_F7:  return GHOSTTY_KEY_F7;
    case VK_F8:  return GHOSTTY_KEY_F8;
    case VK_F9:  return GHOSTTY_KEY_F9;
    case VK_F10: return GHOSTTY_KEY_F10;
    case VK_F11: return GHOSTTY_KEY_F11;
    case VK_F12: return GHOSTTY_KEY_F12;

    // Navigation
    case VK_UP:     return GHOSTTY_KEY_ARROW_UP;
    case VK_DOWN:   return GHOSTTY_KEY_ARROW_DOWN;
    case VK_LEFT:   return GHOSTTY_KEY_ARROW_LEFT;
    case VK_RIGHT:  return GHOSTTY_KEY_ARROW_RIGHT;
    case VK_HOME:   return GHOSTTY_KEY_HOME;
    case VK_END:    return GHOSTTY_KEY_END;
    case VK_PRIOR:  return GHOSTTY_KEY_PAGE_UP;
    case VK_NEXT:   return GHOSTTY_KEY_PAGE_DOWN;
    case VK_INSERT: return GHOSTTY_KEY_INSERT;
    case VK_DELETE: return GHOSTTY_KEY_DELETE;

    // Editing
    case VK_BACK:   return GHOSTTY_KEY_BACKSPACE;
    case VK_RETURN: return GHOSTTY_KEY_ENTER;
    case VK_TAB:    return GHOSTTY_KEY_TAB;
    case VK_ESCAPE: return GHOSTTY_KEY_ESCAPE;
    case VK_SPACE:  return GHOSTTY_KEY_SPACE;

    // Modifier keys (reported as separate key events).
    // Win32 does not always distinguish left/right for VK_SHIFT etc.;
    // use VK_LSHIFT/VK_RSHIFT for that distinction when available.
    case VK_LSHIFT:   return GHOSTTY_KEY_SHIFT_LEFT;
    case VK_RSHIFT:   return GHOSTTY_KEY_SHIFT_RIGHT;
    case VK_SHIFT:    return GHOSTTY_KEY_SHIFT_LEFT;
    case VK_LCONTROL: return GHOSTTY_KEY_CONTROL_LEFT;
    case VK_RCONTROL: return GHOSTTY_KEY_CONTROL_RIGHT;
    case VK_CONTROL:  return GHOSTTY_KEY_CONTROL_LEFT;
    case VK_LMENU:    return GHOSTTY_KEY_ALT_LEFT;
    case VK_RMENU:    return GHOSTTY_KEY_ALT_RIGHT;
    case VK_MENU:     return GHOSTTY_KEY_ALT_LEFT;
    case VK_LWIN:     return GHOSTTY_KEY_META_LEFT;
    case VK_RWIN:     return GHOSTTY_KEY_META_RIGHT;

    // Printable characters: 0-9
    case '0': return GHOSTTY_KEY_DIGIT_0;
    case '1': return GHOSTTY_KEY_DIGIT_1;
    case '2': return GHOSTTY_KEY_DIGIT_2;
    case '3': return GHOSTTY_KEY_DIGIT_3;
    case '4': return GHOSTTY_KEY_DIGIT_4;
    case '5': return GHOSTTY_KEY_DIGIT_5;
    case '6': return GHOSTTY_KEY_DIGIT_6;
    case '7': return GHOSTTY_KEY_DIGIT_7;
    case '8': return GHOSTTY_KEY_DIGIT_8;
    case '9': return GHOSTTY_KEY_DIGIT_9;

    // Printable characters: A-Z (VK codes are uppercase ASCII)
    case 'A': return GHOSTTY_KEY_A;  case 'B': return GHOSTTY_KEY_B;
    case 'C': return GHOSTTY_KEY_C;  case 'D': return GHOSTTY_KEY_D;
    case 'E': return GHOSTTY_KEY_E;  case 'F': return GHOSTTY_KEY_F;
    case 'G': return GHOSTTY_KEY_G;  case 'H': return GHOSTTY_KEY_H;
    case 'I': return GHOSTTY_KEY_I;  case 'J': return GHOSTTY_KEY_J;
    case 'K': return GHOSTTY_KEY_K;  case 'L': return GHOSTTY_KEY_L;
    case 'M': return GHOSTTY_KEY_M;  case 'N': return GHOSTTY_KEY_N;
    case 'O': return GHOSTTY_KEY_O;  case 'P': return GHOSTTY_KEY_P;
    case 'Q': return GHOSTTY_KEY_Q;  case 'R': return GHOSTTY_KEY_R;
    case 'S': return GHOSTTY_KEY_S;  case 'T': return GHOSTTY_KEY_T;
    case 'U': return GHOSTTY_KEY_U;  case 'V': return GHOSTTY_KEY_V;
    case 'W': return GHOSTTY_KEY_W;  case 'X': return GHOSTTY_KEY_X;
    case 'Y': return GHOSTTY_KEY_Y;  case 'Z': return GHOSTTY_KEY_Z;

    // Numpad
    case VK_NUMPAD0: return GHOSTTY_KEY_NUMPAD_0;
    case VK_NUMPAD1: return GHOSTTY_KEY_NUMPAD_1;
    case VK_NUMPAD2: return GHOSTTY_KEY_NUMPAD_2;
    case VK_NUMPAD3: return GHOSTTY_KEY_NUMPAD_3;
    case VK_NUMPAD4: return GHOSTTY_KEY_NUMPAD_4;
    case VK_NUMPAD5: return GHOSTTY_KEY_NUMPAD_5;
    case VK_NUMPAD6: return GHOSTTY_KEY_NUMPAD_6;
    case VK_NUMPAD7: return GHOSTTY_KEY_NUMPAD_7;
    case VK_NUMPAD8: return GHOSTTY_KEY_NUMPAD_8;
    case VK_NUMPAD9: return GHOSTTY_KEY_NUMPAD_9;
    case VK_DECIMAL:  return GHOSTTY_KEY_NUMPAD_DECIMAL;
    case VK_ADD:      return GHOSTTY_KEY_NUMPAD_ADD;
    case VK_SUBTRACT: return GHOSTTY_KEY_NUMPAD_SUBTRACT;
    case VK_MULTIPLY: return GHOSTTY_KEY_NUMPAD_MULTIPLY;
    case VK_DIVIDE:   return GHOSTTY_KEY_NUMPAD_DIVIDE;
    case VK_NUMLOCK:  return GHOSTTY_KEY_NUM_LOCK;

    default:
        return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

Input::KeyResult Input::TranslateKey(WPARAM vkey, LPARAM lParam)
{
    (void)lParam;
    ghostty_input_key_e key  = VkToGhosttyKey(vkey);
    ghostty_input_mods_e mods = CurrentMods();
    return { key, mods };
}

ghostty_input_mods_e Input::TranslateMods(DWORD keyState)
{
    int mods = 0;
    if (keyState & MK_SHIFT)   mods |= GHOSTTY_MODS_SHIFT;
    if (keyState & MK_CONTROL) mods |= GHOSTTY_MODS_CTRL;
    if (keyState & MK_ALT)     mods |= GHOSTTY_MODS_ALT;
    return static_cast<ghostty_input_mods_e>(mods);
}

ghostty_input_mods_e Input::CurrentMods()
{
    int mods = 0;
    if (GetKeyState(VK_SHIFT)   & 0x8000) mods |= GHOSTTY_MODS_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= GHOSTTY_MODS_CTRL;
    if (GetKeyState(VK_MENU)    & 0x8000) mods |= GHOSTTY_MODS_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        mods |= GHOSTTY_MODS_SUPER;
    // Distinguish right-side modifiers for the ghostty mods bitfield.
    if (GetKeyState(VK_RSHIFT)   & 0x8000) mods |= GHOSTTY_MODS_SHIFT_RIGHT;
    if (GetKeyState(VK_RCONTROL) & 0x8000) mods |= GHOSTTY_MODS_CTRL_RIGHT;
    if (GetKeyState(VK_RMENU)    & 0x8000) mods |= GHOSTTY_MODS_ALT_RIGHT;
    return static_cast<ghostty_input_mods_e>(mods);
}
