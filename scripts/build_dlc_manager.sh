#!/usr/bin/env bash
#
# build_dlc_manager.sh -- build the SWOS United DLC Manager (tools/dlc-manager, a submodule:
#                         github.com/SWOS-United/DLC-Manager) and stage it into a drop.
#
#   scripts/build_dlc_manager.sh linux            # -> tools/dlc-manager/build-dist-linux-x86_64/bin/swos-dlc-manager
#   scripts/build_dlc_manager.sh windows          # -> tools/dlc-manager/build_win_static/bin/Release/swos-dlc-manager.exe
#   scripts/build_dlc_manager.sh amiga            # -> tools/dlc-manager/amiga/dlc_manager/out/SWOS-DLC (+ .info)
#   scripts/build_dlc_manager.sh stage-portable DEST   # what SWOS-portable.zip carries, into DEST
#   scripts/build_dlc_manager.sh stage-windows DEST    # what the Windows installer carries, into DEST
#   scripts/build_dlc_manager.sh stage-amiga DRAWER    # SWOS-DLC + its icon, into the SWOS drawer
#   scripts/build_dlc_manager.sh check-windows         # the .exe's DLLs: every one shipped or Windows' own
#
# Exit status: 0 done, 1 failed, 3 a toolchain is missing (a SKIP for build_dist.sh, like `setup`).
# A stage-* with nothing built stages nothing, says so and exits 0: the drop goes out without it.
#
# The manager browses SWOS 2020's DLC catalogue, downloads packs and installs them as mods into
# the player's folder (mods/, enabled in that folder's SWOS.ini [mods]); the game needs nothing
# more. Its builds follow the data setup's (tools/swosdata):
#
#   Linux    CMake + Ninja here, against the system's SDL2/curl/libarchive and Slint's prebuilt C++
#            SDK (fetched once into build-slint-linux-x86_64 -- not the submodule's own
#            third_party/slint, which its fetch_slint.ps1 would fill with the WINDOWS SDK from the
#            same checkout). Built WITHOUT SDL2_mixer (CMAKE_DISABLE_FIND_PACKAGE_PkgConfig: the
#            mixer is found only through pkg-config): the audio preview is optional, and
#            libSDL2_mixer drags in fluidsynth & co., which a machine without it has none of.
#            Shipped beside the game as swos-dlc-manager.x86_64 with DT_RPATH (not RUNPATH: the
#            game's reason, .claude/CLAUDE.md) $ORIGIN/lib/x86_64, so it shares the game's
#            libSDL2 + libdecor there and adds libslint_cpp.so. libcurl, libarchive and
#            libinput (libslint_cpp's) are NOT bundled: on any desktop already (libcurl4 and
#            libarchive13 have many dependants; libinput is the X/Wayland input stack).
#   Windows  scripts/build_dlc_manager.bat through cmd.exe: MSVC + vcpkg (x64-windows-static) + the
#            static Slint the setup uses -- ONE .exe, no DLLs, no VC++ redistributable, like
#            the data setup's window (tools/swosdata/build.bat static). No vcpkg (VCPKG_ROOT) or no static Slint is a SKIP.
#   macOS    scripts/build_macos.sh, on the Mac.
#   Amiga    amiga/dlc_manager's Makefile with the game's m68k toolchain and the Roadshow SDK's
#            netinclude (ROADSHOW_SDK). It goes INSIDE the SWOS drawer beside SWOS2 (it reads and
#            writes PROGDIR:mods/ and PROGDIR:data/MODS -- the game's). NOT in the setup's WHDLoad
#            package: WHDLoad takes the OS away, and the manager is an Intuition/bsdsocket program
#            run from Workbench -- and in that package the game's files are under data/, not
#            beside the program, so its PROGDIR: paths would miss them.
#
# Overrides: SLINT_VERSION (1.17.1), AMIGA_TOOLCHAIN, ROADSHOW_SDK, LINUX_LIBDIR, VCPKG_ROOT and
# SLINT_STATIC_ROOT (Windows side; read by the .bat).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1

