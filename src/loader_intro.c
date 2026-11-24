/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "swos_types.h"
#include "game_types.h"   /* DIYTournamentState (diyFileBufferCopy doubles as the 4KB hero-song staging buffer) */
#include "loader_intro.h"
#include "fade.h"
#include "display.h"
#include "input.h"
#include "audio.h"
#include <string.h>            /* memcpy/memset — both builds (Amiga loader re-stride) */
#ifdef SWOS_SDL_BUILD
#include <stdio.h>             /* (the title track's log line) */
#ifdef SWOS_SDL_BUILD
#include <strings.h>           /* strcasecmp: the intro's own pictures */
#include "platform_sdl.h"      /* MENU_BACKDROP_KEY, Platform_SetIntroBackdrop */
#include "mod_hooks.h"         /* ModHooks_MenuBackdrop: the menus' picture behind the intro */
#include "swos_settings.h"     /* Settings_Save: the pad fire was pressed on at the logo, kept */
#endif
#endif
extern uint8_t _rawAssetLoadBuffer[];   /* overlay/frame staging — both builds */
#ifdef SWOS_SDL_BUILD
extern int  AudioSDL_StartSong(const uint8_t *song_data, int song_size,
                               const uint8_t *ins_data,  int ins_size,
                               int subsong);
extern void AudioSetChannelVolume(uint16_t mask, uint16_t vol);
/* Sizes of the buffers used to stage hero.sng / hero.ins. The actual
 * file payload is smaller; RJP1_Load parses headers internally and
 * uses only what it needs. */
#define HERO_SONG_BUFFER_BYTES   (4 * 1024)        /* diyFileBufferCopyBuf */
#endif

/* ---- External globals (all live in SWOS2.s) ---- */
extern uint16_t _paletteIndex;
extern uint16_t _quitToDos;
extern volatile uint16_t _currentGameTick;
extern void WaitForVSync(void);   /* game_utils.c — pumps audio + events on SDL */
extern uint8_t _pl1Fire;
extern volatile uintptr_t _frontbufferBaseAltPtr;
extern volatile uintptr_t _frontbufferBasePtr;
extern uint32_t _memBoundaryMarker;
extern uint16_t _audioBankLoaded;
extern volatile uint16_t _audioActive;
extern uint16_t _fireResetFlag;
extern volatile uint16_t _keyCount;

/* Harness fast-boot: when non-zero, ShowLoaderIntroSequence returns immediately,
 * skipping the (slow) loader intro. Seeded from SWOS_HARNESS_TURBO in
 * Platform_Init on SDL (the harness always sets that env); gdb-set by
 * amiga_gdb.sh after attach on the Amiga (the target is paused at the entry,
 * before this runs).
 *
 * SDL: kept because platform_sdl.c WRITES it (Platform_Init) — a written global
 * survives. AMIGA: it is only READ (the gdb set is invisible to the compiler),
 * and the m68k LTO/--gc-sections drops a read-only global (used/externally_visible
 * don't save it; `retain` is unsupported on this binutils). So the Amiga harness
 * does NOT use this flag — amiga_gdb.sh skips the intro with a gdb breakpoint +
 * `return` at ShowLoaderIntroSequence instead. This check then just no-ops there
 * (folded away), which is fine. */
__attribute__((used, externally_visible))
volatile uint8_t _skipLoaderIntro = 0;
/* [general] skip_image_reels (SWOS 2020's --no-image-reels; SDL builds set it from SWOS.ini, the
 * Amiga never): the intro is its title picture with the tune alone, until fire -- the pictures
 * before and after it are SWOS 2020's "image reels", the PC loader's equivalent of these. */
volatile uint8_t _skipLoaderReels = 0;
extern volatile uint16_t _currentKey;
/* The title picture: SWOS United's logo (Playa, 2026-11-20: grafs_aga/SWOSUNTD.RAW, the project's
 * own, made by scripts/make_loader_logos.sh, as POWERED.RAW is), where the game had its own
 * (loader1.raw, still read on an OCS build, which has no 4-plane copy of ours). */
#ifdef AGA
#define TITLE_PICTURE GRAFS "swosuntd.raw"
#else
#define TITLE_PICTURE GRAFS "loader1.raw"
#endif
#include "intro_chip_ram.h"     /* the title tune's samples, shared with the sprite pool on the Amiga */
extern uint8_t _aSoundHeroSng[];
extern uint8_t _aSoundHeroIns[];

/* Dedicated 4 KB staging buffer for hero.sng. This used to borrow
 * diyFileBufferCopy (an 8 KB raw BSS buffer at the time). After the step-3
 * untangle diyFileBufferCopy is a ~1 KB typed DIYTournamentState struct, so
 * staging a 4 KB file there would overrun it — hero.sng gets its own buffer. */
static uint8_t heroSongBuffer[4 * 1024];

/* ---- External asm functions (C-callable bridges in swos2_unit.s) ---- */
/* Player1StatusProc declared in input.h */
extern int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);
extern int16_t AssetEnsureReadyWithRetry(uint8_t *name, uint8_t *dest);  /* asset_loader.c */
extern int16_t LoadRNCToBufferNoBlit(uint8_t *name, uint8_t *dest);     /* asset_loader.c */

