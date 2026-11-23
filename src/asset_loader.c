/*
 * Amiga SWOS RE Project
 * SWOS Amiga C/68000 Reversed by Ben Baker (https://github.com/benbaker76)
 * SWOS DOS Reversed by Zlatko Karakaš (https://github.com/zlatkok)
 */
#include "asset_loader.h"
#include "display.h"
#include "swos_types.h"

/* ---- External globals (all live in SWOS2.s) ---- */
extern uintptr_t _assetLoaderWorkPtr;
extern void WaitForVSync(void);   /* game_utils.c — pumps audio + events on SDL */
/* Head of the cache linked-list. Storage retyped to uintptr_t so that on
 * 64-bit hosts (LP64 / LLP64) a real pointer round-trips. */
extern uintptr_t _assetLoaderCacheFlag;
extern uint16_t _assetLoaderAltPathReady;
extern uint32_t _loaderMemTop;
extern uint32_t _loaderBankBitmap;
extern uint8_t _surnameBuf[];
extern uint8_t assetLoaderMainRegion[];
extern uint8_t assetLoaderBank0Base[];
extern uint16_t _assetCurrentIndex;
extern volatile uint16_t _currentGameTick;

/* ---- External asm functions (C-callable bridges in swos2_unit.s) ---- */
extern void PanicHaltLoop(void);
extern volatile uint16_t _panicReasonCode;
extern int16_t CAssetSys_Dispatch(uint16_t opcode, uint16_t param, void *name, void *dest);
extern int16_t CAssetSys_DispatchGetSize(uint16_t opcode, uint16_t param, void *name, void *dest, uint32_t *outFileSize);
extern void RncDecompressV1(uint8_t *src, uint8_t *dest);
extern int16_t LoadFileEx(uint8_t *filename, uint8_t *dest, uint32_t *outFileSize);

/* ---- Constants matching asm EQU ---- */
#define ASSET_OP_SELECT_AND_OPEN    14
#define ASSET_OP_INIT_SOURCE0       2
#define ASSET_OP_5_THEN_MAYBE_6     12
#define ASSET_OP_OPEN_ANY_SOURCE    16
#define FPS_50HZ                    49
#include "rnc_magic.h"

/* ---- Cache node layout constants ----
 *
 * 32-bit (matches Amiga m68k layout exactly):
 *   offset 0:  next pointer (4 bytes)
 *   offset 4:  isFree flag (uint16_t) — 0 = in-use, 1 = free
 *   offset 6:  blockSize (uint32_t) — allocated block size (even-aligned)
 *   offset 10: dataSize (uint32_t) — actual data size stored
 *   offset 14: name (null-terminated asset name)
 *   offset 48: payload data begins
 *
 * 64-bit (next widened to 8 bytes, every other field shifted +4):
 *   offset 0:  next pointer (8 bytes)
 *   offset 8:  isFree
 *   offset 10: blockSize
 *   offset 14: dataSize
 *   offset 18: name
 *   offset 48: payload data begins
 *
 * The 48-byte header has plenty of slack in the name field, so on 64-bit
 * the layout stays inside the same header window — splits at
 * node + CACHE_NODE_HEADER_SIZE + alignedSize remain valid. */
#define CACHE_NODE_HEADER_SIZE  48
#if UINTPTR_MAX == 0xFFFFFFFFu
  #define CACHE_OFF_ISFREE    4
  #define CACHE_OFF_BLOCKSIZE 6
  #define CACHE_OFF_DATASIZE  10
  #define CACHE_OFF_NAME      14
#else
  #define CACHE_OFF_ISFREE    8
  #define CACHE_OFF_BLOCKSIZE 10
  #define CACHE_OFF_DATASIZE  14
  #define CACHE_OFF_NAME      18
#endif

/* Read/write the "next" field as a real native pointer. On both 32-bit
 * (4-byte slot) and 64-bit (8-byte slot) the slot is wide enough to
 * round-trip a pointer, so a direct (uint8_t **) cast is correct. */
