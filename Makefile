# faces -- eigenfaces face recognition demo for the Atari 8-bit
#
# Run `make help` for the list of targets. All output goes to $(BUILD_DIR).

.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
.SUFFIXES:

VERSION := 0.1.0

include mk/config.mk
include mk/deps.mk

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
# Portable math and data code: no Atari headers, also built natively.
CORE_SRCS := \
	src/ef_kernel.c \
	src/ef_model.c \
	src/ef_data.c \
	src/disk.c \
	src/fmt.c

# Atari hardware: reached only through src/hw.h, so they build natively too.
HW_SRCS := \
	src/screen.c \
	src/sound.c \
	src/sys.c

APP_SRCS := $(CORE_SRCS) $(HW_SRCS) src/demo.c src/main.c
APP_CFG  := src/atari.cfg
DATA     := data/FACES.DAT

OBJ_DIR := $(BUILD_DIR)/obj
obj = $(patsubst %.c,$(OBJ_DIR)/%.o,$(1))

# Test sources are compiled with the same flags; the extra include paths
# are harmless for the application.
CC65_INCLUDES := -I src -I tests/unit -I tests/common

$(OBJ_DIR)/%.o: %.c
	$(call msg,CC65,$<)
	@mkdir -p $(@D)
	$(Q)$(CL65) $(APP_CFLAGS) $(CC65_INCLUDES) --create-dep $(@:.o=.d) -c -o $@ $<

# link_xex(output, objects): link with the demo's memory map and report
# how much room is left below the C stack.
define link_xex
	$(call msg,LD65,$(1))
	$(Q)$(CL65) -t $(TARGET) -C $(APP_CFG) -m $(1:.xex=.map) -Wl --dbgfile,$(1:.xex=.dbg) \
		-o $(1) $(2)
	$(Q)sh tools/memreport.sh $(1:.xex=.map) $(APP_CFG)
endef

DEMO_XEX := $(BUILD_DIR)/faces.xex
DEMO_DBG := $(BUILD_DIR)/faces.dbg
DEMO_ATR := $(BUILD_DIR)/faces.atr

$(DEMO_XEX): $(call obj,$(APP_SRCS)) $(APP_CFG) tools/memreport.sh
	$(call link_xex,$@,$(call obj,$(APP_SRCS)))

# ---------------------------------------------------------------------------
# Boot disk
# ---------------------------------------------------------------------------
MKATR := sh tools/mkatr.sh $(DIR2ATR) $(DOS25_DIR)

# Every disk image depends on these: a verified DOS 2.5 and a linker
# config that loads above DOS's MEMLO.
DISK_PREREQS := $(BUILD_DIR)/.dos25-verified $(BUILD_DIR)/.memlo-checked $(DIR2ATR_DEP)

DOS25_VERIFY ?= 1
$(BUILD_DIR)/.dos25-verified: $(DOS25_DIR)/DOS.SYS $(DOS25_DIR)/DUP.SYS third_party/atari-dos25/SHA256SUMS
	@mkdir -p $(@D)
ifeq ($(DOS25_VERIFY),1)
	$(Q)cd $(DOS25_DIR) && $(SHA256SUM) -c $(abspath third_party/atari-dos25/SHA256SUMS) >/dev/null \
		|| { echo "error: DOS.SYS/DUP.SYS in $(DOS25_DIR) are not the expected Atari DOS 2.5." >&2; \
		     echo "       A different DOS build may use more low memory than the demo" >&2; \
		     echo "       allows for. Override with DOS25_VERIFY=0 at your own risk." >&2; exit 1; }
endif
	$(Q)touch $@

$(BUILD_DIR)/.memlo-checked: $(APP_CFG)
	@mkdir -p $(@D)
	$(Q)start=$$(sed -n 's/.*STARTADDRESS: *default *= *\$$\([0-9A-Fa-f]*\).*/\1/p' $<); \
	if [ -z "$$start" ]; then echo "error: no STARTADDRESS in $<" >&2; exit 1; fi; \
	if [ $$((0x$$start)) -le $$((0x$(DOS25_MEMLO))) ]; then \
		echo "error: $< STARTADDRESS \$$$$start is not above DOS 2.5 MEMLO \$$$(DOS25_MEMLO)" >&2; exit 1; \
	fi
	$(Q)touch $@

