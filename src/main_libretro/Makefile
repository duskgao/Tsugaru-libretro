# ============================================================================
#  Tsugaru (FM Towns) libretro core - standard Makefile entry
#
#  This is the entry point used by libretro-super / RetroArch buildbot.
#  It wraps the real CMake build (build_libretro.sh) so that the standard
#  `make -f Makefile platform=...` workflow works, producing:
#     towns_libretro.so   (Linux/macOS)
#     towns_libretro.dll  (Windows/MinGW)
#
#  Usage:
#     make                      build for host platform
#     make platform=unix        force a generic unix build
#     make platform=windows     cross/MinGW build
#     make clean                remove build output
# ============================================================================

CORE_NAME := towns_libretro
SRC_DIR   := $(CURDIR)

.PHONY: all clean

all:
	@echo "==> Building Tsugaru libretro core via CMake ..."
	@sh $(SRC_DIR)/build_libretro.sh

clean:
	@rm -rf $(SRC_DIR)/../build_libretro

# NOTE: platform variable is accepted for libretro-super compatibility but the
# actual toolchain selection is handled inside build_libretro.sh / CMake.
