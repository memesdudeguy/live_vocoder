# Convenience targets (from this cpp/ directory).
# CMake: make prefix  — configure with CMAKE_INSTALL_PREFIX (default ~/.local)
#        PREFIX=/usr/local make prefix
#        make build     — configure then compile
# Inno: ./build-installer.sh — Windows ISCC, or Linux Wine + ~/.wine/.../Inno Setup 6/ISCC.exe

PREFIX ?= $(HOME)/.local
BUILD_DIR ?= build
BUILD_TYPE ?= Release
CMAKE ?= cmake
JOBS ?= $(shell nproc 2>/dev/null || echo 4)

.PHONY: dist-installer-folder inno prefix configure build setup clean

# Configure / reconfigure build tree with install prefix
prefix configure:
	"$(CMAKE)" -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE="$(BUILD_TYPE)" -DCMAKE_INSTALL_PREFIX="$(PREFIX)"

build: prefix
	"$(CMAKE)" --build "$(BUILD_DIR)" -j$(JOBS)

# One-shot: configure with PREFIX then compile
setup: build

clean:
	"$(CMAKE)" -E rm -rf "$(BUILD_DIR)"

dist-installer-folder:
	mkdir -p dist-installer

inno: dist-installer-folder
	./build-installer.sh