static inline uint8_t *cache_node_next(const uint8_t *node)
{
    return *(uint8_t *const *)node;
}
static inline void cache_node_set_next(uint8_t *node, uint8_t *next)
{
    *(uint8_t **)node = next;
}

/* ---- Forward declarations ---- */
extern void InstallAndInitLoaderBootstrap(void);

/* ================================================================
 * assetCacheAllocInternal  (SWOS2.s line 9831, AssetCacheAlloc)
 *
 * Allocates a cache node from the free list.
 * Walks the linked list looking for a free node with enough space.
 * Splits the node if remaining space can hold another node + header.
 *
 * Returns pointer to allocated node, or NULL on failure.
 * The node's isFree flag is set to 0 (in-use), blockSize and dataSize
 * are written. assetLoaderWorkPtr is decremented.
 * ================================================================ */
static uint8_t *assetCacheAllocInternal(uint32_t size)
{
    uint8_t *node;
    uint32_t alignedSize;
    int32_t remaining;
    uint8_t *splitNode;

    /* Align size up to even */
    alignedSize = (size + 1u) & 0xFFFFFFFEu;

    node = (uint8_t *)_assetLoaderCacheFlag;
    if (node == (uint8_t *)0)
        return (uint8_t *)0;

    for (;;)
    {
        /* Only consider free nodes */
        if (*(uint16_t *)(node + CACHE_OFF_ISFREE) != 0)
        {
            /* Check if block is large enough */
            if (*(uint32_t *)(node + CACHE_OFF_BLOCKSIZE) >= alignedSize)
            {
                /* Found a suitable block — claim it */
                remaining = (int32_t)(*(uint32_t *)(node + CACHE_OFF_BLOCKSIZE) - alignedSize - CACHE_NODE_HEADER_SIZE);

                *(uint32_t *)(node + CACHE_OFF_BLOCKSIZE) = alignedSize;
                *(uint32_t *)(node + CACHE_OFF_DATASIZE)  = size;
                *(uint16_t *)(node + CACHE_OFF_ISFREE)    = 0;

                /* Split: create a new free node from the remainder */
                if (remaining >= 0)
                {
                    splitNode = node + CACHE_NODE_HEADER_SIZE + alignedSize;
                    cache_node_set_next(splitNode, cache_node_next(node));  /* new->next = old->next */
                    cache_node_set_next(node, splitNode);                    /* old->next = new */
                    *(uint32_t *)(splitNode + CACHE_OFF_BLOCKSIZE) = (uint32_t)remaining;
                    *(uint16_t *)(splitNode + CACHE_OFF_ISFREE)    = 1;
                }

                _assetLoaderWorkPtr -= alignedSize;
                return node;
            }
        }

        /* Advance to next node */
        if (cache_node_next(node) == (uint8_t *)0)
            return (uint8_t *)0;

        node = cache_node_next(node);
    }
}

/* ================================================================
 * cacheNodeFindByName  (internal helper)
 *
 * Searches the cache linked list for an in-use node matching name.
 * Returns pointer to the node (not payload), or NULL if not found.
 * On success, *outDataSize is set to the node's dataSize.
 * ================================================================ */
static uint8_t *cacheNodeFindByName(const uint8_t *name, uint32_t *outDataSize)
{
    uint8_t *node;
    const uint8_t *s1;
    const uint8_t *s2;

    node = (uint8_t *)_assetLoaderCacheFlag;
    if (node == (uint8_t *)0)
        return (uint8_t *)0;

    for (;;)
    {
        /* Only check in-use nodes (isFree == 0) */
        if (*(uint16_t *)(node + CACHE_OFF_ISFREE) == 0)
        {
            /* Compare name */
            s1 = name;
            s2 = node + CACHE_OFF_NAME;

            for (;;)
            {
                if (*s1 != *s2)
                    break;
                if (*s1 == 0)
                {
                    /* Match found */
                    *outDataSize = *(uint32_t *)(node + CACHE_OFF_DATASIZE);
                    return node;
                }
                s1++;
                s2++;
            }
        }

        /* Advance to next node */
        if (cache_node_next(node) == (uint8_t *)0)
            return (uint8_t *)0;

        node = cache_node_next(node);
    }
}