/* ================================================================
 * LoaderIntroEnsureMenuMusicPlaying  (SWOS2.s line 21035)
 *
 * If audio bank not yet loaded, stops audio and starts menu music
 * from the hero audio bank data. Then applies preset 0 and marks
 * audio as active.
 * ================================================================ */
/* The title track is SWOS United's MP3 (united_title.mp3): it plays on through POWERED BY, to the
 * main menu (LoaderIntro_EndPowered). The Amiga's tune stops at fire -- its samples share memory
 * with the sprites the game loads meanwhile (intro_chip_ram.h). */
static int32_t s_titleStream;

static void loaderIntroEnsureMenuMusicPlaying(void)
{
#ifdef SWOS_SDL_BUILD
    /* SDL build: route through the RJP1 player + SDL audio callback
     * (same mechanism TryPlayMusic uses for the menu song). The Amiga
     * `InitAudioState` is stubbed on SDL because it parses a 68k-BE
     * bank header we don't byte-swap, so calling it here would leave
     * the hero music silent. Hand the loaded hero.sng / hero.ins to
     * the player instead, then ramp the master volume in. */
    if (_audioBankLoaded == 0) {
        StopAllAudioChannels();
        /* HERO has 5 subsongs; the reference rjp1player plays subsong 0
         * (the full mix with vocals). Subsong 1 has the singing channel
         * sequence byte zeroed, so passing 1 here was the cause of the
         * "music plays but no singing" symptom. */
        extern int AudioSDL_StreamStart(const char *name, int loop);
        extern int AudioSDL_PcSongStart(const char *stem, int loop);
        extern int32_t DosMusic_Style(void);
        extern int32_t DosMusic_TitleMidi(void);
        /* SWOS United's title track (Playa, 2026-11-20): daXX's remix of Goalscoring Superstar
         * Hero, sound_dos/united_title.mp3 (from SWOS 2020's music DLC, laid in by the installer,
         * scripts/build_installer.sh), whatever the music style, repeated until fire. Without it:
         * [audio] music = pc: [audio] title_music = midi plays sound_dos/title.xmi (SWOS 2020's
         * floppy title song, from a mod) on the emulated card, else the PC CD release's title
         * song, sound_dos/title.mp3. */
        if (AudioSDL_StreamStart("united_title", 1) == 0) {
            fprintf(stderr, "[music] title track: sound_dos/united_title.mp3\n");
            s_titleStream = 1;
        } else
        if (DosMusic_Style() && DosMusic_TitleMidi() && AudioSDL_PcSongStart("title", 0) == 0) {
        } else
        if (DosMusic_Style() && AudioSDL_StreamStart("title", 0) == 0) {
        } else
        if (AudioSDL_StartSong(heroSongBuffer, HERO_SONG_BUFFER_BYTES,
                               g_introChipRam.titleTuneSamples, (int)sizeof g_introChipRam.titleTuneSamples, 0) != 0) {
            /* Load failed — keep audio silent but mark "loaded" so we
             * don't hammer the player every vblank. */
        }
        _audioBankLoaded = 1;
    }

    AudioSetChannelVolume(0xFFFF, 0);
    _audioActive = 1;
    /* Ramp the master volume from 0 to 0x40 (full) over a few vblanks so the
     * song fades in instead of starting mid-note. SDL plays music at full
     * volume: at 0x40 there is no digital attenuation, so the 8-bit output keeps
     * full level + full precision (matching the reference rjp1player). The Amiga
     * build keeps 0x23 — there the AUDxVOL attenuation is lossless analog. */
    for (uint16_t vol = 0; vol <= 0x3E; vol += 2) {
        AudioSetChannelVolume(0xFFFF, vol);
        WaitForVSync();
    }
    AudioSetChannelVolume(0xFFFF, 0x40);
#else
    if (_audioBankLoaded == 0)
    {
        StopAllAudioChannels();
        InitAudioState(heroSongBuffer, g_introChipRam.titleTuneSamples);
        _audioBankLoaded = 1;
    }
    AudioApplyPresetToChannels(0);
    _audioActive = 1;
#endif
}

/* ================================================================
 * LoaderIntroStopMusicAndResetAudio  (SWOS2.s line 21051)
 *
 * Beeps all channels down to silence, stops audio, and clears
 * the active/loaded flags.
 *
 * SDL deviation: the asm `AudioBeepAllShort` jumps master volume
 * to 0x40 (max) and ramps down to 0 — emitting a brief loud blip
 * that's part of the original Amiga's "click" between intro and
 * menu music. On SDL with our queued audio path that blip lands as
 * a 0x23 → 0x40 step (LOUDER than playback) followed by a long
 * fade. Ramp from the current 0x23 down to 0 instead so the music
 * just fades cleanly on skip. The ramp also keeps AudioSDL_Tick
 * fed so the queue doesn't underrun during the fade.
 * ================================================================ */
