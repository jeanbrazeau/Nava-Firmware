#!/usr/bin/env bash
# CI-001: Vendor simavr at the pinned commit, build libsimavr.a and hd44780.o
# for native arm64 on macOS.
# Pre-step (consent-gated, run ONCE per clone before invoking this script):
#   git submodule update --init sim/simavr
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SIM_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$SIM_DIR/.." && pwd)"
SIMAVR_DIR="$SIM_DIR/simavr"
BUILD_DIR="$SIM_DIR/build"
VERSION_FILE="$SIM_DIR/simavr.version"

# ---- 1. Read pinned hash ----
if [[ ! -f "$VERSION_FILE" ]]; then
    echo "ERROR: $VERSION_FILE not found." >&2
    exit 1
fi
PINNED_HASH=$(tr -d '[:space:]' < "$VERSION_FILE")
if [[ -z "$PINNED_HASH" ]]; then
    echo "ERROR: $VERSION_FILE is empty." >&2
    exit 1
fi
echo "==> Pinned simavr commit: $PINNED_HASH"

# ---- 2. Verify submodule is populated ----
# Submodule init is a consent-gated pre-step run outside this script; see README.
# Sources live at simavr/simavr/sim/, not simavr/sim/ — the upstream repo nests
# the library one level below the checkout root.
if [[ ! -f "$SIMAVR_DIR/simavr/sim/sim_avr.h" ]]; then
    echo "ERROR: sim/simavr checkout is empty or has an unexpected layout." >&2
    echo "       Expected: $SIMAVR_DIR/simavr/sim/sim_avr.h" >&2
    echo "       Populate with: git clone https://github.com/buserror/simavr.git sim/simavr" >&2
    exit 1
fi

cd "$SIMAVR_DIR"
git fetch --quiet --tags origin

# Checkout exactly the pinned commit
git checkout "$PINNED_HASH" --quiet

ACTUAL_HASH=$(git rev-parse HEAD)
if [[ "$ACTUAL_HASH" != "$PINNED_HASH" ]]; then
    echo "ERROR: HEAD is $ACTUAL_HASH, expected $PINNED_HASH" >&2
    echo "       Update sim/simavr.version with a build-verified hash." >&2
    echo "       Fallback: checkout the v1.7 release tag and record its SHA." >&2
    exit 1
fi
echo "==> Verified commit: $ACTUAL_HASH"

# ---- 2.5 Locate keg-only libelf ----
# libelf is keg-only: brew does NOT symlink it into $(brew --prefix)/include, so
# simavr's own -I$(HOMEBREW_PREFIX)/include/libelf is insufficient and explicit
# keg -I/-L are required.  NOTE: `brew --prefix libelf` prints a path for any
# KNOWN formula whether or not it is installed, so test for a real directory.
LIBELF_PREFIX=$(brew --prefix libelf 2>/dev/null || true)
if [[ -z "$LIBELF_PREFIX" || ! -d "$LIBELF_PREFIX/include" ]]; then
    echo "ERROR: libelf is not installed (checked $LIBELF_PREFIX/include)." >&2
    echo "       Install with: brew install libelf" >&2
    exit 1
fi
LIBELF_INC="-I$LIBELF_PREFIX/include -I$LIBELF_PREFIX/include/libelf"
LIBELF_LIB="-L$LIBELF_PREFIX/lib"
echo "==> libelf prefix: $LIBELF_PREFIX"

# The -I/-L above are necessary but NOT sufficient. simavr gates ELF support on
# a pkg-config probe (Makefile.common: `pkg-config --cflags libelf && echo
# -DHAVE_LIBELF=1`), and without HAVE_LIBELF it still compiles and links
# cleanly - elf_read_firmware() is simply replaced by a stub that reports "ELF
# format is not supported by this build" at run time. The failure therefore
# surfaces as every test failing to load the firmware, with a successful setup.
#
# Two things break that probe on Apple Silicon: pkg-config may not be installed
# at all, and libelf is keg-only so its .pc file is outside the default search
# path. Both are checked here rather than left to produce a silently crippled
# library.
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "ERROR: pkg-config is not installed, so simavr cannot detect libelf" >&2
    echo "       and would build without ELF support." >&2
    echo "       Install with: brew install pkgconf" >&2
    exit 1
