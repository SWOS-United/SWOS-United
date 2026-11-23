/* make_loader_logos.c -- one AGA loader picture from a logo (scripts/make_loader_logos.sh).
 *
 *   make_loader_logos <logo.png> <out.RAW> <preview.png> <cx> <cy> <maxw> <maxh> <dither 0|1> [caption charset.RAW
 *                     [font 0|1 [below 0|1 [button.png button_h [glow_ink glow_red]]]]]
 *
 * A 352x272 8-plane RAW, RNC-packed, in the DOS menu palette (the AGA build shows the loader
 * pictures in it, src/fade.c): the logo trimmed of the black around it, fitted into maxw x maxh
 * centred on cx,cy, area-resampled over index 0 (the screen's background, which near-black also
 * becomes), quantised to the palette (dither 1: capped Floyd-Steinberg); with a caption, the text
 * in the menus' small font above it, its glyphs from CHARSET.RAW (ASCII cells 16x8, ink 2, shadow 8).
 * font 1: the menus' big font (the same cells, from row 112 of CHARSET.RAW); below 1: the caption
 * under the logo; "()" in the caption: the button picture there, button_h tall, on the text's middle.
 * glow_ink / glow_red: palette entries the caption's ink and the button's bright red are drawn in
 * instead, which the logo must not use (src/loader_intro.c glows them while the picture waits). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "amiga_raw.h"
#include "rnc_pack.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
extern int32_t Sg_RncUnpack(const SgBuf *in, SgBuf *out, char *err);
enum { W = 352, H = 272 };
static double pr[256], pg[256], pb[256];
static int nearest(double r, double g, double b) {
    int best = 0; double bd = 1e30;
    for (int i = 0; i < 256; i++) {
        double dr = r - pr[i], dg = g - pg[i], db = b - pb[i];
        double rm = (r + pr[i]) / 2, d = (2 + rm / 256) * dr * dr + 4 * dg * dg + (2 + (255 - rm) / 256) * db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
/* A grey of the logo (its silver) among the palette's neutral greys only: by RGB alone the nearest of
 * the menu palette's few mid greys are its pinkish browns (a88c8c, b09090 ...), dark dirty patches
 * in the silver. `skip1`/`skip2`: entries kept for the caption's glow. */