static void loaderIntroStopMusicAndResetAudio(void)
{
#ifdef SWOS_SDL_BUILD
    /* fade from where the volume is: full (SDL playback level) when fire leaves the logo; 0 after
     * LoaderIntro_EndPowered's own fade -- starting at full again there jumped the title track back
     * up for a moment as POWERED BY faded into the main menu (Playa, 2026-11-23) */
    extern AudioState _audioState;
    for (int16_t vol = (int16_t)_audioState.channelVolume[0]; vol >= 0; vol--) {
        AudioSetChannelVolume(0xFFFF, (uint16_t)vol);
        WaitForVSync();
        if (_quitToDos) break;
    }
    AudioSetChannelVolume(0xFFFF, 0);
    {   /* (the song itself stopped too: silencing the channels leaves an MP3 title track --
         *  united_title.mp3 repeats -- running unheard, back when the volume is) */
        extern void AudioSDL_StopSong(void);
        AudioSDL_StopSong();
    }
#else
    AudioBeepAllShort();
#endif
    StopAllAudioChannels();
    _audioActive = 0;
    _audioBankLoaded = 0;
}

/* ================================================================
 * LoaderIntroLoadHeroAudioBank  (SWOS2.s line 21021)
 *
 * Loads hero song ("sound/hero.sng") into diyFileBufferCopy and
 * hero instruments ("sound/hero.ins") into audioInstrumentBank.
 * Retries the entire sequence from scratch on any failure.
 * ================================================================ */
static void loaderIntroLoadHeroAudioBank(void)
{
#ifdef SWOS_SDL_BUILD
    /* No Amiga music installed (main.c): nothing to stage. */
    { extern int _gNoAmigaSound; if (_gNoAmigaSound) { _audioBankLoaded = 0; return; } }
#endif
retry:
    _audioBankLoaded = 0;
    if (AssetEnsureReadyWithRetry(_aSoundHeroSng, heroSongBuffer) != 0)
        goto retry;
    if (LoadRNCToBufferNoBlit(_aSoundHeroIns, g_introChipRam.titleTuneSamples) != 0)
        goto retry;
}

#ifdef SWOS_SDL_BUILD
/* Arcade mode skips the loader intro, so the HERO title song is never staged.
 * Load hero.sng / hero.ins and start playback (subsong 0, faded in) on demand —
 * used by the arcade attract loop. Reuses the two intro helpers so the staging
 * buffers and subsong choice stay in one place. SDL-only (arcade is). */
void Arcade_PlayHeroMusic(void)
{
    loaderIntroLoadHeroAudioBank();
    loaderIntroEnsureMenuMusicPlaying();
}
#endif

/* ================================================================
 * LoaderIntroLoadFrameToFrontbufferAlt  (SWOS2.s line 21365)
 *
 * Loads a raw image file into the alternate frontbuffer.
 *
 * Amiga: bitplane data goes straight into the alt frontbuffer
 *   (which is itself bitplane-format) — display picks it up as-is.
 *
 * SDL:   the alt frontbuffer is 8bpp chunky (palette index per
 *   byte) so writing raw bitplane bytes there makes
 *   `chunky_to_rgba` paint garbage. Stage the file through
 *   `rawAssetLoadBuffer` and decode NUM_PLANES interleaved planes
 *   into chunky bytes — same loop LoadOverlayToFrontbuffer uses
 *   for menubg.raw. Frame layout matches the menu BG: PLANE_STRIDE
 *   bytes/plane row, NUM_PLANES planes, decode SCREEN_WIDTH pixels
 *   per scanline.
 * ================================================================ */
static void loaderIntroLoadFrameToFrontbufferAlt(const uint8_t *filename)
{
#ifdef SWOS_SDL_BUILD
    uint32_t frameSize = 0;
    LoadFileEx((uint8_t *)filename, _rawAssetLoadBuffer, &frameSize);

    uint8_t *dstAlt  = (uint8_t *)_frontbufferBaseAltPtr;
    uint8_t *dstBase = (uint8_t *)_frontbufferBasePtr;
    if (!dstAlt) return;

    /* Same full-screen layout as LoadOverlayToFrontbuffer: detect the
     * frame's bitplane depth from its decompressed size (4bpp OCS / 8bpp
     * AGA) so loader frames decode at their own depth, not NUM_PLANES. */
    uint16_t framePlanes    = DetectBankPlanes(frameSize, PLANE_STRIDE, SCREEN_HEIGHT);
    uint32_t frameRowStride = (uint32_t)PLANE_STRIDE * framePlanes;
    memset(dstAlt, 0, FRONTBUFFER_SIZE_TOTAL_BYTES);
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        const uint8_t *rowSrc = _rawAssetLoadBuffer + (size_t)y * frameRowStride;
        uint8_t       *rowDst = dstAlt + (size_t)y * FRONTBUFFER_ROW_STRIDE_BYTES + WIDE_MARGIN;
        for (int px = 0; px < SCREEN_WIDTH; px++) {
            int bit  = 7 - (px & 7);
            int byte = px >> 3;
            uint8_t idx = 0;
            for (int p = 0; p < framePlanes; p++) {
                uint8_t planeByte = rowSrc[p * PLANE_STRIDE + byte];
                idx |= (uint8_t)(((planeByte >> bit) & 1u) << p);
            }
            rowDst[px] = idx;
        }
    }

    /* Mirror alt → base so the post-swap frame the renderer reads is
     * always the decoded image, not whatever stale chunky data was in
     * the other buffer. LoadFileEx does the same mirror when dest is
     * alt; we have to do it ourselves here because we staged through
     * rawAssetLoadBuffer to run the bitplane decode. Without this the
     * intro frame appears for ~1/50 s then gets replaced by the prior
     * buffer contents on the next vsync swap → visible flicker. */
    {   /* SWOS United's own pictures (the logo, POWERED BY) over the menus' picture (Playa,
         * 2026-11-23): their background (index 0, the wide margins too) the menus' key colour, the
         * backdrops mod's picture shown there and faded with them (Platform_SetIntroBackdrop). The
         * game's own frames, and a game without a picture, as they were. */
        const char *slash = strrchr((const char *)filename, '/');
        const char *base = slash ? slash + 1 : (const char *)filename;
        const uint8_t *rgba = NULL;
        int32_t w = 0, h = 0;
        if ((strcasecmp(base, "swosuntd.raw") == 0 || strcasecmp(base, "powered.raw") == 0) &&
            ModHooks_MenuBackdrop(&rgba, &w, &h) && rgba) {
            for (uint32_t i = 0; i < FRONTBUFFER_SIZE_TOTAL_BYTES; i++)
                if (dstAlt[i] == 0)
                    dstAlt[i] = MENU_BACKDROP_KEY;
            Platform_SetIntroBackdrop(rgba, w, h);
        }
    }
    if (dstBase && dstBase != dstAlt) {
        memcpy(dstBase, dstAlt, FRONTBUFFER_SIZE_TOTAL_BYTES);
    }