/* ================================================================
 * AssetCacheFindByName  (SWOS2.s line 9878)
 *
 * Searches the asset cache for a named entry.
 * Returns the data size on success, or -1 (0xFFFFFFFF) if not found.
 * ================================================================ */
__attribute__((used, externally_visible))
int32_t AssetCacheFindByName(const uint8_t *name)
{
    uint32_t dataSize;
    uint8_t *node = cacheNodeFindByName(name, &dataSize);
    if (node == (uint8_t *)0)
        return (int32_t)-1;
    return (int32_t)dataSize;
}

/* The cached game data files (data/...: team files, the pool, the cups' .TMD) forgotten, their
 * blocks free again: the next read takes each from the disk, through the mods -- a world or season
 * switch's (world_switch.c). Every LoadFileEx may cache what it read (file_io.c), so a season's
 * UEFACUP.TMD stayed and the next season's draw read it. The graphics and sounds stay cached. */
void AssetCache_ForgetData(void)
{
    uint8_t *node = (uint8_t *)_assetLoaderCacheFlag;
    while (node) {
        const uint8_t *n = node + CACHE_OFF_NAME;
        if (*(uint16_t *)(node + CACHE_OFF_ISFREE) == 0 && (n[0] | 0x20) == 'd' && (n[1] | 0x20) == 'a' &&
            (n[2] | 0x20) == 't' && (n[3] | 0x20) == 'a' && (n[4] == '/' || n[4] == '\\')) {
            node[CACHE_OFF_NAME] = 0;
            *(uint16_t *)(node + CACHE_OFF_ISFREE) = 1;
        }
        node = cache_node_next(node);
    }
}

/* ================================================================
 * AssetCacheCopyByName  (SWOS2.s line 9910)
 *
 * Finds a cached asset by name and copies its payload to dest.
 * Returns 0 on success (Z flag set), -1 on not found (Z flag clear).
 * If outFileSize is non-NULL, stores the data size there.
 *
 * The asm version uses word-copy for aligned data <= 128KB, falling
 * back to byte-copy otherwise. We use byte-copy for simplicity.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
int16_t AssetCacheCopyByName(uint8_t *name, uint8_t *dest, uint32_t *outFileSize)
{
    uint32_t dataSize;
    uint8_t *node;
    uint8_t *src;
    uint32_t count;

    node = cacheNodeFindByName(name, &dataSize);
    if (node == (uint8_t *)0)
        return -1;

    if (outFileSize)
        *outFileSize = dataSize;

    src = node + CACHE_NODE_HEADER_SIZE;
    count = dataSize;

    /* Copy payload to destination */
    while (count != 0)
    {
        *dest++ = *src++;
        count--;
    }

    return 0;
}

