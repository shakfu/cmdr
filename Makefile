# Thin frontend for CMake. Prefer: cmake -S . -B build && cmake --build build
BUILD_DIR ?= build
CMAKE ?= cmake
BUILD_TYPE ?= RelWithDebInfo
CMAKE_FLAGS ?=

.PHONY: all configure check test clean

all: configure
	$(CMAKE) --build $(BUILD_DIR)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_FLAGS)

check: all
	$(CMAKE) --build $(BUILD_DIR) --target check

test: check

clean:
	rm -rf $(BUILD_DIR)
	rm -f cmdr
