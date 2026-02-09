#!/bin/bash
# Compile LibreQuake maps using ericw-tools.
# Usage: LIBREQUAKE_SRC=/path/to/LibreQuake ./scripts/compile-maps.sh [options]
#
# Options (passed to compile_maps.py):
#   -m          Compile all maps
#   -s <map>    Compile a single map (e.g., -s lq_e1m1)
#   -d <dir>    Compile all maps in a directory (e.g., -d src/e1)
#   -c          Clean build artifacts
#   -h          Show help
#
# Examples:
#   LIBREQUAKE_SRC=~/src/LibreQuake ./scripts/compile-maps.sh -m
#   LIBREQUAKE_SRC=~/src/LibreQuake ./scripts/compile-maps.sh -d src/e1
#   LIBREQUAKE_SRC=~/src/LibreQuake ./scripts/compile-maps.sh -s lq_e1m1

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
TOOLS_DIR="$PROJECT_ROOT/tools"

if [[ -z "${LIBREQUAKE_SRC:-}" ]]; then
    echo "Error: map compilation is optional and requires an external LibreQuake source checkout."
    echo "Set LIBREQUAKE_SRC to your LibreQuake repo path."
    echo "Example: LIBREQUAKE_SRC=~/src/LibreQuake ./scripts/compile-maps.sh -m"
    exit 1
fi

MAPS_DIR="$LIBREQUAKE_SRC/lq1/maps"
if [[ ! -d "$MAPS_DIR" ]]; then
    echo "Error: expected maps directory not found: $MAPS_DIR"
    exit 1
fi

# Check for required tools
if [[ ! -x "$TOOLS_DIR/qbsp" ]]; then
    echo "Error: ericw-tools not found in $TOOLS_DIR"
    echo "Download from: https://github.com/ericwa/ericw-tools/releases"
    echo "Extract qbsp, vis, light, and *.dylib to tools/"
    exit 1
fi

# Add tools to PATH and set library path for ericw-tools
export PATH="$TOOLS_DIR:$PATH"
export DYLD_LIBRARY_PATH="$TOOLS_DIR:$DYLD_LIBRARY_PATH"

cd "$MAPS_DIR"

if [[ ! -f compile_maps.py ]]; then
    echo "Error: compile_maps.py not found in $MAPS_DIR"
    exit 1
fi

if [[ $# -eq 0 ]]; then
    echo "Usage: LIBREQUAKE_SRC=/path/to/LibreQuake $0 [options]"
    echo "Run '$0 -h' for help"
    exit 1
fi

python3 compile_maps.py "$@"