#else
    /* Amiga: RE-STRIDE the 44-byte-plane-row frame into the ALT frontbuffer's
     * PLANE_STRIDE(52) slots. The frame FILES are authored at the pre-FMODE
     * 44-byte plane row; the AGA widen made the frontbuffer PLANE_STRIDE 52, so
     * the old verbatim `LoadFileEx(..., _frontbufferBaseAltPtr, 0)` misaligned
     * every plane row -> garbled loader/intro screens. Identical fix to
     * LoadOverlayToFrontbuffer; plane count from the decompressed size so 4bpp/
     * 8bpp frames both load. On OCS (PLANE_STRIDE==44) it's a straight copy.
     * docs/AGA.md §11. */
    enum { LOADER_SRC_PLANE_STRIDE = 44 };
    /* AGA FMODE=1 displays static byte-0 content 16px (2 bytes) left of the
     * OCS/FMODE=0 position, so a full-screen loader frame drawn at byte 0 lands
     * 16px left of centre -- the SAME offset the menu foreground gets fixed with
     * in DrawMenu (menu_system.c AGA_MENU_FG_SHIFT_BYTES). Shift the frame right by
     * LOADER_X_SHIFT_BYTES to re-centre it. 0 on OCS. */
#ifdef AGA
    enum { LOADER_X_SHIFT_BYTES = 2 };
#else
    enum { LOADER_X_SHIFT_BYTES = 0 };
#endif
    uint8_t *dstAlt = (uint8_t *)_frontbufferBaseAltPtr;
    if (!dstAlt) return;
    uint32_t frameSize = 0;
    LoadFileEx((uint8_t *)filename, _rawAssetLoadBuffer, &frameSize);
    uint32_t framePlanes = frameSize / ((uint32_t)LOADER_SRC_PLANE_STRIDE * SCREEN_HEIGHT);
    if (framePlanes == 0 || framePlanes > (uint32_t)NUM_PLANES)
        framePlanes = NUM_PLANES;
    /* No full-screen memset (that caused the black-flash); the re-stride overwrites
     * the whole visible area. Per plane row: clear the LOADER_X_SHIFT_BYTES left
     * margin (kept black), then place the 44-byte frame row shifted right by it.
     * Off-screen padding past the frame stays stale but is never displayed. */
    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        const uint8_t *src = _rawAssetLoadBuffer +
                             (size_t)y * LOADER_SRC_PLANE_STRIDE * framePlanes;
        uint8_t *dst = dstAlt + (size_t)y * ROW_STRIDE_BYTES;
        for (uint32_t p = 0; p < framePlanes; p++) {
            uint8_t *pd = dst + (size_t)p * PLANE_STRIDE;
            if (LOADER_X_SHIFT_BYTES) memset(pd, 0, LOADER_X_SHIFT_BYTES);
            memcpy(pd + LOADER_X_SHIFT_BYTES,
                   src + (size_t)p * LOADER_SRC_PLANE_STRIDE,
                   LOADER_SRC_PLANE_STRIDE);
        }
    }
    /* Mirror the decoded frame ALT -> BASE. This is the real intro-flicker fix:
     * the original `LoadFileEx(..., alt)` mirrored to base IMPLICITLY (LoadFileEx
     * copies alt->base when the dest is the alt buffer), but our staged re-stride
     * writes ALT only. The display double-buffer swaps every vblank, so with the
     * frame in ALT alone it strobed between the frame (alt) and the stale/other
     * buffer (base). Copying alt->base puts the frame in BOTH buffers so every swap
     * shows it. (Same reason the SDL branch above mirrors.) */
    {
        uint8_t *dstBase = (uint8_t *)_frontbufferBasePtr;
        if (dstBase && dstBase != dstAlt)
            memcpy(dstBase, dstAlt, FRONTBUFFER_SIZE_TOTAL_BYTES);
    }
