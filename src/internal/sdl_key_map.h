/*
 * vkQuake RmlUI - SDL to RmlUI Key Translation
 *
 * Maps SDL keycodes and modifiers to their RmlUI equivalents.
 * Pure lookup — no global state dependencies.
 */

#ifndef QRMLUI_SDL_KEY_MAP_H
#define QRMLUI_SDL_KEY_MAP_H

#include <RmlUi/Core/Input.h>
#include <SDL.h>

namespace QRmlUI {

inline Rml::Input::KeyIdentifier TranslateKey(int sdl_key)
{
    using namespace Rml::Input;

    switch (sdl_key) {
        case SDLK_UNKNOWN:      return KI_UNKNOWN;
        case SDLK_SPACE:        return KI_SPACE;
        case SDLK_0:            return KI_0;
        case SDLK_1:            return KI_1;
        case SDLK_2:            return KI_2;
        case SDLK_3:            return KI_3;
        case SDLK_4:            return KI_4;
        case SDLK_5:            return KI_5;
        case SDLK_6:            return KI_6;
        case SDLK_7:            return KI_7;
        case SDLK_8:            return KI_8;
        case SDLK_9:            return KI_9;
        case SDLK_a:            return KI_A;
        case SDLK_b:            return KI_B;
        case SDLK_c:            return KI_C;
        case SDLK_d:            return KI_D;
        case SDLK_e:            return KI_E;
        case SDLK_f:            return KI_F;
        case SDLK_g:            return KI_G;
        case SDLK_h:            return KI_H;
        case SDLK_i:            return KI_I;
        case SDLK_j:            return KI_J;
        case SDLK_k:            return KI_K;
        case SDLK_l:            return KI_L;
        case SDLK_m:            return KI_M;
        case SDLK_n:            return KI_N;
        case SDLK_o:            return KI_O;
        case SDLK_p:            return KI_P;
        case SDLK_q:            return KI_Q;
        case SDLK_r:            return KI_R;
        case SDLK_s:            return KI_S;
        case SDLK_t:            return KI_T;
        case SDLK_u:            return KI_U;
        case SDLK_v:            return KI_V;
        case SDLK_w:            return KI_W;
        case SDLK_x:            return KI_X;
        case SDLK_y:            return KI_Y;
        case SDLK_z:            return KI_Z;
        case SDLK_SEMICOLON:    return KI_OEM_1;
        case SDLK_PLUS:         return KI_OEM_PLUS;
        case SDLK_COMMA:        return KI_OEM_COMMA;
        case SDLK_MINUS:        return KI_OEM_MINUS;
        case SDLK_PERIOD:       return KI_OEM_PERIOD;
        case SDLK_SLASH:        return KI_OEM_2;
        case SDLK_BACKQUOTE:    return KI_OEM_3;
        case SDLK_LEFTBRACKET:  return KI_OEM_4;
        case SDLK_BACKSLASH:    return KI_OEM_5;
        case SDLK_RIGHTBRACKET: return KI_OEM_6;
        case SDLK_QUOTEDBL:     return KI_OEM_7;
        case SDLK_KP_0:         return KI_NUMPAD0;
        case SDLK_KP_1:         return KI_NUMPAD1;
        case SDLK_KP_2:         return KI_NUMPAD2;
        case SDLK_KP_3:         return KI_NUMPAD3;
        case SDLK_KP_4:         return KI_NUMPAD4;
        case SDLK_KP_5:         return KI_NUMPAD5;
        case SDLK_KP_6:         return KI_NUMPAD6;
        case SDLK_KP_7:         return KI_NUMPAD7;
        case SDLK_KP_8:         return KI_NUMPAD8;
        case SDLK_KP_9:         return KI_NUMPAD9;
        case SDLK_KP_ENTER:     return KI_NUMPADENTER;
        case SDLK_KP_MULTIPLY:  return KI_MULTIPLY;
        case SDLK_KP_PLUS:      return KI_ADD;
        case SDLK_KP_MINUS:     return KI_SUBTRACT;
        case SDLK_KP_PERIOD:    return KI_DECIMAL;
        case SDLK_KP_DIVIDE:    return KI_DIVIDE;
        case SDLK_BACKSPACE:    return KI_BACK;
        case SDLK_TAB:          return KI_TAB;
        case SDLK_CLEAR:        return KI_CLEAR;
        case SDLK_RETURN:       return KI_RETURN;
        case SDLK_PAUSE:        return KI_PAUSE;
        case SDLK_CAPSLOCK:     return KI_CAPITAL;
        case SDLK_ESCAPE:       return KI_ESCAPE;
        case SDLK_PAGEUP:       return KI_PRIOR;
        case SDLK_PAGEDOWN:     return KI_NEXT;
        case SDLK_END:          return KI_END;
        case SDLK_HOME:         return KI_HOME;
        case SDLK_LEFT:         return KI_LEFT;
        case SDLK_UP:           return KI_UP;
        case SDLK_RIGHT:        return KI_RIGHT;
        case SDLK_DOWN:         return KI_DOWN;
        case SDLK_INSERT:       return KI_INSERT;
        case SDLK_DELETE:       return KI_DELETE;
        case SDLK_HELP:         return KI_HELP;
        case SDLK_F1:           return KI_F1;
        case SDLK_F2:           return KI_F2;
        case SDLK_F3:           return KI_F3;
        case SDLK_F4:           return KI_F4;
        case SDLK_F5:           return KI_F5;
        case SDLK_F6:           return KI_F6;
        case SDLK_F7:           return KI_F7;
        case SDLK_F8:           return KI_F8;
        case SDLK_F9:           return KI_F9;
        case SDLK_F10:          return KI_F10;
        case SDLK_F11:          return KI_F11;
        case SDLK_F12:          return KI_F12;
        case SDLK_F13:          return KI_F13;
        case SDLK_F14:          return KI_F14;
        case SDLK_F15:          return KI_F15;
        case SDLK_NUMLOCKCLEAR: return KI_NUMLOCK;
        case SDLK_SCROLLLOCK:   return KI_SCROLL;
        case SDLK_LSHIFT:       return KI_LSHIFT;
        case SDLK_RSHIFT:       return KI_RSHIFT;
        case SDLK_LCTRL:        return KI_LCONTROL;
        case SDLK_RCTRL:        return KI_RCONTROL;
        case SDLK_LALT:         return KI_LMENU;
        case SDLK_RALT:         return KI_RMENU;
        case SDLK_LGUI:         return KI_LMETA;
        case SDLK_RGUI:         return KI_RMETA;
        default:                return KI_UNKNOWN;
    }
}

inline int GetKeyModifiers()
{
    SDL_Keymod sdl_mods = SDL_GetModState();
    int mods = 0;

    if (sdl_mods & KMOD_CTRL)  mods |= Rml::Input::KM_CTRL;
    if (sdl_mods & KMOD_SHIFT) mods |= Rml::Input::KM_SHIFT;
    if (sdl_mods & KMOD_ALT)   mods |= Rml::Input::KM_ALT;
    if (sdl_mods & KMOD_GUI)   mods |= Rml::Input::KM_META;
    if (sdl_mods & KMOD_NUM)   mods |= Rml::Input::KM_NUMLOCK;
    if (sdl_mods & KMOD_CAPS)  mods |= Rml::Input::KM_CAPSLOCK;

    return mods;
}

} // namespace QRmlUI

#endif /* QRMLUI_SDL_KEY_MAP_H */