/* ================================================================
 * AssetCacheLoadFromAssetSys  (SWOS2.s line 9947)
 *
 * Loads an asset from the asset system into a new cache node.
 * 1. Queries file size via ASSET_OP_5_THEN_MAYBE_6
 * 2. Allocates a cache node for the file
 * 3. Loads file data into node payload via ASSET_OP_INIT_SOURCE0
 * 4. Copies asset name into node
 *
 * Returns: 0 = success, 1 = alloc failed, 0xFF = asset system error
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
int16_t AssetCacheLoadFromAssetSys(const uint8_t *name)
{
    uint32_t fileSize;
    int16_t result;
    uint8_t *node;
    uint8_t *payload;
    uint8_t *nameField;
    const uint8_t *srcName;

    /* Query file size */
    result = CAssetSys_DispatchGetSize(ASSET_OP_5_THEN_MAYBE_6, 0, (void *)name, (void *)0, &fileSize);
    if (result != 0) {
#ifdef SWOS_SDL_BUILD
        SWOS_DBG("[cacheload] %s: size-query failed r=%d\n", (const char *)name, result);
#endif
        return (int16_t)0xFF;
    }

    /* Allocate cache node */
    node = assetCacheAllocInternal(fileSize);
    if (node == (uint8_t *)0) {
#ifdef SWOS_SDL_BUILD
        SWOS_DBG("[cacheload] %s: alloc failed for %u bytes\n", (const char *)name, fileSize);
#endif
        return 1;
    }

    /* Load file data into node payload */
    payload = node + CACHE_NODE_HEADER_SIZE;
    result = CAssetSys_Dispatch(ASSET_OP_INIT_SOURCE0, 0, (void *)name, payload);
    if (result != 0) {
#ifdef SWOS_SDL_BUILD
        SWOS_DBG("[cacheload] %s: load failed r=%d\n", (const char *)name, result);
#endif
        return (int16_t)0xFF;
    }

    /* Copy name into node */
    nameField = node + CACHE_OFF_NAME;
    srcName = name;
    do
    {
        *nameField++ = *srcName;
    } while (*srcName++ != 0);

    return 0;
}

/* ================================================================
 * AssetCacheStoreFromMemory  (SWOS2.s line 9980)
 *
 * Stores a memory buffer into a new cache node.
 * 1. Allocates a cache node for the data
 * 2. Copies payload from src to node
 * 3. Copies asset name into node
 *
 * Silently returns if allocation fails.
 * ================================================================ */
__attribute__((used, externally_visible, optimize("no-tree-loop-distribute-patterns")))
void AssetCacheStoreFromMemory(uint8_t *name, uint8_t *src, uint32_t size)
{
    uint8_t *node;
    uint8_t *payload;
    uint8_t *nameField;
    uint32_t count;

    node = assetCacheAllocInternal(size);
    if (node == (uint8_t *)0)
        return;

    /* Copy payload data */
    payload = node + CACHE_NODE_HEADER_SIZE;
    count = size;
    while (count != 0)
    {
        *payload++ = *src++;
        count--;
    }

    /* Copy name into node */
    nameField = node + CACHE_OFF_NAME;
    do
    {
        *nameField++ = *name;
    } while (*name++ != 0);
}

/* ================================================================
 * AssetLoaderRegisterRegion  (SWOS2.s line 10261)
 *
 * Registers a memory region in the asset cache linked list.
 * - size: total byte size of region (must be even)
 * - region: pointer to region base
 *
 * Cache node layout (48-byte header):
 *   offset 0: next pointer (uint32_t)
 *   offset 4: flags/status (uint16_t) — set to 1 for "available"
 *   offset 6: usable size (uint32_t)
 *   offset 10+: data length / payload / name
 * ================================================================ */
__attribute__((used, externally_visible))
void AssetLoaderRegisterRegion(uint32_t size, uint8_t *region)
{
    uint8_t **cacheHead;
    uint8_t *node;

    /* Panic if size is odd */
    if (size & 1u)
    {
        _panicReasonCode = 0x2332u;
        PanicHaltLoop();
        /* never returns */
    }

    /* Subtract 48-byte node header */
    if (size <= 48u)
        return;
    size -= 48u;

    /* Accumulate total available space */
    _assetLoaderWorkPtr += size;

    /* Walk linked list to find tail */
    node = (uint8_t *)_assetLoaderCacheFlag;
    if (node == (uint8_t *)0)
    {
        /* First node — set cache head */
        _assetLoaderCacheFlag = (uintptr_t)region;
    }
    else
    {
        /* Walk to end of list */
        while (cache_node_next(node) != (uint8_t *)0)
        {
            node = cache_node_next(node);
        }
        /* Append new node */
        cache_node_set_next(node, region);
    }

    /* Initialize node header */
    cache_node_set_next(region, (uint8_t *)0);
    *(uint32_t *)(region + CACHE_OFF_BLOCKSIZE) = size;
    *(uint16_t *)(region + CACHE_OFF_ISFREE)    = 1;  /* free */
}