#endif
}

/* ================================================================
 * LoaderIntroWaitForFireOrTimeout  (SWOS2.s line 21337)
 *
 * Polls player 1 input each frame. Returns non-zero if fire was
 * pressed or _quitToDos set; returns 0 if the timeout (d7 frames)
 * expired without interruption.
 *
 * Original asm: d7 = timeout count (dbf-style, so iterates d7+1 times).
 * Each iteration: poll input, wait 1 tick.
 * ================================================================ */
/* A mouse click skips an intro picture as fire does (SDL: src/menu_mouse.c, which takes the click
 * so the first menu does not get it too). */
static int32_t intro_clicked(void)
{
#ifdef SWOS_SDL_BUILD
    extern int32_t MenuMouse_TakeClick(void);
    return MenuMouse_TakeClick();
#else
    return 0;
#endif
}

#ifdef AGA
/* The title picture's caption glows (Playa, 2026-11-23): its ink and the fire button's bright red
 * are palette entries 31 and 128, white and red like the logo's own but never its pixels
 * (scripts/make_loader_logos.sh), so their copper colours pulse slowly -- between 70% and full
 * over three seconds -- with now and then a one-step flicker. The picture's fades write them back
 * from the palette (ApplyFadeStep), so nothing needs restoring. */
static int32_t s_titleGlow;
enum { kGlowInk = 31, kGlowRed = 128, kGlowPeriod = 150 };

static void title_glow(uint32_t frame)
{
    extern uint8_t _copperListBuffer[];
    extern void *_paletteTable[];
    static const uint16_t bankValOffsets[8] = {
        COP_PAL_B0_C0_VAL_OFF, COP_PAL_B1_C0_VAL_OFF, COP_PAL_B2_C0_VAL_OFF, COP_PAL_B3_C0_VAL_OFF,
        COP_PAL_B4_C0_VAL_OFF, COP_PAL_B5_C0_VAL_OFF, COP_PAL_B6_C0_VAL_OFF, COP_PAL_B7_C0_VAL_OFF
    };
    static const int16_t kCos16[16] = {   /* 0.5 + 0.5 cos, x 256, a sixteenth of a turn each */
        256, 246, 219, 177, 128, 79, 37, 10, 0, 10, 37, 79, 128, 177, 219, 246
    };
    static uint32_t s_seed = 0x2026u, s_flickerUntil;
    const uint16_t *palette = (const uint16_t *)_paletteTable[_paletteIndex];
    const uint32_t ph = (frame % kGlowPeriod) * 16u * 16u / kGlowPeriod;    /* 0..255 */
    const int32_t c0 = kCos16[ph >> 4], c1 = kCos16[((ph >> 4) + 1) & 15];
    int32_t level = 179 + (77 * (c0 + (c1 - c0) * (int32_t)(ph & 15) / 16)) / 256;   /* 0.7 .. 1 of 256 */
    s_seed = s_seed * 1103515245u + 12345u;
    if (((s_seed >> 16) & 63) == 0)     /* (about once a second: a short dip) */
        s_flickerUntil = frame + 3;
    if (frame < s_flickerUntil)
        level -= 26;
    for (int32_t k = 0; k < 2; k++) {
        const int32_t idx = k ? kGlowRed : kGlowInk;
        const uint16_t c = palette[idx];
        const uint16_t r = (uint16_t)((((c >> 8) & 15) * level + 128) >> 8);
        const uint16_t g = (uint16_t)((((c >> 4) & 15) * level + 128) >> 8);
        const uint16_t b = (uint16_t)(((c & 15) * level + 128) >> 8);
        volatile uint16_t *dst = (volatile uint16_t *)(_copperListBuffer + bankValOffsets[idx >> 5]) + (idx & 31) * 2;
#if defined(__m68k__)
        __asm__ volatile ("move.w %0,(%1)" : : "d"((uint16_t)(r << 8 | g << 4 | b)), "a"(dst) : "memory");
#else
        *dst = (uint16_t)(r << 8 | g << 4 | b);
#endif
    }
}
#endif

static int16_t loaderIntroWaitForFireOrTimeout(int16_t timeout)
{
    int16_t remaining = timeout;
#ifdef AGA
    static uint32_t glowFrame;          /* (on across calls: the logo's wait asks a frame at a time) */
#endif

    while (remaining >= 0)
    {
        uint16_t tick;

        Player1StatusProc();

        if (_pl1Fire != 0 || intro_clicked())
            return 1;
        if (_quitToDos != 0)
            return 1;

#ifdef AGA
        if (s_titleGlow)
            title_glow(glowFrame++);
#endif
        WaitForVSync();

        remaining--;
    }

    return 0;
}

/* ================================================================
 * LoaderIntroShowFrameFadeInOut  (SWOS2.s line 21287)
 *
 * Loads a frame to the alt frontbuffer, fades in, waits for
 * fire/timeout, then fades out (unless interrupted).
 * Returns non-zero if fire/quit interrupted, 0 if timeout.
 * ================================================================ */