DLCM="tools/dlc-manager"
SLINT_VERSION="${SLINT_VERSION:-1.17.1}"
SLINT_LINUX="$DLCM/build-slint-linux-x86_64"   # (build-*: ignored in the submodule)
LINUX_BUILD="$DLCM/build-dist-linux-x86_64"
LINUX_BIN="$LINUX_BUILD/bin/swos-dlc-manager"
LINUX_NAME="swos-dlc-manager.x86_64"
# The game's lib/<arch> (build_dist.sh LINUX_SDL_LIBS): the manager shares them.
LINUX_LIBDIR="${LINUX_LIBDIR:-/usr/lib}"
LINUX_SHARED_LIBS="libSDL2-2.0.so.0 libdecor-0.so.0"
WIN_DIR="$DLCM/build_win_static/bin/Release"
WIN_EXE="$WIN_DIR/swos-dlc-manager.exe"
AMIGA_TOOLCHAIN="${AMIGA_TOOLCHAIN:-/mnt/c/Users/headk/.vscode/extensions/bartmanabyss.amiga-debug-1.8.2/bin/linux}"
ROADSHOW_SDK="${ROADSHOW_SDK:-/mnt/c/Projects/Amiga/Roadshow/Roadshow-SDK-1.8}"
AMIGA_OUT="$DLCM/amiga/dlc_manager/out"

say() { echo "build_dlc_manager: $*"; }
skip() { say "SKIP: $*" >&2; exit 3; }
die() { say "$*" >&2; exit 1; }

[ -f "$DLCM/CMakeLists.txt" ] ||
    die "$DLCM is empty: git submodule update --init $DLCM"

build_linux() {
    [ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || skip "the Linux build is x86_64 Linux only"
    command -v cmake >/dev/null && command -v ninja >/dev/null || skip "no cmake/ninja"
    if [ ! -f "$SLINT_LINUX/lib/cmake/Slint/SlintConfig.cmake" ]; then
        # (scripts/fetch_slint.sh's download, into a folder of its own)
        local url="https://github.com/slint-ui/slint/releases/download/v$SLINT_VERSION/Slint-cpp-$SLINT_VERSION-Linux-x86_64.tar.gz"
        say "fetching $url"
        mkdir -p "$SLINT_LINUX" &&
            curl -fsSL "$url" | tar -xz -C "$SLINT_LINUX" --strip-components=1 ||
            { rm -rf "$SLINT_LINUX"; die "cannot fetch Slint $SLINT_VERSION"; }
    fi
    # '\$ORIGIN': the flag goes through Ninja to /bin/sh, which would expand a bare one to nothing.
    # BUILD_WITH_INSTALL_RPATH: the project's INSTALL_RPATH ($ORIGIN) in the built file, not the
    # build tree's path; the flag adds lib/x86_64 after it.
    cmake -S "$DLCM" -B "$LINUX_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
          -DSWOS_SLINT_ROOT="$ROOT/$SLINT_LINUX" \
          -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON \
          -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON \
          '-DCMAKE_EXE_LINKER_FLAGS=-Wl,--disable-new-dtags -Wl,-rpath,\$ORIGIN/lib/x86_64' >/dev/null ||
        die "cmake configure failed (missing libsdl2-dev / libcurl4-openssl-dev / libarchive-dev?)"
    cmake --build "$LINUX_BUILD" || die "build failed"
    readelf -d "$LINUX_BIN" | grep -q 'RPATH.*\$ORIGIN/lib/x86_64' ||
        die "$LINUX_BIN has no DT_RPATH \$ORIGIN/lib/x86_64"
    say "OK: $LINUX_BIN"
}

build_windows() {
    command -v cmd.exe >/dev/null 2>&1 || skip "no Windows side (cmd.exe)"
    local win; win="$(wslpath -w "$ROOT" 2>/dev/null)" || skip "cannot map $ROOT to a Windows path"
    ( cd /mnt/c && cmd.exe /c "cd /d $win && scripts\\build_dlc_manager.bat" )
    case $? in
        0) ;;
        3) exit 3 ;;   # (the .bat said which toolchain)
        *) die "scripts/build_dlc_manager.bat failed" ;;
    esac
    [ -f "$WIN_EXE" ] || die "$WIN_EXE was not produced"
    check_windows || exit 1
    say "OK: $WIN_EXE"
}

# Every DLL the .exe (and any DLL shipped beside it) imports must be shipped beside it or be
# Windows' own -- and never the VC++ runtime, which a player may not have (the static build links
# it in). Windows' own = in System32 here; objdump reads the PE import tables (no cygcheck: this is
# an MSVC binary, not a Cygwin one -- scripts/check_win_dlls.sh is the game's).
check_windows() {
    [ -f "$WIN_EXE" ] || { say "check-windows: no $WIN_EXE"; return 2; }
    local sys todo="$WIN_EXE" seen=" " bad="" f d
    sys=" $(ls /mnt/c/Windows/System32 2>/dev/null | tr 'A-Z' 'a-z' | tr '\n' ' ') "
    while [ -n "$todo" ]; do
        set -- $todo; f="$1"; shift; todo="$*"
        for d in $(objdump -p "$f" 2>/dev/null | sed -n 's/^\s*DLL Name: //p'); do
            case "$seen" in *" ${d,,} "*) continue ;; esac
            seen="$seen${d,,} "
            case "${d,,}" in
                vcruntime*|msvcp*|concrt*) bad="$bad $d(VC++ runtime)" ;;
                api-ms-win-*|ext-ms-*) ;;
                *) if [ -f "$WIN_DIR/$d" ]; then todo="$todo $WIN_DIR/$d"
                   elif [[ "$sys" != *" ${d,,} "* ]]; then bad="$bad $d"; fi ;;
            esac
        done
    done
    [ -z "$bad" ] || { say "check-windows: $WIN_EXE needs DLLs a player may not have:$bad" >&2; return 1; }
    say "check-windows: every DLL is shipped or Windows' own:$(echo $seen | tr ' ' '\n' | sort | tr '\n' ' ')"
}

