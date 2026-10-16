# Thin frontend for CMake. Prefer: cmake -S . -B build && cmake --build build
BUILD_DIR ?= build
CMAKE ?= cmake
BUILD_TYPE ?= RelWithDebInfo

.PHONY: all configure check clean

all: configure
	$(CMAKE) --build $(BUILD_DIR)

configure:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

check: all
	$(CMAKE) --build $(BUILD_DIR) --target check

clean:
	rm -rf $(BUILD_DIR)
	rm -f cmdr
