#!/usr/bin/env bash
set -euo pipefail

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BOLD='\033[1m'
RESET='\033[0m'

ok()   { printf "  ${GREEN}✓${RESET} %s\n" "$1"; }
warn() { printf "  ${YELLOW}!${RESET} %s\n" "$1"; }
fail() { printf "  ${RED}✗${RESET} %s\n" "$1"; }

cd "$(dirname "$0")"

printf "\n${BOLD}vkQuake + RmlUI — Setup${RESET}\n\n"

# ── 1. Check system dependencies ──────────────────────────────────

printf "${BOLD}Checking dependencies...${RESET}\n"

missing=()

check_cmd() {
    if command -v "$1" &>/dev/null; then
        ok "$1"
    else
        fail "$1 — not found"
        missing+=("$1")
    fi
}

check_pkg() {
    if pkg-config --exists "$1" 2>/dev/null; then
        ok "$1 ($(pkg-config --modversion "$1"))"
    else
        fail "$1 — not found"
        missing+=("$1")
    fi
}

check_cmd cmake
check_cmd meson
check_cmd ninja
check_cmd glslangValidator
check_cmd curl
check_cmd unzip
check_cmd pkg-config
check_pkg sdl2
check_pkg vulkan
check_pkg freetype2

if [ ${#missing[@]} -gt 0 ]; then
    printf "\n${RED}Missing dependencies:${RESET} ${missing[*]}\n"

    if command -v pacman &>/dev/null; then
        printf "\nInstall with:\n"
        printf "  ${BOLD}sudo pacman -S cmake meson ninja sdl2 vulkan-devel glslang freetype2 curl unzip${RESET}\n\n"
    elif command -v brew &>/dev/null; then
        printf "\nInstall with:\n"
        printf "  ${BOLD}brew install cmake meson ninja sdl2 molten-vk vulkan-headers glslang freetype${RESET}\n\n"
    fi

    exit 1
fi

# ── 2. Initialize submodules ──────────────────────────────────────

printf "\n${BOLD}Initializing submodules...${RESET}\n"

if [ ! -f lib/rmlui/CMakeLists.txt ]; then
    git submodule update --init --recursive
    ok "RmlUI submodule initialized"
else
    ok "RmlUI submodule already present"
fi

# ── 3. Ensure LibreQuake PAK files ──────────────────────────────

printf "\n${BOLD}Checking LibreQuake assets...${RESET}\n"

PAK_DIR="id1"
LEGACY_PAK_DIR="external/assets/id1"
PAK_URL="https://github.com/lavenderdotpet/LibreQuake/releases/download/v0.09-beta/full.zip"

if [ -f "$PAK_DIR/pak0.pak" ]; then
    ok "PAK files already present"
elif [ -f "$LEGACY_PAK_DIR/pak0.pak" ]; then
    warn "Found legacy PAK path at $LEGACY_PAK_DIR — migrating to $PAK_DIR"
    mkdir -p "$PAK_DIR"
    cp "$LEGACY_PAK_DIR"/pak*.pak "$PAK_DIR"/
    ok "PAK files migrated from legacy path"
else
    warn "PAK files missing — downloading LibreQuake..."
    tmpdir=$(mktemp -d)
    trap "rm -rf '$tmpdir'" EXIT

    curl -L --fail --progress-bar "$PAK_URL" -o "$tmpdir/librequake.zip"
    unzip -q "$tmpdir/librequake.zip" -d "$tmpdir/librequake"

    mkdir -p "$PAK_DIR"
    if ! cp "$tmpdir/librequake/full/id1/pak"*.pak "$PAK_DIR/" 2>/dev/null; then
        fail "PAK files not found in expected zip structure"
        exit 1
    fi
    ok "PAK files downloaded and extracted"
fi

# ── 4. Summary ────────────────────────────────────────────────────

printf "\n${GREEN}${BOLD}Ready!${RESET} Build and run with:\n"
printf "  ${BOLD}make run${RESET}\n\n"
printf "To compile QuakeC or maps, install optional tools with:\n"
printf "  ${BOLD}./scripts/setup-tools.sh${RESET}\n\n"