static int16_t loaderIntroShowFrameFadeInOut(const uint8_t *filename, int16_t timeout)
{
    if (_quitToDos != 0) return 1;

    loaderIntroLoadFrameToFrontbufferAlt(filename);
#ifndef AGA
    FadeIn();
#endif

    if (loaderIntroWaitForFireOrTimeout(timeout) != 0)
        return 1;

#ifndef AGA
    /* Skip the fade-out if the user closed the window — it just adds
     * ~1 s of unresponsive black-screen between the click and exit. */
    if (_quitToDos == 0)
        FadeOutToBlack();
#endif
    return 0;
}

/* ================================================================
 * LoaderIntroShowFrameWithMusic  (SWOS2.s line 21305)
 *
 * Same as ShowFrameFadeInOut but also ensures menu music is playing
 * before the fade-in.
 * ================================================================ */
static int16_t loaderIntroShowFrameWithMusic(const uint8_t *filename, int16_t timeout)
{
    if (_quitToDos != 0) return 1;

    loaderIntroLoadFrameToFrontbufferAlt(filename);
    loaderIntroEnsureMenuMusicPlaying();
#ifndef AGA
    FadeIn();
#endif

    if (loaderIntroWaitForFireOrTimeout(timeout) != 0)
        return 1;

#ifndef AGA
    if (_quitToDos == 0)
        FadeOutToBlack();
#endif
    return 0;
}

/* ================================================================
 * ShowLoaderIntroSequence  (SWOS2.s line 21170)
 *
 * Displays the loader intro animation sequence: 13 frames with
 * fade in/out effects, music, and fire-button skip support.
 *
 * Flow:
 *   1. Sanity-check memBoundaryMarker address
 *   2. Initialize camera and display
 *   3. Poll fire button (skip intro if pressed)
 *   4. Load hero audio bank
 *   5. Show frames loader00..loader10 with fade in/out
 *   6. Show final loader1.raw frame with extended timeout
 *   7. Stop music and reset audio
 *
 * Early exit paths:
 *   - Fire pressed before frames → jump to loader1.raw, fade in, return
 *   - Load fail on first 2 frames → fade out, show loader1.raw, fade in
 *   - Load fail on later frames → fade out, show loader1.raw, fade in,
 *     then stop music
 * ================================================================ */
#ifdef AGA
/* ================================================================
 * SWOS United's intro (Playa, 2026-11-20), in place of the game's thirteen frames and its disk
 * pictures: its logo with the title track until fire, then (the track stopped) POWERED BY OpenSWOS
 * for two seconds -- shown where the disk pictures were, while the game loads (SWOS.c phase 5:
 * LoaderIntro_ShowPowered), so the load is not a black screen -- then the main menu. Faded in and
 * out on SDL builds (the AGA Amiga cuts, as its frames always did).
 * ================================================================ */
static int32_t s_introShown;            /* the logo was shown: POWERED BY follows it (SDL: the intro runs) */
static int32_t s_poweredShown;          /* POWERED BY is up (SDL: from the start; Amiga: phase 5) */

__attribute__((used, externally_visible))
void ShowLoaderIntroSequence(void)
{
    if (_skipLoaderIntro)               /* harness fast-boot, skip_intro, or no Amiga data: no intro */
        return;
    s_introShown = 1;

    SetCameraCoordinates(0, 0);
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();

    Player1StatusProc();
#ifdef SWOS_SDL_BUILD
    /* SDL: the other way round (2026-11-24) -- POWERED BY first, up while the game loads, the
     * title track starting with it; the logo and PRESS FIRE TO START last, the main menu straight
     * after its fire (LoaderIntro_EndPowered). The Amiga keeps the logo first: its tune's samples
     * share memory with the sprites the load brings in (intro_chip_ram.h). */
    loaderIntroLoadHeroAudioBank();
    loaderIntroEnsureMenuMusicPlaying();
    if (_quitToDos == 0) {              /* (always, as before the swap: INTRO PICTURES is SWOS 2020's reels) */
        enum { kPoweredFrames = 100 };              /* 2 s at the game's 50 Hz, at least */
        s_poweredShown = 1;
        loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)GRAFS "powered.raw");
        FadeIn();
        (void)loaderIntroWaitForFireOrTimeout(kPoweredFrames);   /* (fire: on to the loading) */
    }
    if (!s_titleStream)                 /* (no MP3: the Amiga tune, whose samples the load reuses) */
        loaderIntroStopMusicAndResetAudio();
    _fireResetFlag = 1;
    _keyCount = 0;
    _currentKey = 0;
    return;
#endif
    if (_pl1Fire == 0 && !intro_clicked()) {   /* (fire held at the start: straight to POWERED BY) */
        loaderIntroLoadHeroAudioBank();
        loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)TITLE_PICTURE);
        loaderIntroEnsureMenuMusicPlaying();
        s_titleGlow = 1;                /* (the caption glows: title_glow) */
        while (loaderIntroWaitForFireOrTimeout(0x7FFF) == 0)
            ;                           /* the logo until fire (or the window closed) */
        s_titleGlow = 0;
        if (!s_titleStream || _quitToDos)   /* (the MP3 plays on, through POWERED BY) */
            loaderIntroStopMusicAndResetAudio();
    }
    /* (exit_cleanup's reasons: the fire that left the logo is still held, and still queued; the
     *  boot fades the logo out next, SWOS.c) */
    _fireResetFlag = 1;
    _keyCount = 0;
    _currentKey = 0;
}