static int nearest_grey(double r, double g, double b, int skip1, int skip2) {
    int best = 3; double bd = 1e30;
    for (int i = 1; i < 256; i++) {
        double mx = fmax(pr[i], fmax(pg[i], pb[i])), mn = fmin(pr[i], fmin(pg[i], pb[i]));
        if (mx - mn > 12 || i == skip1 || i == skip2) continue;
        double dr = r - pr[i], dg = g - pg[i], db = b - pb[i], d = dr * dr + dg * dg + db * db;
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
int main(int argc, char **argv) {
    char err[256]; int w, h, n;
    unsigned char *src = stbi_load(argv[1], &w, &h, &n, 4);
    int cx = atoi(argv[4]), cy = atoi(argv[5]), maxw = atoi(argv[6]), maxh = atoi(argv[7]), dith = atoi(argv[8]);
    const char *caption = argc > 9 ? argv[9] : NULL;
    const int big = argc > 11 && atoi(argv[11]), below = argc > 12 && atoi(argv[12]);
    const int fy = big ? 112 : 0, glyphH = 8, spaceW = big ? 5 : 3;
    int bw = 0, bh = 0, bn; unsigned char *btn = NULL; int bth = 0, btw = 0;
    if (argc > 14) { btn = stbi_load(argv[13], &bw, &bh, &bn, 4); bth = atoi(argv[14]); btw = (int)lround((double)bw * bth / bh); }
    const int glowInk = argc > 16 ? atoi(argv[15]) : -1, glowRed = argc > 16 ? atoi(argv[16]) : -1;
    for (int i = 0; i < 256; i++) { uint32_t c = Sg_AmigaRawDosMenuPal[i]; pr[i] = c >> 16 & 255; pg[i] = c >> 8 & 255; pb[i] = c & 255; }
    /* the black around the logo, and the black panels open to it: background (clear), flooded from
     * the picture's edges through clear and near-black pixels -- a near-black the logo encloses (an
     * outline, an eye) stays, as black. The game shows the menus' picture in the background. */
    {
        int *stack = malloc(sizeof(int) * (size_t)w * h), sp = 0;
        unsigned char *seen = calloc((size_t)w * h, 1);
        #define DARKBG(i) (src[(i) * 4 + 3] <= 16 || (src[(i) * 4] < 28 && src[(i) * 4 + 1] < 28 && src[(i) * 4 + 2] < 40))
        for (int x = 0; x < w; x++) { stack[sp++] = x; stack[sp++] = (h - 1) * w + x; }
        for (int y = 0; y < h; y++) { stack[sp++] = y * w; stack[sp++] = y * w + w - 1; }
        while (sp) {
            int i = stack[--sp];
            if (seen[i] || !DARKBG(i)) continue;
            seen[i] = 1;
            src[i * 4 + 3] = 0;
            int x = i % w, y = i / w;
            if (x > 0 && !seen[i - 1]) stack[sp++] = i - 1;
            if (x < w - 1 && !seen[i + 1]) stack[sp++] = i + 1;
            if (y > 0 && !seen[i - w]) stack[sp++] = i - w;
            if (y < h - 1 && !seen[i + w]) stack[sp++] = i + w;
            if (sp > w * h - 8) break;
        }
        #undef DARKBG
        free(stack); free(seen);
    }
    /* trim: pixels that show (alpha) and are not the black around the logo */
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        unsigned char *p = src + (y * w + x) * 4;
        if (p[3] > 16 && p[0] + p[1] + p[2] > 60) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    }
    int tw = x1 - x0 + 1, th = y1 - y0 + 1;
    /* the caption's glyphs (CHARSET.RAW's small font: ASCII cells 16x8, ink 2, shadow 8) */
    SgImage cs; int capw = 0, caph = 0, glyphw[96] = {0};
    if (caption) {
        SgBuf in, raw;
        if (Sg_ReadFile(argv[10], &in, err) || Sg_RncUnpack(&in, &raw, err) || Sg_AmigaRawDecode(&raw, 320, 256, 4, &cs, err)) { fprintf(stderr, "%s\n", err); return 1; }
        for (const char *c = caption; *c; c++) {
            int k = *c - 32, gx = (k % 16) * 16, gy = (k / 16) * 8, gw = 0;
            if (*c == ' ') { capw += spaceW; continue; }
            if (c[0] == '(' && c[1] == ')' && btn) { capw += btw; c++; continue; }
            gy += fy;
            for (int y = 0; y < 8; y++) for (int x = 0; x < 16; x++) if (cs.px[(gy + y) * 320 + gx + x] && x + 1 > gw) gw = x + 1;
            glyphw[k] = gw; capw += gw + (big ? 1 : 0);
        }
        caph = big ? glyphH : 6;
        if (btn && bth > caph) caph = bth;
    }
    int gap = caption ? (below ? 8 : 4) : 0, room = maxh - (caption ? caph + gap : 0);
    double s = fmin((double)maxw / tw, (double)room / th);
    int ow = (int)lround(tw * s), oh = (int)lround(th * s);
    int top = cy - (oh + (caption ? caph + gap : 0)) / 2, left = cx - ow / 2;
    int logoTop = below ? top : top + (caption ? caph + gap : 0);
    int capTop = below ? top + oh + gap : top;
    printf("logo %dx%d (trimmed %dx%d) at %d,%d; caption %dx%d\n", ow, oh, tw, th, left, logoTop, capw, caph);
    uint8_t *px = calloc(W * H, 1);
    double *er = calloc((size_t)(ow + 2) * 3 * 2, sizeof(double));     /* Floyd-Steinberg rows */
    double *cur = er, *nxt = er + (ow + 2) * 3;
    for (int oy = 0; oy < oh; oy++) {
        memset(nxt, 0, (ow + 2) * 3 * sizeof(double));
        for (int ox = 0; ox < ow; ox++) {
            double fx0 = x0 + ox / s, fx1 = x0 + (ox + 1) / s, fy0 = y0 + oy / s, fy1 = y0 + (oy + 1) / s;
            double r = 0, g = 0, b = 0, a = 0, wt = 0;
            for (int y = (int)fy0; y < (int)ceil(fy1) && y < h; y++) for (int x = (int)fx0; x < (int)ceil(fx1) && x < w; x++) {
                double cw = fmin(x + 1, fx1) - fmax(x, fx0), ch = fmin(y + 1, fy1) - fmax(y, fy0);
                if (cw <= 0 || ch <= 0) continue;
                unsigned char *p = src + (y * w + x) * 4; double al = p[3] / 255.0, k = cw * ch;
                r += p[0] * al * k; g += p[1] * al * k; b += p[2] * al * k; a += al * k; wt += k;
            }
            r /= wt; g /= wt; b /= wt; a /= wt;
            r += (1 - a) * pr[0]; g += (1 - a) * pg[0]; b += (1 - a) * pb[0];   /* over the background */
            double *e = cur + (ox + 1) * 3;
            double rr = r + (dith ? e[0] : 0), gg = g + (dith ? e[1] : 0), bb = b + (dith ? e[2] : 0);
            /* (the black around and inside the logo: the screen's own background, index 0) */
            /* (index 0 is the background ALONE -- the game shows the menus' picture there (src/loader_intro.c);
             *  the logo's own near-black a black of the palette, never 0, or the picture would show through it) */
            int idx = a < 0.5 ? 0 : (r < 28 && g < 28 && b < 40) ? 3 :
                      fmax(r, fmax(g, b)) - fmin(r, fmin(g, b)) < 28 ? nearest_grey(rr, gg, bb, glowInk, glowRed) :
                      nearest(rr, gg, bb);
            if (idx == 0 && a >= 0.5) idx = 3;
            if (glowInk >= 0 && (idx == glowInk || idx == glowRed))     /* (the glow's: the caption's alone) */
                idx = idx == glowInk ? 2 : 10;
            px[(logoTop + oy) * W + left + ox] = (uint8_t)idx;
            if (dith && idx) {
                double d[3] = { rr - pr[idx], gg - pg[idx], bb - pb[idx] };
                for (int k = 0; k < 3; k++)     /* (capped: no streaks where the palette has no near colour) */
                    d[k] = d[k] > 24 ? 24 : d[k] < -24 ? -24 : d[k];
                for (int k = 0; k < 3; k++) {
                    cur[(ox + 2) * 3 + k] += d[k] * 7 / 16; nxt[ox * 3 + k] += d[k] * 3 / 16;
                    nxt[(ox + 1) * 3 + k] += d[k] * 5 / 16; nxt[(ox + 2) * 3 + k] += d[k] * 1 / 16;
                }
            }
        }
        double *t = cur; cur = nxt; nxt = t;
    }
    if (caption && glowInk >= 0) {     /* (the glow's entries: the logo's own pixels never) */
        for (int i = 0; i < W * H; i++)
            if (px[i] == glowInk || px[i] == glowRed) { fprintf(stderr, "glow entry %d is the logo's\n", px[i]); return 1; }
    }
    if (caption) {
        int x = cx - capw / 2;
        const int textTop = capTop + (caph - (big ? glyphH : 6)) / 2;
        for (const char *c = caption; *c; c++) {
            int k = *c - 32, gx = (k % 16) * 16, gy = (k / 16) * 8 + fy;
            if (*c == ' ') { x += spaceW; continue; }
            if (c[0] == '(' && c[1] == ')' && btn) {     /* the button: area-resampled, over index 0 */
                const int by = capTop + (caph - bth) / 2;
                const double sx = (double)bw / btw, sy = (double)bh / bth;
                for (int oy = 0; oy < bth; oy++) for (int ox = 0; ox < btw; ox++) {
                    double r = 0, g = 0, b = 0, a = 0, wt = 0;
                    for (int yy = (int)(oy * sy); yy < (int)ceil((oy + 1) * sy) && yy < bh; yy++)
                        for (int xx = (int)(ox * sx); xx < (int)ceil((ox + 1) * sx) && xx < bw; xx++) {
                            double cw = fmin(xx + 1, (ox + 1) * sx) - fmax(xx, ox * sx), chh = fmin(yy + 1, (oy + 1) * sy) - fmax(yy, oy * sy);
                            if (cw <= 0 || chh <= 0) continue;
                            unsigned char *p = btn + (yy * bw + xx) * 4; double al = p[3] / 255.0, kk = cw * chh;
                            r += p[0] * al * kk; g += p[1] * al * kk; b += p[2] * al * kk; a += al * kk; wt += kk;
                        }
                    r /= wt; g /= wt; b /= wt; a /= wt;
                    r += (1 - a) * pr[0]; g += (1 - a) * pg[0]; b += (1 - a) * pb[0];
                    /* (the palette's reds and whites only -- its nearest by RGB turns a mid red into
                     *  the darkest and the glint orange -- brightened a little: the menu palette has no
                     *  mid red, so the body takes the bright one, the rim the dark) */
                    int idx = 0;
                    if (!(a < 0.3 || (r < 20 && g < 20 && b < 30))) {
                        double bd = 1e30, R = fmin(255, r * 1.35), G = fmin(255, g * 1.35), B = fmin(255, b * 1.35);
                        for (int i = 1; i < 256; i++) {
                            int red = pg[i] < 0.3 * pr[i] && pb[i] < 0.45 * pr[i], white = pr[i] > 200 && pg[i] > 200 && pb[i] > 200;
                            if (!red && !white) continue;
                            double d = (R - pr[i]) * (R - pr[i]) + (G - pg[i]) * (G - pg[i]) + (B - pb[i]) * (B - pb[i]);
                            if (d < bd) { bd = d; idx = i; }
                        }
                    }
                    if (idx && glowRed >= 0 && pr[idx] > 240 && pg[idx] < 16 && pb[idx] < 16)
                        idx = glowRed;          /* (the bright red: glowing) */
                    if (idx) px[(by + oy) * W + x + ox] = (uint8_t)idx;
                }
                x += btw; c++;
                continue;
            }
            for (int y = 0; y < 8; y++) for (int xx = 0; xx < glyphw[k]; xx++) {
                int v = cs.px[(gy + y) * 320 + gx + xx];
                if (v) px[(textTop + y) * W + x + xx] = (uint8_t)(v == 2 && glowInk >= 0 ? glowInk : v);
            }
            x += glyphw[k] + (big ? 1 : 0);
        }
    }
    SgImage img; memset(&img, 0, sizeof img); img.w = W; img.h = H; img.px = px;
    SgBuf raw, packed;
    if (Sg_AmigaRawEncode(&img, 8, &raw, err) || Sg_RncPackLikeSwosGfx(&raw, &packed, err) ||
        Sg_WriteFile(argv[2], packed.data, packed.size, err)) { fprintf(stderr, "%s\n", err); return 1; }
    unsigned char *rgb = malloc(W * H * 3);
    for (int i = 0; i < W * H; i++) { rgb[i*3] = pr[px[i]]; rgb[i*3+1] = pg[px[i]]; rgb[i*3+2] = pb[px[i]]; }
    stbi_write_png(argv[3], W, H, 3, rgb, W * 3);
    printf("%s: %u bytes packed (%u raw)\n", argv[2], (unsigned)packed.size, (unsigned)raw.size);
    return 0;
}