fi
export PKG_CONFIG_PATH="$LIBELF_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
if ! pkg-config --exists libelf; then
    echo "ERROR: pkg-config cannot find libelf even with PKG_CONFIG_PATH=" >&2
    echo "       $PKG_CONFIG_PATH" >&2
    exit 1
fi
echo "==> libelf detected by pkg-config: $(pkg-config --modversion libelf)"

# simavr's Makefile.common gates its Darwin branch on $(HOMEBREW_PREFIX)/Cellar
# and defaults to /usr/local, which is wrong on Apple Silicon.
BREW_PREFIX=$(brew --prefix)

# ---- 3. Build libsimavr.a ----
# Extra flags go through the ENVIRONMENT, not the make command line: simavr's
# Makefile.common builds its own CFLAGS with `+=`, and a command-line CFLAGS=
# would override those assignments outright and strip its include paths.
echo "==> Building simavr (native $(clang -dumpmachine))"
CFLAGS="$LIBELF_INC" LDFLAGS="$LIBELF_LIB" \
    make -C "$SIMAVR_DIR" build-simavr RELEASE=1 \
        HOMEBREW_PREFIX="$BREW_PREFIX"

# simavr emits to simavr/obj-<target triple>/ (OBJ := obj-$(CC -dumpmachine)).
SIMAVR_OBJ="$SIMAVR_DIR/simavr/obj-$(clang -dumpmachine)"
if [[ ! -f "$SIMAVR_OBJ/libsimavr.a" ]]; then
    echo "ERROR: libsimavr.a not found at $SIMAVR_OBJ" >&2
    exit 1
fi
mkdir -p "$SIMAVR_DIR/lib"
cp -f "$SIMAVR_OBJ/libsimavr.a" "$SIMAVR_DIR/lib/libsimavr.a"

# Prove ELF support actually made it in. Without HAVE_LIBELF the archive carries
# the stub's error string instead of a working loader, and everything downstream
# builds and links normally - the tests just all fail to load the firmware.
if strings "$SIMAVR_DIR/lib/libsimavr.a" | grep -q "not supported by this build"; then
    echo "ERROR: libsimavr.a was built WITHOUT ELF support (HAVE_LIBELF unset)." >&2
    echo "       Every test would fail with 'cannot read ELF'. This usually means" >&2
    echo "       a stale object tree: remove simavr/obj-* and re-run." >&2
    exit 1
fi
echo "==> libsimavr.a ready (ELF support present)"

# ---- 4. Build HD44780 example part ----
# hd44780.c includes sim_time.h (simavr/sim) and its own hd44780.h (examples/parts).
# This version of the part printf()s every data/command byte to STDOUT, which
# interleaves with and corrupts the tests' TAP stream.  Neutralise printf for
# this translation unit only, via a compile flag — the pinned submodule source
# stays untouched so the vendored checkout remains reproducible.
echo "==> Building HD44780 part"
mkdir -p "$BUILD_DIR"
clang -O2 \
    -Dprintf=nava_hd44780_quiet \
    $LIBELF_INC \
    -I"$SIMAVR_DIR/simavr/sim" \
    -I"$SIMAVR_DIR/examples/parts" \
    -c "$SIMAVR_DIR/examples/parts/hd44780.c" \
    -o "$BUILD_DIR/hd44780.o"
echo "==> hd44780.o ready"

echo ""
echo "==> simavr setup complete."
echo "    Commit : $ACTUAL_HASH"
echo "    Lib    : $SIMAVR_DIR/lib/libsimavr.a"
echo "    Part   : $BUILD_DIR/hd44780.o"