build_amiga() {
    local gcc="$AMIGA_TOOLCHAIN/opt/bin/m68k-amiga-elf-gcc"
    [ -x "$gcc" ] || command -v m68k-amiga-elf-gcc >/dev/null 2>&1 ||
        skip "no m68k-amiga-elf-gcc (set AMIGA_TOOLCHAIN)"
    [ -d "$ROADSHOW_SDK/netinclude" ] ||
        skip "no Roadshow SDK at $ROADSHOW_SDK (set ROADSHOW_SDK; the manager's network code needs its netinclude)"
    PATH="$AMIGA_TOOLCHAIN/opt/bin:$AMIGA_TOOLCHAIN:$PATH" \
        make -C "$DLCM/amiga/dlc_manager" ROADSHOW_SDK="$ROADSHOW_SDK" >/dev/null || die "Amiga build failed"
    [ -f "$AMIGA_OUT/SWOS-DLC" ] && [ -f "$AMIGA_OUT/SWOS-DLC.info" ] || die "no $AMIGA_OUT/SWOS-DLC (+ .info)"
    say "OK: $AMIGA_OUT/SWOS-DLC ($(stat -c %s "$AMIGA_OUT/SWOS-DLC") bytes)"
}

stage_linux() {   # DEST
    [ -f "$LINUX_BIN" ] || { say "NOTE: no Linux manager built ($LINUX_BIN)"; return 0; }
    mkdir -p "$1/lib/x86_64" &&
        cp "$LINUX_BIN" "$1/$LINUX_NAME" &&
        cp -L "$SLINT_LINUX/lib/libslint_cpp.so" "$1/lib/x86_64/" &&
        strip --strip-unneeded "$1/lib/x86_64/libslint_cpp.so" || die "cannot stage the Linux manager"
    local l
    for l in $LINUX_SHARED_LIBS; do
        [ -f "$1/lib/x86_64/$l" ] && continue
        cp -L "$LINUX_LIBDIR/x86_64-linux-gnu/$l" "$1/lib/x86_64/$l" ||
            say "NOTE: $l not found: the manager needs a system one" >&2
    done
    say "staged $LINUX_NAME + lib/x86_64/libslint_cpp.so"
}

stage_windows() {   # DEST
    [ -f "$WIN_EXE" ] || { say "NOTE: no Windows manager built ($WIN_EXE)"; return 0; }
    check_windows || die "not staging $WIN_EXE"
    mkdir -p "$1" && cp "$WIN_EXE" "$1/" || die "cannot stage $WIN_EXE"
    # (a DLL beside it, if the build ever stops being static; check_windows has vetted them)
    local f
    for f in "$WIN_DIR"/*.dll; do [ -f "$f" ] && { cp "$f" "$1/" || die "cannot stage $f"; }; done
    say "staged $(basename "$WIN_EXE")"
}

stage_amiga() {   # DRAWER
    [ -f "$AMIGA_OUT/SWOS-DLC" ] || { say "NOTE: no Amiga manager built ($AMIGA_OUT/SWOS-DLC)"; return 0; }
    cp "$AMIGA_OUT/SWOS-DLC" "$AMIGA_OUT/SWOS-DLC.info" "$1/" || die "cannot stage SWOS-DLC"
    say "staged SWOS-DLC + SWOS-DLC.info"
}

case "${1:-}" in
    linux)          build_linux ;;
    windows)        build_windows ;;
    amiga)          build_amiga ;;
    check-windows)  check_windows ;;
    stage-portable) [ -n "${2:-}" ] || die "stage-portable DEST"; stage_linux "$2"; stage_windows "$2" ;;
    stage-windows)  [ -n "${2:-}" ] || die "stage-windows DEST"; stage_windows "$2" ;;
    stage-amiga)    [ -n "${2:-}" ] || die "stage-amiga DRAWER"; stage_amiga "$2" ;;
    *) awk 'NR >= 3 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "${BASH_SOURCE[0]}"; exit 2 ;;
esac