$(DEMO_ATR): $(DEMO_XEX) $(DATA) $(DISK_PREREQS)
	$(call msg,ATR,$@)
	$(Q)$(MKATR) 720 $@ AUTORUN.SYS=$(DEMO_XEX) FACES.DAT=$(DATA)

# ---------------------------------------------------------------------------
# Data (needs Python with numpy, pillow and scikit-learn)
# ---------------------------------------------------------------------------
.PHONY: data
data: ## Regenerate data/FACES.DAT and tests/fixtures/ef_reference.h
	$(Q)$(PYTHON) tools/export_faces.py

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include mk/test-host.mk
include mk/test-target.mk
include mk/test-integration.mk

# ---------------------------------------------------------------------------
# Release archive
# ---------------------------------------------------------------------------
DIST_NAME := atari-faces-$(VERSION)
DIST_ZIP  := $(BUILD_DIR)/$(DIST_NAME).zip

$(DIST_ZIP): $(DEMO_XEX) $(DEMO_ATR) $(DATA) README.md LICENSE NOTICE
	$(call msg,ZIP,$@)
	$(Q)rm -rf $(BUILD_DIR)/$(DIST_NAME) $@
	$(Q)mkdir -p $(BUILD_DIR)/$(DIST_NAME)
	$(Q)cp $^ $(BUILD_DIR)/$(DIST_NAME)/
	$(Q)cd $(BUILD_DIR) && zip -qr $(DIST_NAME).zip $(DIST_NAME)
	$(Q)rm -rf $(BUILD_DIR)/$(DIST_NAME)

# ---------------------------------------------------------------------------
# User-facing targets
# ---------------------------------------------------------------------------
.PHONY: all atr run test size dist version clean distclean help

all: $(DEMO_XEX) ## Build build/faces.xex (default)

atr: $(DEMO_ATR) ## Build the bootable DOS 2.5 disk build/faces.atr

run: $(DEMO_ATR) ## Boot build/faces.atr in the atari800 emulator (800, 48K, no BASIC)
	$(ATARI800) -atari -nobasic $(DEMO_ATR)

test: test-host test-target test-integration test-tools ## Run every test suite

size: $(DEMO_XEX) ## Show the memory map of the demo
	@grep -E '^(STARTUP|ONCE|CODE|RODATA|DATA|BSS) ' $(DEMO_XEX:.xex=.map)

dist: $(DIST_ZIP) ## Package faces.xex, faces.atr and FACES.DAT for a release

version: ## Print the project version
	@echo $(VERSION)

clean: ## Remove build output (keeps downloaded dependencies)
	$(Q)if [ -d $(BUILD_DIR) ]; then \
		find $(BUILD_DIR) -mindepth 1 -maxdepth 1 ! -name deps -exec rm -rf {} +; \
	fi

distclean: ## Remove all build output, including dependencies
	$(Q)rm -rf $(BUILD_DIR)

help: ## Show this help
	@echo "Usage: make [target] [VARIABLE=value ...]"
	@echo
	@echo "Targets:"
	@grep -hE '^[a-z-]+:.*## ' $(MAKEFILE_LIST) | \
		sed 's/^\([a-z-]*\):.*## /\1|/' | awk -F'|' '{ printf "  %-18s %s\n", $$1, $$2 }'
	@echo
	@echo "Common variables:"
	@echo "  CL65=path          cc65 compiler driver      (default: cl65)"
	@echo "  HOST_CC=compiler   compiler for host tests   (default: cc)"
	@echo "  PYTHON=path        python for make data      (default: .env/bin/python or python3)"
	@echo "  DOS25_DIR=dir      DOS.SYS/DUP.SYS location  (default: third_party/atari-dos25)"
	@echo "  DIR2ATR=path       use an installed dir2atr instead of building one"
	@echo "  V=1                show full commands"

-include $(shell find $(OBJ_DIR) -name '*.d' 2>/dev/null)
