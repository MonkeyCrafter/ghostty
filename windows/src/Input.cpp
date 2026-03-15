#include "Input.h"

/// Map a Win32 virtual key code to a Ghostty ghostty_input_key_e.
/// Returns GHOSTTY_KEY_INVALID for unmapped keys.
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
    case VK_UP:     return GHOSTTY_KEY_UP;
    case VK_DOWN:   return GHOSTTY_KEY_DOWN;
    case VK_LEFT:   return GHOSTTY_KEY_LEFT;
    case VK_RIGHT:  return GHOSTTY_KEY_RIGHT;
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

    // Modifier keys (reported as separate key events)
    case VK_SHIFT:   case VK_LSHIFT:  case VK_RSHIFT:  return GHOSTTY_KEY_LEFT_SHIFT;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return GHOSTTY_KEY_LEFT_CONTROL;
    case VK_MENU:    case VK_LMENU:   case VK_RMENU:   return GHOSTTY_KEY_LEFT_ALT;
    case VK_LWIN:    return GHOSTTY_KEY_LEFT_SUPER;
    case VK_RWIN:    return GHOSTTY_KEY_RIGHT_SUPER;

    // Printable characters: 0-9
    case '0': return GHOSTTY_KEY_ZERO;
    case '1': return GHOSTTY_KEY_ONE;
    case '2': return GHOSTTY_KEY_TWO;
    case '3': return GHOSTTY_KEY_THREE;
    case '4': return GHOSTTY_KEY_FOUR;
    case '5': return GHOSTTY_KEY_FIVE;
    case '6': return GHOSTTY_KEY_SIX;
    case '7': return GHOSTTY_KEY_SEVEN;
    case '8': return GHOSTTY_KEY_EIGHT;
    case '9': return GHOSTTY_KEY_NINE;

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
    case VK_NUMPAD0: return GHOSTTY_KEY_KP_0;
    case VK_NUMPAD1: return GHOSTTY_KEY_KP_1;
    case VK_NUMPAD2: return GHOSTTY_KEY_KP_2;
    case VK_NUMPAD3: return GHOSTTY_KEY_KP_3;
    case VK_NUMPAD4: return GHOSTTY_KEY_KP_4;
    case VK_NUMPAD5: return GHOSTTY_KEY_KP_5;
    case VK_NUMPAD6: return GHOSTTY_KEY_KP_6;
    case VK_NUMPAD7: return GHOSTTY_KEY_KP_7;
    case VK_NUMPAD8: return GHOSTTY_KEY_KP_8;
    case VK_NUMPAD9: return GHOSTTY_KEY_KP_9;
    case VK_DECIMAL:  return GHOSTTY_KEY_KP_DECIMAL;
    case VK_ADD:      return GHOSTTY_KEY_KP_ADD;
    case VK_SUBTRACT: return GHOSTTY_KEY_KP_SUBTRACT;
    case VK_MULTIPLY: return GHOSTTY_KEY_KP_MULTIPLY;
    case VK_DIVIDE:   return GHOSTTY_KEY_KP_DIVIDE;
    case VK_NUMLOCK:  return GHOSTTY_KEY_NUM_LOCK;

    default:
        return GHOSTTY_KEY_INVALID;
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
    return static_cast<ghostty_input_mods_e>(mods);
}
