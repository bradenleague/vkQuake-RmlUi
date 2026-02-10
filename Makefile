# vkQuake + RmlUI — Top-level build wrapper
#
# Usage:
#   make          Build everything
#   make run      Build and run (set MOD_NAME to select mod)
#   make smoke    Build and run a startup smoke test (catches crash-on-launch)
#   make engine   Build the engine (+ embedded RmlUI deps)
#   make libs     Alias for engine build (for compatibility)
#   make assemble Ensure id1/ base assets exist
#   make clean    Clean build artifacts only
#   make distclean Clean everything including base assets
#   make format   Auto-format code (clang-format 17, matching CI)
#   make format-check  Check formatting without modifying files
#   make setup    Run first-time setup (deps, rmlui submodule, PAK files)
#   make meson-setup  Re-run meson setup for the engine

# Mod to build/run — override with: make run MOD_NAME=mymod
# If MOD_NAME is not set, runs base game (id1) with no -game flag.
MOD_NAME ?=

QC_SRC := $(if $(MOD_NAME),$(wildcard $(MOD_NAME)/qcsrc/*.qc))
FTEQCC := $(if $(filter Darwin,$(shell uname -s)),tools/fteqcc,tools/fteqcc64)
GAME_FLAG := $(if $(MOD_NAME),-game $(MOD_NAME))

# Formatting — uses clang-format 17 from .venv to match CI
CLANG_FORMAT := .venv/bin/clang-format
FORMAT_DIRS  := Quake Shaders src
FORMAT_EXTS  := h,c,cpp

.PHONY: all libs engine run smoke clean distclean setup meson-setup assemble check-submodules format format-check format-venv

# --- Submodule guard ---
check-submodules:
	@if [ ! -f lib/rmlui/CMakeLists.txt ]; then \
		echo "Error: RmlUI submodule not initialized. Run 'make setup' or 'git submodule update --init'."; \
		exit 1; \
	fi

# --- Top-level targets ---
all: engine

libs: engine

engine: check-submodules build/.configured
	meson compile -C build

# Stamp-based meson setup — re-runs when meson.build or options change
build/.configured: meson.build meson_options.txt
	@if [ -f build/build.ninja ]; then \
		meson setup build . --reconfigure || \
		(rm -rf build && meson setup build .); \
	else \
		meson setup build .; \
	fi
	@touch $@

assemble:
	@if [ ! -d id1 ]; then \
		echo "Error: id1/ not found. Run 'make setup' to download base game assets."; \
		exit 1; \
	fi

# --- QuakeC compilation (skipped if no MOD_NAME, fteqcc not installed, or no qcsrc/) ---
ifneq ($(MOD_NAME),)
$(MOD_NAME)/progs.dat: $(QC_SRC)
	@if [ ! -d "$(MOD_NAME)/qcsrc" ]; then \
		true; \
	elif [ -x "$(FTEQCC)" ]; then \
		echo "Compiling QuakeC ($(MOD_NAME))..."; \
		cd $(MOD_NAME)/qcsrc && ../../$(FTEQCC); \
	else \
		echo "Note: fteqcc not found, skipping QuakeC compilation"; \
	fi

run: all $(MOD_NAME)/progs.dat assemble
	./build/vkquake -basedir . $(GAME_FLAG)
else
run: all assemble
	./build/vkquake -basedir .
endif

# Smoke test — launch the engine, wait enough frames for deferred UI init, then quit.
# Catches startup crashes (segfaults, asserts) that a compile-only check would miss.
WAIT_CMDS := +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait +wait
smoke: all assemble
	@echo "Smoke test: launching engine..."
	@timeout 30 ./build/vkquake -basedir . $(WAIT_CMDS) +quit > /dev/null 2>&1; \
	STATUS=$$?; \
	if [ $$STATUS -eq 0 ]; then \
		echo "Smoke test: PASSED"; \
	else \
		echo "Smoke test: FAILED (exit code $$STATUS)"; \
		exit 1; \
	fi

setup:
	./setup.sh

# --- Formatting (clang-format 17, matching CI) ---
format-venv:
	@if [ ! -x $(CLANG_FORMAT) ]; then \
		echo "Creating .venv with clang-format 17..."; \
		python3 -m venv .venv && .venv/bin/pip install -q clang-format==17.0.6; \
	fi

format: format-venv
	@for dir in $(FORMAT_DIRS); do \
		find $$dir -type f \( -name '*.h' -o -name '*.c' -o -name '*.cpp' \) \
			-exec $(CLANG_FORMAT) -i {} +; \
	done
	@echo "Formatted all files in: $(FORMAT_DIRS)"

format-check: format-venv
	@fail=0; \
	for dir in $(FORMAT_DIRS); do \
		find $$dir -type f \( -name '*.h' -o -name '*.c' -o -name '*.cpp' \) \
			-exec $(CLANG_FORMAT) --dry-run --Werror {} + || fail=1; \
	done; \
	if [ $$fail -eq 1 ]; then \
		echo "Formatting check FAILED — run 'make format' to fix"; \
		exit 1; \
	else \
		echo "Formatting check passed"; \
	fi

meson-setup:
	rm -rf build
	meson setup build .
	@touch build/.configured

clean:
	rm -rf build

distclean: clean
	rm -rf id1