/* ================================================================
 * AssetLoaderInitAltPathAndBanks  (SWOS2.s line 10225)
 *
 * Registers memory regions for the asset cache.
 * Original game used post-BSS free memory + bank scanner.
 * Port: allocates cache memory at runtime via AllocMem since
 * BSS is hunk-allocated with no guaranteed free space after it.
 * ================================================================ */
/* Pre-allocated cache memory (set in _start before hardware takeover) */
extern uint8_t *_earlyAllocCacheMem;
extern uint32_t _earlyAllocCacheSize;

__attribute__((used, externally_visible))
void AssetLoaderInitAltPathAndBanks(void)
{
    _assetLoaderAltPathReady = 1;
    _assetLoaderCacheFlag = 0;

    /* Register surnameBuf as placeholder (size 0 → will be skipped inside) */
    AssetLoaderRegisterRegion(0, _surnameBuf);

    /* Use cache memory that was pre-allocated in _start (before hardware takeover) */
    if (_earlyAllocCacheMem != (uint8_t *)0 && _earlyAllocCacheSize > 0)
    {
        AssetLoaderRegisterRegion(_earlyAllocCacheSize, _earlyAllocCacheMem);
    }
}

#ifdef SWOS_SDL_BUILD
/* A "sound/..." name while no Amiga music or effects are installed (main.c). */
static int isMissingAmigaSound(const uint8_t *name)
{
    extern int _gNoAmigaSound;
    static const char kPrefix[] = "sound/";
    int i;
    if (!_gNoAmigaSound)
        return 0;
    for (i = 0; kPrefix[i]; i++)
        if ((name[i] | 0x20) != kPrefix[i] && name[i] != kPrefix[i])
            return 0;
    return 1;
}
#endif

/* ---- Precache string tables (duplicated from asm data) ---- */

static const uint8_t * const kCorePrecacheTable[] = {
    (const uint8_t *)"sound/sfx.in2",
    (const uint8_t *)GRAFS "swcpich2.map",
    (const uint8_t *)0  /* sentinel */
};

static const uint8_t * const kMenuPrecacheTable[] = {
    (const uint8_t *)GRAFS "menus.raw",
    (const uint8_t *)GRAFS "menus2.raw",
    (const uint8_t *)GRAFS "cjcgrafs.raw",
    (const uint8_t *)GRAFS "cjcbench.raw",
    (const uint8_t *)GRAFS "cjcbits.raw",
    (const uint8_t *)GRAFS "cjcteam1.raw",
    (const uint8_t *)GRAFS "cjcteam2.raw",
    (const uint8_t *)GRAFS "cjcteam3.raw",
    (const uint8_t *)GRAFS "cjcteamg.raw",
    (const uint8_t *)GRAFS "menubg.raw",
    (const uint8_t *)GRAFS "swcpich1.map",
    (const uint8_t *)GRAFS "swcpich3.map",
    (const uint8_t *)GRAFS "swcpich4.map",
    (const uint8_t *)GRAFS "swcpich5.map",
    (const uint8_t *)GRAFS "swcpich6.map",
    (const uint8_t *)"data/poolplyr.dat",
    (const uint8_t *)GRAFS "soccer_s.raw",
    (const uint8_t *)"sound/menu.sng",
    (const uint8_t *)"sound/menu.ins",
    (const uint8_t *)"sound/sfx.sng",
    (const uint8_t *)"sound/sfx.in1",
    (const uint8_t *)"data/team.008",
    (const uint8_t *)0  /* sentinel */
};

/* ================================================================
 * AssetPrecacheOneCore  (SWOS2.s line 21083)
 *
 * Precaches a single asset: if not already cached, loads it
 * from the asset system repeatedly until successful or empty.
 * ================================================================ */
