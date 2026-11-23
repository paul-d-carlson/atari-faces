# mk/config.mk -- tools, paths and flags shared by every build fragment.
#
# Every variable can be overridden on the command line, e.g.
#   make CL65=/opt/cc65/bin/cl65 HOST_CC=gcc DOS25_DIR=~/atari/dos25

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
BUILD_DIR ?= build
DEPS_DIR  ?= $(BUILD_DIR)/deps
DOS25_DIR ?= third_party/atari-dos25

# ---------------------------------------------------------------------------
# Atari (6502) toolchain -- cc65 <https://cc65.github.io/>
# ---------------------------------------------------------------------------
CL65   ?= cl65
TARGET := atari

# -Os keeps the code small: the whole demo must fit between $1D00 and the
# screen at $B5E0. The per-pixel loops (ef_kernel.c, screen.c and the mean
# in ef_model.c) switch on static locals themselves with #pragma, which is
# where nearly all of the speed-up from -Cl comes from; using -Cl for
# every file would cost RAM the demo does not have.
APP_CFLAGS := -t $(TARGET) -Os -g

# ---------------------------------------------------------------------------
# Host toolchain (unit tests and the emulator-based test runners)
# ---------------------------------------------------------------------------
HOST_CC     ?= cc
HOST_CFLAGS ?= -O1 -g -Wall -Wextra
# The portable code is also compiled natively for the unit tests, with
# sanitizers and EF_CHECK_RANGE: every value the 6502 keeps in a 16-bit
# int is checked to fit, so an overflow fails on the Mac first.
UNIT_CFLAGS := -std=c99 -pedantic -fno-common -DEF_CHECK_RANGE \
               -fsanitize=address,undefined -fno-sanitize-recover=all

# Source coverage for the host unit tests (make coverage). Needs clang as
# HOST_CC; on macOS the LLVM tools come from Xcode via xcrun.
XCRUN         := $(if $(shell command -v xcrun 2>/dev/null),xcrun )
LLVM_PROFDATA ?= $(XCRUN)llvm-profdata
LLVM_COV      ?= $(XCRUN)llvm-cov

# ---------------------------------------------------------------------------
# Miscellaneous host tools
# ---------------------------------------------------------------------------
CURL     ?= curl
ATARI800 ?= atari800
# Only needed to regenerate the data (make data) and to test the exporter.
PYTHON   ?= $(if $(wildcard .env/bin/python),.env/bin/python,python3)

ifneq ($(shell command -v sha256sum 2>/dev/null),)
SHA256SUM ?= sha256sum
else
SHA256SUM ?= shasum -a 256
endif

# ---------------------------------------------------------------------------
# Memory map
# ---------------------------------------------------------------------------
# MEMLO (first free byte) after Atari DOS 2.5 boots with its default
# configuration. The demo must load above it.
DOS25_MEMLO := 1CFC

# Quiet output by default; `make V=1` shows full commands.
ifeq ($(V),1)
Q :=
msg :=
else
Q := @
msg = @printf '  %-8s %s\n' '$(1)' '$(2)'
endif