/* POWERED BY OpenSWOS, after the logo (SWOS.c phase 5, in the disk pictures' place): faded in and
 * held two seconds; it stays up while the game goes on loading, until the boot fades it out. */
extern volatile uint16_t _fadeState, _fadeCommand;   /* (fade.c) */

int32_t LoaderIntro_PoweredShown(void) { return s_poweredShown; }

/* The main menu is ready (frontend.c, before its music): POWERED BY faded out, and the title track
 * with it, then stopped -- the menu's own music and picture fade in after. */
void LoaderIntro_EndPowered(void)
{
#ifdef SWOS_SDL_BUILD
    if (s_poweredShown && _fadeState != 0 && _fadeCommand == FADE_SENTINEL)
        _fadeCommand = 0xFFFFu;         /* (the picture's fade started: it runs each vblank) */
    if (s_introShown && _quitToDos == 0) {
        /* The logo and PRESS FIRE TO START, last: POWERED BY faded out (the title track playing
         * on), the logo faded in and glowing until fire -- then, with the track, out to the main
         * menu. In the intro's palette, as at the start; a pad's fire claims player 1 here as it
         * did there (InputSDL_SetIntroClaim). */
        extern void InputSDL_SetIntroClaim(int on);
        const uint16_t paletteWas = _paletteIndex;
        if (s_poweredShown)
            FadeOutToBlack();
        _paletteIndex = 0;
        SetCameraCoordinates(0, 0);
        loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)TITLE_PICTURE);
        FadeIn();
        s_poweredShown = 1;             /* (the picture up: faded out below) */
        InputSDL_SetIntroClaim(1);
        _fireResetFlag = 1;             /* (a fire held from POWERED BY is not this one) */
        Player1StatusProc();
        while (_pl1Fire != 0 && _quitToDos == 0) {
            WaitForVSync();
            Player1StatusProc();
        }
        s_titleGlow = 1;
        {   /* The logo until fire -- player 1's, or a new press on any pad: PRESS FIRE TO START is
             * anyone's, also where player 1 is a pad chosen on CONTROLS (the claim above is only
             * for AUTO) -- or the window closed. */
            extern void InputSDL_PadPressBegin(void);
            extern int  InputSDL_PadPressPoll(int only, int *dev, int *button);
            extern int  InputSDL_PlayerDevice(int p);
            extern void InputSDL_GivePlayerDevice(int p, int d);
            extern void InputSDL_DeviceLabel(int d, char *out, int cap);
            extern void InputSDL_StoreConfig(PlatformInitOptions *o);
            extern PlatformInitOptions *g_arcadeOpts;
            extern const char *g_arcadeIniPath;
            int dev = -1, button = -1;
            InputSDL_PadPressBegin();
            while (loaderIntroWaitForFireOrTimeout(0) == 0 && !InputSDL_PadPressPoll(-1, &dev, &button))
                dev = -1;
            /* The pad pressed on is player 1's from now on -- also where CONTROLS had chosen
             * another (a pad, or the keyboard): the one fire is pressed on is the one played
             * with. Kept (SWOS.ini), as a choice on CONTROLS is. */
            extern const char *InputSDL_PlayerPref(int p);
            /* (AUTO: the intro's claim has made it player 1's already, and nothing is kept) */
            if (dev >= 0 && InputSDL_PlayerPref(0)[0] && InputSDL_PlayerDevice(0) != dev && _quitToDos == 0) {
                char name[64];
                InputSDL_GivePlayerDevice(0, dev);
                InputSDL_DeviceLabel(dev, name, sizeof name);
                fprintf(stderr, "[intro] fire on \"%s\": player 1's pad now\n", name);
                if (g_arcadeOpts && g_arcadeIniPath) {
                    InputSDL_StoreConfig(g_arcadeOpts);
                    Settings_Save(g_arcadeOpts, g_arcadeIniPath);
                }
            }
        }
        s_titleGlow = 0;
        InputSDL_SetIntroClaim(0);
        _fireResetFlag = 1;             /* (the fire that left the logo is not the main menu's) */
        _keyCount = 0;
        _currentKey = 0;
        FadeOutToBlack();
        _paletteIndex = paletteWas;
    }
    if (s_titleStream) {
        for (int16_t vol = 0x40; vol >= 0 && _quitToDos == 0; vol--) {
            AudioSetChannelVolume(0xFFFF, (uint16_t)vol);
            WaitForVSync();
        }
        loaderIntroStopMusicAndResetAudio();
        s_titleStream = 0;
    }
#endif
    if (s_poweredShown)
        FadeOutToBlack();               /* (done already, or what is left of it) */
#ifdef SWOS_SDL_BUILD
    Platform_SetIntroBackdrop(NULL, 0, 0);   /* (the menus set their own, as they load) */
#endif
}

