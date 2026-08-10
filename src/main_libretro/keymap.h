/* libretro (RETROK_*) -> FM Towns JIS keyboard code mapping.
 *
 * FM Towns keyboard codes are defined as TOWNS_JISKEY_* in towns/townsdef/townsdef.h.
 * libretro RETROK_* values mirror SDL 1.2 keysyms (letters/digits equal their ASCII
 * value), so the mapping below is ASCII-driven where possible.
 *
 * This header is included by libretro.cpp only.
 */
#ifndef TOWNS_LIBRETRO_KEYMAP_IS_INCLUDED
#define TOWNS_LIBRETRO_KEYMAP_IS_INCLUDED

#include "towns/townsdef/townsdef.h"

/* Returns a TOWNS_JISKEY_* code for a given libretro RETROK_*, or 0 if unmapped. */
static inline unsigned int TownsKeyFromRetroKey(unsigned int retroKey)
{
	switch(retroKey)
	{
	case 8:   return TOWNS_JISKEY_BACKSPACE;
	case 9:   return TOWNS_JISKEY_TAB;
	case 13:  return TOWNS_JISKEY_RETURN;
	case 27:  return TOWNS_JISKEY_ESC;
	case 32:  return TOWNS_JISKEY_SPACE;

	case 44:  return TOWNS_JISKEY_COMMA;
	case 45:  return TOWNS_JISKEY_MINUS;
	case 46:  return TOWNS_JISKEY_DOT;
	case 47:  return TOWNS_JISKEY_SLASH;

	case 48:  return TOWNS_JISKEY_0;
	case 49:  return TOWNS_JISKEY_1;
	case 50:  return TOWNS_JISKEY_2;
	case 51:  return TOWNS_JISKEY_3;
	case 52:  return TOWNS_JISKEY_4;
	case 53:  return TOWNS_JISKEY_5;
	case 54:  return TOWNS_JISKEY_6;
	case 55:  return TOWNS_JISKEY_7;
	case 56:  return TOWNS_JISKEY_8;
	case 57:  return TOWNS_JISKEY_9;

	case 59:  return TOWNS_JISKEY_SEMICOLON;
	case 61:  return TOWNS_JISKEY_HAT;   // '=' -> '^' (closest JIS key)
	case 91:  return TOWNS_JISKEY_LEFT_SQ_BRACKET;
	case 92:  return TOWNS_JISKEY_BACKSLASH;
	case 93:  return TOWNS_JISKEY_RIGHT_SQ_BRACKET;
	case 94:  return TOWNS_JISKEY_HAT;

	/* Letters A-Z */
	/* libretro 的字母键码是小写 RETROK_a..z = 97..122（无 RETROK_A 大写）。
	   返回物理 JIS 键（TOWNS_JISKEY_A），大小写由 Shift 修饰键状态决定。 */
	case 97:  return TOWNS_JISKEY_A;
	case 98:  return TOWNS_JISKEY_B;
	case 99:  return TOWNS_JISKEY_C;
	case 100: return TOWNS_JISKEY_D;
	case 101: return TOWNS_JISKEY_E;
	case 102: return TOWNS_JISKEY_F;
	case 103: return TOWNS_JISKEY_G;
	case 104: return TOWNS_JISKEY_H;
	case 105: return TOWNS_JISKEY_I;
	case 106: return TOWNS_JISKEY_J;
	case 107: return TOWNS_JISKEY_K;
	case 108: return TOWNS_JISKEY_L;
	case 109: return TOWNS_JISKEY_M;
	case 110: return TOWNS_JISKEY_N;
	case 111: return TOWNS_JISKEY_O;
	case 112: return TOWNS_JISKEY_P;
	case 113: return TOWNS_JISKEY_Q;
	case 114: return TOWNS_JISKEY_R;
	case 115: return TOWNS_JISKEY_S;
	case 116: return TOWNS_JISKEY_T;
	case 117: return TOWNS_JISKEY_U;
	case 118: return TOWNS_JISKEY_V;
	case 119: return TOWNS_JISKEY_W;
	case 120: return TOWNS_JISKEY_X;
	case 121: return TOWNS_JISKEY_Y;
	case 122: return TOWNS_JISKEY_Z;

	/* Modifiers */
	case 303: return TOWNS_JISKEY_SHIFT;  // RSHIFT
	case 304: return TOWNS_JISKEY_SHIFT;  // LSHIFT
	case 305: return TOWNS_JISKEY_CTRL;   // RCTRL
	case 306: return TOWNS_JISKEY_CTRL;   // LCTRL
	case 307: return TOWNS_JISKEY_ALT;    // RALT
	case 308: return TOWNS_JISKEY_ALT;    // LALT

	/* Arrows / editing */
	case 273: return TOWNS_JISKEY_UP;
	case 274: return TOWNS_JISKEY_DOWN;
	case 275: return TOWNS_JISKEY_RIGHT;
	case 276: return TOWNS_JISKEY_LEFT;
	case 277: return TOWNS_JISKEY_INSERT;
	case 278: return TOWNS_JISKEY_HOME;
	case 279: return TOWNS_JISKEY_NEXT;   // END -> NEXT (closest)
	case 280: return TOWNS_JISKEY_HOME;   // PAGEUP (no direct key; reuse)
	case 281: return TOWNS_JISKEY_NEXT;   // PAGEDOWN
	case 127: return TOWNS_JISKEY_DELETE;

	/* Function keys */
	case 282: return TOWNS_JISKEY_PF01;
	case 283: return TOWNS_JISKEY_PF02;
	case 284: return TOWNS_JISKEY_PF03;
	case 285: return TOWNS_JISKEY_PF04;
	case 286: return TOWNS_JISKEY_PF05;
	case 287: return TOWNS_JISKEY_PF06;
	case 288: return TOWNS_JISKEY_PF07;
	case 289: return TOWNS_JISKEY_PF08;
	case 290: return TOWNS_JISKEY_PF09;
	case 291: return TOWNS_JISKEY_PF10;
	case 292: return TOWNS_JISKEY_PF11;
	case 293: return TOWNS_JISKEY_PF12;
	case 294: return TOWNS_JISKEY_PF13;
	case 295: return TOWNS_JISKEY_PF14;
	case 296: return TOWNS_JISKEY_PF15;

	/* Numeric keypad */
	case 256: return TOWNS_JISKEY_NUM_0;
	case 257: return TOWNS_JISKEY_NUM_1;
	case 258: return TOWNS_JISKEY_NUM_2;
	case 259: return TOWNS_JISKEY_NUM_3;
	case 260: return TOWNS_JISKEY_NUM_4;
	case 261: return TOWNS_JISKEY_NUM_5;
	case 262: return TOWNS_JISKEY_NUM_6;
	case 263: return TOWNS_JISKEY_NUM_7;
	case 264: return TOWNS_JISKEY_NUM_8;
	case 265: return TOWNS_JISKEY_NUM_9;
	case 266: return TOWNS_JISKEY_NUM_DOT;
	case 267: return TOWNS_JISKEY_NUM_SLASH;
	case 268: return TOWNS_JISKEY_NUM_STAR;
	case 269: return TOWNS_JISKEY_NUM_MINUS;
	case 270: return TOWNS_JISKEY_NUM_PLUS;
	case 271: return TOWNS_JISKEY_NUM_RETURN;
	case 272: return TOWNS_JISKEY_NUM_EQUAL;

	/* Misc JIS */
	case 340: return TOWNS_JISKEY_CAPS;       // CAPSLOCK
	case 341: return TOWNS_JISKEY_KANA_KANJI; // KANA
	case 343: return TOWNS_JISKEY_CONVERT;    // HENKAN
	case 344: return TOWNS_JISKEY_NO_CONVERT; // MUHENKAN

	default:  return 0;
	}
}

#endif
