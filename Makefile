# Thin frontend for CMake. Prefer: cmake -S . -B build && cmake --build build
BUILD_DIR ?= build
CMAKE ?= cmake
BUILD_TYPE ?= RelWithDebInfo
# Extra configure flags, such as -DCMDR_SND=ON -DCMDR_MIDI=ON.
CMAKE_FLAGS ?=
# make install: where, and which optional extensions, such as WITH="snd midi".
PREFIX ?= $(HOME)/.cmdr
WITH ?=

.PHONY: all configure check test install check-format clean

all: configure
	$(CMAKE) --build $(BUILD_DIR)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_FLAGS)

check: all
	$(CMAKE) --build $(BUILD_DIR) --target check

# Every optional extension whose libraries are found, built in build-all, so
# the cmdr in BUILD_DIR keeps its own options.
test:
	$(MAKE) check BUILD_DIR=build-all CMAKE_FLAGS="-DCMDR_SND=AUTO -DCMDR_MIDI=AUTO $(CMAKE_FLAGS)"

# cmdr into PREFIX/bin, and the examples' packages linked into PREFIX/lib.
install:
	python3 scripts/install.py -b $(BUILD_DIR) $(foreach ext,$(WITH),-w $(ext)) $(PREFIX)

# Tracked .cmdr files must be in flat layout. The highlight fixture is not a program.
check-format: all
	git ls-files '*.cmdr' | grep -v '/test/highlight/' | xargs $(BUILD_DIR)/cmdr --fmt --flat --check

clean:
	rm -rf $(BUILD_DIR) build-all
	rm -f cmdr
