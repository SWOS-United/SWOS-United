#!/bin/sh
# Build OpenSWOS Setup (and the swosdata CLI) with CMake.
#
#   ./build.sh          configure and build -> build-app/bin/swosunited-setup (PROJECT_SETUP_PROGRAM)
#   ./build.sh static   link Slint statically: one binary, no libslint_cpp.so
#                       -> build-app-static/bin/swosunited-setup (on a Mac,
#                       "OpenSWOS Setup.app", universal: arm64 + x86_64)
#   ./build.sh clean    remove both build directories
#
# Slint is needed at BUILD time (slint-compiler turns the .slint files into
# C++). Without SLINT_ROOT, CMake fetches the prebuilt SDK into .deps/ on the
# first configure (cmake/FetchSlint.cmake).
#
#   SLINT_ROOT=/path/to/sdk ./build.sh            use an SDK installed elsewhere
#   SLINT_STATIC_ROOT=/path ./build.sh static     where the static Slint lives
#   BUILD_DIR=~/build/openswos-setup ./build.sh   build somewhere else (a Linux
#                                                 path builds faster under WSL)
#
# The Makefile still builds the CLI and the tests alone (make -C tools/swosdata).
set -e
cd "$(dirname "$0")"

MODE=shared
case "$1" in
  clean) rm -rf build-app build-app-static; echo "removed build-app/ and build-app-static/"; exit 0 ;;
  static) MODE=static ;;
  "") ;;
  *) echo "usage: $0 [static|clean]" >&2; exit 2 ;;
esac

# The static tree is separate rather than a switch inside one: Slint_DIR is
# cached, so pointing SLINT_ROOT at another SDK in a configured tree is ignored.
if [ "$MODE" = static ]; then
  : "${BUILD_DIR:=build-app-static}"
  if [ "$(uname -s)" = Darwin ]; then
    # One app for both kinds of Mac: Slint built for arm64 and x86_64, its two
    # libslint_cpp.a joined with lipo (the slint-compiler is the arm64 one).
    : "${SLINT_STATIC_ROOT:=$PWD/../../../slint/install/macos-universal-static}"
  else
    : "${SLINT_STATIC_ROOT:=$PWD/../../../slint/install/linux-x86_64-static}"
  fi
  if [ ! -f "$SLINT_STATIC_ROOT/lib/libslint_cpp.a" ]; then
    echo "No static Slint at $SLINT_STATIC_ROOT." >&2
    if [ "$(uname -s)" = Darwin ]; then
      echo "Build it with ./build_slint_macos.sh (once per Mac), or set SLINT_STATIC_ROOT." >&2
    else
      echo "Build one from a Slint 1.17.1 checkout with BUILD_SHARED_LIBS=OFF (see" >&2
      echo "StorageAgent's docs/SLINT_STATIC.md), or set SLINT_STATIC_ROOT." >&2
    fi
    exit 1
  fi
  SLINT_ROOT="$SLINT_STATIC_ROOT"
else
  : "${BUILD_DIR:=build-app}"
fi

set -- -DCMAKE_BUILD_TYPE=Release
[ -n "$SLINT_ROOT" ] && set -- "$@" "-DSLINT_ROOT=$SLINT_ROOT"
# A static Slint on a Mac is universal, so the app is too.
[ "$MODE" = static ] && [ "$(uname -s)" = Darwin ] && set -- "$@" "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"

cmake -S . -B "$BUILD_DIR" "$@"
cmake --build "$BUILD_DIR" -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

echo
echo "Built in $BUILD_DIR/bin:"
ls -1 "$BUILD_DIR/bin"
