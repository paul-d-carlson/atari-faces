# mk/test-integration.mk -- boot the real demo disk and check the show.
#
# tests/driver/demo_runner.c boots build/faces.atr (the exact disk users
# get) on an emulated 48K Atari 800, taps START to skip the pauses, and
# checks the page-six report: every mystery face recognised as the numpy
# reference says, each phase within its frame budget. It also saves a
# screenshot of every screen and the 6502 line coverage of the whole demo.

INT_DIR := $(BUILD_DIR)/integration
DEMO_RUNNER := $(INT_DIR)/demo_runner

$(DEMO_RUNNER): tests/driver/demo_runner.c $(EMU_SRCS) $(EMU_HDRS) src/demo.h src/screen.h \
		tests/fixtures/ef_reference.h $(LIBATARI800)
	$(call msg,HOSTCC,$@)
	@mkdir -p $(@D)
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -I tests/driver -I src $(LIBATARI800_CFLAGS) \
		-o $@ tests/driver/demo_runner.c $(EMU_SRCS) $(LIBATARI800) $(LIBATARI800_LIBS) -lm

INT_COVER := $(foreach f,$(APP_SRCS),--cover $(f))

.PHONY: test-integration screenshots

test-integration: $(DEMO_RUNNER) $(DEMO_ATR) ## Boot the demo disk and check every match
	$(Q)rm -rf $(INT_DIR)/shots && mkdir -p $(INT_DIR)/shots
	$(Q)$(DEMO_RUNNER) $(DEMO_ATR) $(DEMO_DBG) --shots $(INT_DIR)/shots $(INT_COVER)

screenshots: test-integration ## Same, then list the PNG of every screen
	@ls -1 $(INT_DIR)/shots
