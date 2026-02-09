#!/usr/bin/env bash
# Download/install optional build tools into tools/
#
# Usage: ./scripts/setup-tools.sh
#
# Tools installed:
#   fteqcc     — QuakeC compiler (used automatically by make run)
#   ericw-tools — Map compiler: qbsp, vis, light (needed for ./scripts/compile-maps.sh)

set -euo pipefail

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BOLD='\033[1m'
RESET='\033[0m'

ok()   { printf "  ${GREEN}✓${RESET} %s\n" "$1"; }
warn() { printf "  ${YELLOW}!${RESET} %s\n" "$1"; }
fail() { printf "  ${RED}✗${RESET} %s\n" "$1"; }

cd "$(dirname "$0")/.."
TOOLS_DIR="$(pwd)/tools"
mkdir -p "$TOOLS_DIR"

OS="$(uname -s)"

printf "\n${BOLD}vkQuake + RmlUI — Tool Setup${RESET}\n\n"

# ── fteqcc (QuakeC compiler) ─────────────────────────────────────

printf "${BOLD}fteqcc${RESET}\n"

case "$OS" in
    Linux)
        FTEQCC="$TOOLS_DIR/fteqcc64"
        if [ -x "$FTEQCC" ]; then
            ok "fteqcc64 already present"
        else
            printf "  Downloading fteqcc64...\n"
            curl -L --fail --progress-bar \
                "https://sourceforge.net/projects/fteqw/files/FTEQCC/fteqcc64/download" \
                -o "$FTEQCC"
            chmod +x "$FTEQCC"
            ok "fteqcc64 downloaded"
        fi
        ;;
    Darwin)
        FTEQCC="$TOOLS_DIR/fteqcc"
        if [ -x "$FTEQCC" ]; then
            ok "fteqcc already present"
        else
            warn "No prebuilt macOS binary available — building from source..."
            FTEQW_TMP="/tmp/fteqw"
            if [ ! -d "$FTEQW_TMP" ]; then
                git clone https://github.com/BryanHaley/fteqw-applesilicon "$FTEQW_TMP"
            fi
            make -C "$FTEQW_TMP/engine" qcc-rel
            cp "$FTEQW_TMP/engine/release/fteqcc" "$FTEQCC"
            chmod +x "$FTEQCC"
            ok "fteqcc built and installed"
        fi
        ;;
    *)
        fail "Unsupported platform: $OS"
        ;;
esac

# ── ericw-tools (map compiler) ───────────────────────────────────

printf "\n${BOLD}ericw-tools${RESET}\n"

case "$OS" in
    Linux)
        if command -v qbsp &>/dev/null; then
            ok "qbsp found in PATH ($(command -v qbsp))"
        else
            warn "Not installed — on Arch Linux: yay -S ericw-tools"
        fi
        ;;
    Darwin)
        if [ -x "$TOOLS_DIR/qbsp" ]; then
            ok "qbsp already present in tools/"
        else
            warn "Not installed — download from https://github.com/ericwa/ericw-tools/releases"
            printf "    Extract qbsp, vis, light, and *.dylib into tools/\n"
        fi
        ;;
esac

# ── Summary ──────────────────────────────────────────────────────

printf "\n${BOLD}Done.${RESET} QuakeC compiles automatically with:\n"
printf "  ${BOLD}make run${RESET}\n\n"