__attribute__((used, externally_visible))
void AssetPrecacheOneCore(const uint8_t *assetName)
{
    int32_t result;
    int16_t status;

#ifdef SWOS_SDL_BUILD
    /* No Amiga music or effects installed (main.c): nothing to cache. */
    if (isMissingAmigaSound(assetName))
        return;
#endif

    result = AssetCacheFindByName(assetName);

    if (result != (int32_t)0xFFFFFFFFu)
        return; /* already cached */

    /* Load until cached or no more data */
    do
    {
        status = AssetCacheLoadFromAssetSys(assetName);
    } while (status != 0);
}

/* ================================================================
 * AssetPrecacheCore  (SWOS2.s line 21069)
 *
 * Installs loader bootstrap, then precaches all entries in
 * the core precache table (sfx + sprite map).
 * ================================================================ */
__attribute__((used, externally_visible))
void AssetPrecacheCore(void)
{
    const uint8_t * const *entry;

    InstallAndInitLoaderBootstrap();

    if (_assetLoaderCacheFlag == 0)
        return;

    entry = kCorePrecacheTable;
    while (*entry != (const uint8_t *)0)
    {
        AssetPrecacheOneCore(*entry);
        entry++;
    }
}

/* ================================================================
 * AssetPrecacheOneMenu  (SWOS2.s line 21115)
 *
 * Same pattern as AssetPrecacheOneCore but for menu assets.
 * ================================================================ */
__attribute__((used, externally_visible))
void AssetPrecacheOneMenu(const uint8_t *assetName)
{
    int32_t result;
    int16_t status;

#ifdef SWOS_SDL_BUILD
    /* No Amiga music or effects installed (main.c): nothing to cache. */
    if (isMissingAmigaSound(assetName))
        return;
#endif

    result = AssetCacheFindByName(assetName);
    if (result != (int32_t)0xFFFFFFFFu)
        return; /* already cached */

    do
    {
        status = AssetCacheLoadFromAssetSys(assetName);
    } while (status != 0);
}

/* ================================================================
 * AssetPrecacheMenuPack  (SWOS2.s line 21102)
 *
 * Precaches all entries in the menu precache table.
 * ================================================================ */
__attribute__((used, externally_visible))
void AssetPrecacheMenuPack(void)
{
    const uint8_t * const *entry;

    if (_assetLoaderCacheFlag == 0)
        return;

    entry = kMenuPrecacheTable;
    while (*entry != (const uint8_t *)0)
    {
        AssetPrecacheOneMenu(*entry);
        entry++;
    }


}

