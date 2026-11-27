/*
 * united_ui.c -- SWOS United's online screens' own drawing (include/united_ui.h): SWOS 2020's
 * style icons and page arrows. (The API itself is SWOS-API's client: third_party/swos-api.)
 */
#ifdef SWOS_SDL_BUILD
#include "united_ui.h"
#include "display.h"            /* FRONTBUFFER_ROW_STRIDE_BYTES */

/* The buttons' icons: SWOS 2020's mp_game_style_icon_set_v2_16x16_3x1_map (PixelmapAndStrings.h),
 * three 16x16 icons side by side -- the Amiga's tick, PC DOS, XBOX -- each a menu palette index
 * ('0'-'9', 'A'-'G' = 0-16), '.' clear -- SWOS 2020 drew them 32 in from the button's left
 * (draw_mp_game_style_icon_sets); here each goes in its button's icon well. */
static const char *const k_styleIcons[16] = {
    "...........0000...00000.00000...................",
    "...........0AA0...011110011110.......22..22.....",
    "..........0AAA0...017710017770.....1111221111...",
    "..........0AAA0...011110010000.....2177117712...",
    ".........06660....017770011110....221177771122..",
    ".........06660....07000.077770...22221777712222.",
    "........06660...00000.000000000..22217711771222.",
    ".00000..0FFF0...0AAA000AD000FF00.22217122171222.",
    ".0BBB0.0FFF0....0ACCA0DBBA0FGGF0.22177122177122.",
    ".0BBB0.0FFF0....0AC0A0AB0D0FG0G0.21771222217712.",
    "..0BBB0EEE0.....0AC0A0DB0A0GFFF0.21712222221712.",
    "..0DDD0EEE0.....0AC0A0AB0D00GGF0..171222222171..",
    "...0DDEEE0......0AC0A0DB0A0F0GF0..212222222212..",
    "...0DDEEE0......0AAAC0BADB0GFFG0....22222222....",
    "....0EEE0.......0CCC000BB000GG00.....222222.....",
    "....00000.......00000.0000.0000.................",
};

/* SWOS 2020's drawScrollButtons arrows, red: up (cols 0-11) and down (12-23). */
static const char *const k_pageArrows[12] = {
    "..A...........A.........",
    "..A3..........A3........",
    ".AAA..........A3........",
    ".AAA3.........A3........",
    "AAAAA.........A3........",
    "A.A3A3......A.A3A.......",
    "..A3.3......AAAAA3......",
    "..A3.........AAA33......",
    "..A3.........AAA3.......",
    "..A3..........A33.......",
    "..A3..........A3........",
    "...3...........3........",
};

void United_DrawPageArrow(int32_t down, int32_t ox, int32_t oy)
{
    extern volatile uintptr_t _frontbufferBasePtr;
    extern int _menu_draw_x_offset;
    uint8_t *fb = (uint8_t *)_frontbufferBasePtr + _menu_draw_x_offset;
    int32_t x, y;
    for (y = 0; y < 12; y++)
        for (x = 0; x < 12; x++) {
            const char c = k_pageArrows[y][(down ? 12 : 0) + x];
            if (c != '.')
                fb[(uint32_t)(oy + y) * FRONTBUFFER_ROW_STRIDE_BYTES + ox + x] =
                    (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
        }
}

/* Whether buttons show their icons: the one switch for all of them, OPTIONS -> APPEARANCE: MENU
 * BUTTON ICONS ([mod.menus2020] icons: settings_menus.c, mod_menus2020.c), on unless it is off. */
int32_t United_ButtonIcons(void)
{
    extern const char *ModApi_Setting(const char *slug, const char *key, const char *fallback);
    const char *v = ModApi_Setting("menus2020", "icons", "on");
    return !(v && (v[0] == 'o' || v[0] == 'O') && (v[1] == 'f' || v[1] == 'F'));
}

/* One of them as palette indices, 0xFF clear: for a menu's icon well (menu_extras.c,
 * ModApi_SetMenuIconPixels), which shows it over the finished picture. */
void United_StyleIconPixels(int32_t icon, uint8_t out[16 * 16])
{
    int32_t x, y;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++) {
            const char c = icon >= 0 && icon <= 2 ? k_styleIcons[y][icon * 16 + x] : '.';
            out[y * 16 + x] = c == '.' ? 0xFF : (uint8_t)(c <= '9' ? c - '0' : c - 'A' + 10);
        }
}

#endif
