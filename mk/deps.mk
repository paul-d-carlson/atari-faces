# mk/deps.mk -- third-party host tools, fetched and built on demand
# (the same recipe as atari-emacs).
#
#   libatari800  atari800 emulator as a library; the test drivers boot the
#                real 6502 binaries under it. Built with MONITOR_PROFILE so
#                the drivers can read per-address execution counts for line
#                coverage.
#   dir2atr      from AtariSIO; builds bootable DOS 2.5 .atr disk images.
#
# Both are GPL-2.0 licensed. They are downloaded at build time into
# $(DEPS_DIR) and never redistributed by this project.

ATARI800_VERSION := 5.2.0
ATARI800_URL     := https://github.com/atari800/atari800/releases/download/ATARI800_5_2_0/atari800-$(ATARI800_VERSION)-src.tgz
ATARI800_SHA256  := 3874d02b89d83c8089f75391a4c91ecb4e94001da2020c2617be088eba1f461f

ATARISIO_VERSION := 221009
ATARISIO_URL     := https://www.horus.com/~hias/atari/atarisio/atarisio-$(ATARISIO_VERSION).tar.gz
ATARISIO_SHA256  := d3dfcc966b782fd5c23b46a0363648e7604670c0506b759553266f29a08f243c

ATARI800_DIR  := $(DEPS_DIR)/atari800-$(ATARI800_VERSION)
ATARI800_SRC  := $(ATARI800_DIR)/src
LIBATARI800   := $(ATARI800_SRC)/libatari800.a
LIBATARI800_CFLAGS := -I$(ATARI800_SRC) -I$(ATARI800_SRC)/libatari800
# Extra link libraries chosen by atari800's ./configure (e.g. -lz on some
# hosts), captured once the library is built.
LIBATARI800_LIBS_FILE := $(ATARI800_DIR)/.link-libs
LIBATARI800_LIBS = $(shell cat $(LIBATARI800_LIBS_FILE) 2>/dev/null)

ATARISIO_DIR  := $(DEPS_DIR)/atarisio-$(ATARISIO_VERSION)
# Set DIR2ATR=/path/to/dir2atr to use an existing install instead.
ifeq ($(origin DIR2ATR),undefined)
DIR2ATR       := $(ATARISIO_DIR)/tools/dir2atr
DIR2ATR_DEP   := $(DIR2ATR)
endif

FETCH := sh tools/fetch.sh

# MAKEOVERRIDES= keeps our own command-line variables (BUILD_DIR=..., etc.)
# from leaking into the third-party builds.
SUBMAKE := $(MAKE) MAKEOVERRIDES=

$(DEPS_DIR)/atari800-$(ATARI800_VERSION)-src.tgz:
	$(Q)$(FETCH) $(ATARI800_URL) $(ATARI800_SHA256) $@

$(DEPS_DIR)/atarisio-$(ATARISIO_VERSION).tar.gz:
	$(Q)$(FETCH) $(ATARISIO_URL) $(ATARISIO_SHA256) $@

# ./configure always leaves MONITOR_PROFILE disabled for the libatari800
# target, so it is switched on by editing the generated config.h.
#   -Wno-implicit-function-declaration  libatari800/main.c calls an
#       undeclared function, which modern compilers reject by default.
#   -fno-common  MONITOR_PROFILE adds a 1 MB tentative definition that
#       makes Apple ld warn about section alignment on every link.
$(LIBATARI800): $(DEPS_DIR)/atari800-$(ATARI800_VERSION)-src.tgz
	$(call msg,BUILD,libatari800 $(ATARI800_VERSION))
	$(Q)rm -rf $(ATARI800_DIR)
	$(Q)tar -xzf $< -C $(DEPS_DIR)
	$(Q)cd $(ATARI800_DIR) && ./configure --target=libatari800 >configure.log 2>&1 \
		|| { cat configure.log; exit 1; }
	$(Q)sed 's|/\* #undef MONITOR_PROFILE \*/|#define MONITOR_PROFILE 1|' \
		$(ATARI800_SRC)/config.h >$(ATARI800_SRC)/config.h.new
	$(Q)mv $(ATARI800_SRC)/config.h.new $(ATARI800_SRC)/config.h
	$(Q)grep -q '^#define MONITOR_PROFILE 1' $(ATARI800_SRC)/config.h \
		|| { echo "error: could not enable MONITOR_PROFILE in atari800 config.h" >&2; exit 1; }
	$(Q)$(SUBMAKE) -C $(ATARI800_SRC) libatari800.a >$(ATARI800_DIR)/build.log 2>&1 \
		CFLAGS="-O2 -Wno-implicit-function-declaration -fno-common" \
		|| { tail -40 $(ATARI800_DIR)/build.log; exit 1; }
	$(Q)sed -n 's/^LIBS = //p' $(ATARI800_SRC)/Makefile >$(LIBATARI800_LIBS_FILE)
	$(Q)touch $@

ifdef DIR2ATR_DEP
$(DIR2ATR): $(DEPS_DIR)/atarisio-$(ATARISIO_VERSION).tar.gz
	$(call msg,BUILD,dir2atr $(ATARISIO_VERSION))
	$(Q)rm -rf $(ATARISIO_DIR)
	$(Q)tar -xzf $< -C $(DEPS_DIR)
	$(Q)$(SUBMAKE) -C $(ATARISIO_DIR)/tools -f Makefile.posix dir2atr \
		CXXFLAGS="-O2 -w" >$(ATARISIO_DIR)/build.log 2>&1 \
		|| { tail -40 $(ATARISIO_DIR)/build.log; exit 1; }
	$(Q)touch $@
endif

.PHONY: deps
deps: $(LIBATARI800) $(DIR2ATR_DEP) ## Fetch and build libatari800 and dir2atr
