BUILD_DIR ?= build
INSTALL_PREFIX ?= $(CURDIR)
PACKAGE_DIR ?= dist/DankSoulverCore
PLUGIN_FILES := plugin.json SoulverLauncher.qml SoulverService.qml SoulverSettings.qml run-helper README.md LICENSE

.PHONY: all configure build test stage package clean

all: build

configure:
	cmake -S . -B $(BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=Release

build: configure
	cmake --build $(BUILD_DIR)

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

stage: build
	cmake --install $(BUILD_DIR) --prefix $(INSTALL_PREFIX)

package: stage
	cmake -E remove_directory $(PACKAGE_DIR)
	cmake -E make_directory $(PACKAGE_DIR)/bin
	cmake -E copy_if_different $(PLUGIN_FILES) $(PACKAGE_DIR)
	cmake -E copy_if_different bin/dank-soulver-core $(PACKAGE_DIR)/bin/dank-soulver-core

clean:
	cmake -E remove_directory $(BUILD_DIR)
	cmake -E remove_directory bin
	cmake -E remove_directory dist