void LoaderIntro_ShowPowered(void)
{
    enum { kPoweredFrames = 100 };      /* 2 s at the game's 50 Hz */
#ifdef SWOS_SDL_BUILD
    return;                             /* (SDL: up since the start, ShowLoaderIntroSequence) */
#endif
    if (!s_introShown || _quitToDos != 0)
        return;
    s_poweredShown = 1;
    loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)GRAFS "powered.raw");
    FadeIn();
    for (int32_t i = 0; i < kPoweredFrames && _quitToDos == 0; i++)
        WaitForVSync();
    _fireResetFlag = 1;                 /* (a fire pressed meanwhile is not the main menu's) */
    _keyCount = 0;
    _currentKey = 0;
}

#else   /* OCS: the game's intro */
__attribute__((used, externally_visible))
void ShowLoaderIntroSequence(void)
{
    if (_skipLoaderIntro)            /* harness fast-boot: skip the intro */
        return;

    if ((uintptr_t)&_memBoundaryMarker > 0x100000u) { }

    SetCameraCoordinates(0, 0);
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();
    SwapBuffersAndCameraViewState();
    UpdatePlanePtrsAndBlitParams();

    Player1StatusProc();
    if (_pl1Fire != 0 || intro_clicked())
    {
        /* Fire pressed path: just show loader1.raw and fade in */
#ifndef AGA
        _paletteIndex = 3;
#endif
        loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)TITLE_PICTURE);
        FadeIn();
        return;
    }

    loaderIntroLoadHeroAudioBank();

    if (_skipLoaderReels) {             /* the title alone, as long as the last frame waits */
#ifndef AGA
        _paletteIndex = 3;
#endif
        loaderIntroShowFrameWithMusic((const uint8_t *)TITLE_PICTURE, 0x6D6);
        goto exit_cleanup;
    }

#ifndef AGA
    _paletteIndex = 6;
#endif

    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader00.raw", 0xFA) != 0)
        goto load_fail_early;

    /* Frame 1: loader01.raw */
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader01.raw", 0xFA) != 0)
        goto load_fail_early;

    /* Frame 2: loader1.raw (with music) */
#ifndef AGA
    _paletteIndex = 3;
#endif
    if (loaderIntroShowFrameWithMusic((const uint8_t *)TITLE_PICTURE, 0xFA) != 0)
        goto exit_cleanup;

    /* Frame 3: loader2.raw */
#ifndef AGA
    _paletteIndex = 5;
#endif
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader2.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader76.raw", 0xFA) != 0)
        goto load_fail_mid;
    /* Frames 4-12: loader3..loader10 */
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader3.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader4.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader5.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader6.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader7.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader8a.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader8b.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader9.raw", 0xFA) != 0)
        goto load_fail_mid;
    if (loaderIntroShowFrameFadeInOut((const uint8_t *)GRAFS "loader10.raw", 0xFA) != 0)
        goto load_fail_mid;

    /* Final frame: loader1.raw with extended timeout (0x6D6 = 1750 frames) */
#ifndef AGA
    _paletteIndex = 3;
#endif
    loaderIntroShowFrameFadeInOut((const uint8_t *)TITLE_PICTURE, 0x6D6);
    /* Fall through to exit_cleanup (ignore return value) */

exit_cleanup:
    loaderIntroStopMusicAndResetAudio();
    /* The user pressed ENTER (or whatever fires) to skip the intro and
     * is likely still holding the key when control returns. Without
     * this, the next MenuProc iteration sees fire=1 and immediately
     * fires the highlighted main-menu entry (FRIENDLY GAME by
     * default). fireResetFlag tells MenuCheckControls to suppress
     * fire/shortFire until the key is released, then auto-clears.
     *
     * Also drain the keyboard FIFO. The skip ENTER landed as
     * AKEY_ENTER in keyBuffer when SDL pumped the KEYDOWN event, and
     * nothing called GetKey() during the rest of the loader phases
     * (asset precache / disk-image fade / op3 polling) — so the
     * scancode is still queued when MenuProc finally runs. The
     * `currentKey == 0x0D` path inside MenuCheckControls runs AFTER
     * the fireResetFlag clear and would set fire=1 from that stale
     * scancode, which is what dropped the user into FRIENDLY GAME on
     * intro skip. Zero the count + currentKey so the buffer can't
     * retroactively trigger anything. */
    _fireResetFlag = 1;
    _keyCount = 0;
    _currentKey = 0;
    return;

load_fail_mid:
    /* Fire pressed → show loader1 splash and continue.
     * Window-X pressed → bypass the splash and exit fast. */
    if (_quitToDos == 0) {
        FadeOutToBlack();
#ifndef AGA
        _paletteIndex = 3;
#endif
        loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)TITLE_PICTURE);
        FadeIn();
    }
    goto exit_cleanup;

load_fail_early:
    if (_quitToDos == 0) {
        FadeOutToBlack();
#ifndef AGA
        _paletteIndex = 3;
#endif
        loaderIntroLoadFrameToFrontbufferAlt((const uint8_t *)TITLE_PICTURE);
        FadeIn();
    }
    /* See exit_cleanup for the rationale. */
    _fireResetFlag = 1;
    _keyCount = 0;
    _currentKey = 0;
    return;
}
#endif  /* AGA */