/* ================================================================
 * AssetSvcLoaderOp  (SWOS2.s line 10023)
 *
 * Selects and opens asset source 1 via AssetSys_Dispatch.
 * Sets assetCurrentIndex = 1 and returns 0 on success.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t AssetSvcLoaderOp(void)
{
    return CAssetSys_Dispatch(ASSET_OP_SELECT_AND_OPEN, 1, (void *)0, (void *)0);
}

/* ================================================================
 * AssetSvcLoaderOp2  (SWOS2.s line 10033)
 *
 * Selects and opens asset source 2 via AssetSys_Dispatch.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t AssetSvcLoaderOp2(void)
{
    return CAssetSys_Dispatch(ASSET_OP_SELECT_AND_OPEN, 2, (void *)0, (void *)0);
}

/* ================================================================
 * AssetSvcLoaderOp3  (SWOS2.s line 10043)
 *
 * Selects and opens asset source 3 via AssetSys_Dispatch.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t AssetSvcLoaderOp3(void)
{
    return CAssetSys_Dispatch(ASSET_OP_SELECT_AND_OPEN, 3, (void *)0, (void *)0);
}

/* ================================================================
 * PlaySong  (SWOS2.s line 9560)
 *
 * Loads an asset file with caching support and retry on failure.
 * If assetCurrentIndex == 4, dispatches directly and returns.
 * Otherwise tries cache first, then disk load.
 * On success, stores to cache if cache is active and alt path ready.
 * On error, retries after 50-tick delay unless assetCurrentIndex == 4.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t PlaySong(uint8_t *name, uint8_t *dest)
{
    uint32_t fileSize;
    int16_t result;
    int16_t i;
    uint16_t tick;

    for (;;)
    {
        if (_assetCurrentIndex == 4)
        {
            return CAssetSys_Dispatch(ASSET_OP_INIT_SOURCE0, 0, name, dest);
        }

        if (_assetLoaderCacheFlag != 0)
        {
            if (AssetCacheCopyByName(name, dest, (uint32_t *)0) == 0)
                return 0;
        }

        result = CAssetSys_DispatchGetSize(ASSET_OP_INIT_SOURCE0, 0, name, dest, &fileSize);
        if (result == 0)
        {
            if (_assetLoaderCacheFlag != 0 && _assetLoaderAltPathReady != 0)
            {
                AssetCacheStoreFromMemory(name, dest, fileSize);
            }
            return 0;
        }

        /* Error: give up if assetCurrentIndex == 4 */
        if (_assetCurrentIndex == 4)
            return result;

        /* Wait 50 ticks before retry */
        for (i = FPS_50HZ; i >= 0; i--) {
            WaitForVSync();
        }
    }
}

/* ================================================================
 * AssetEnsureReadyWithRetry  (SWOS2.s line 9603)
 *
 * Same as PlaySong but without cache store on success.
 * Loads an asset, tries cache first, retries on failure.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t AssetEnsureReadyWithRetry(uint8_t *name, uint8_t *dest)
{
    int16_t result;
    int16_t i;
    uint16_t tick;

    for (;;)
    {
        if (_assetCurrentIndex == 4)
            return CAssetSys_Dispatch(ASSET_OP_INIT_SOURCE0, 0, name, dest);

        if (_assetLoaderCacheFlag != 0)
        {
            if (AssetCacheCopyByName(name, dest, (uint32_t *)0) == 0)
                return 0;
        }

        result = CAssetSys_Dispatch(ASSET_OP_INIT_SOURCE0, 0, name, dest);
        if (result == 0)
            return 0;

        /* Error: give up if assetCurrentIndex == 4 */
        if (_assetCurrentIndex == 4)
            return result;

        
        /* Wait 50 ticks before retry */
        for (i = FPS_50HZ; i >= 0; i--) {
            WaitForVSync();
        }
    }
}

/* ================================================================
 * LoadRNCToBufferNoBlit  (SWOS2.s line 9431)
 *
 * Loads a file via AssetSys_Dispatch and decompresses RNC v1 in-place.
 * On load failure: if assetCurrentIndex != 4, waits 50 ticks then
 * falls back to LoadFile (full file loader with blit).
 * Panics if loaded data is not RNC v1 format.
 * ================================================================ */
__attribute__((used, externally_visible))
int16_t LoadRNCToBufferNoBlit(uint8_t *name, uint8_t *dest)
{
    int16_t result;
    int16_t i;
    uint16_t tick;

    result = CAssetSys_Dispatch(ASSET_OP_INIT_SOURCE0, 0, name, dest);
    if (result != 0)
    {
        /* Load failed */
        if (_assetCurrentIndex == 4)
            return result;

        /* Wait 50 ticks then fall back to LoadFile */
        for (i = 49; i >= 0; i--) {
            WaitForVSync();
        }
        return LoadFileEx(name, dest, (uint32_t *)0);
    }

    /* Check for RNC v1 magic */
    if (*(uint32_t *)dest != RNC_MAGIC_V1)
    {
        _panicReasonCode = 0xE002u;
        PanicHaltLoop();
    }

    /* Decompress RNC v1 in-place */
    RncDecompressV1(dest, dest);
    return 0;
}
