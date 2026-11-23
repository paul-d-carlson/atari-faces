# mk/test-host.mk -- the unit tests, compiled natively.
#
# The same tests/unit/*.c files also run on the 6502 (mk/test-target.mk).
# Natively they run in well under a second, with AddressSanitizer,
# UndefinedBehaviorSanitizer and EF_CHECK_RANGE (16-bit overflow checks)
# switched on. Atari hardware is a fake 64K array (tests/host/hw_host.c).

HOST_DIR := $(BUILD_DIR)/host

HOST_UNIT_SRCS := \
	$(CORE_SRCS) $(HW_SRCS) src/demo.c \
	$(wildcard tests/unit/*.c) \
	tests/host/hw_host.c \
	tests/host/main_host.c
HOST_UNIT_DEPS := $(HOST_UNIT_SRCS) $(wildcard src/*.h tests/unit/*.h tests/host/*.h) \
	tests/fixtures/ef_reference.h
HOST_INCLUDES := -I src -I tests/unit -I tests/host -I tests/common

HOST_UNIT := $(HOST_DIR)/unit_tests
HOST_COV  := $(HOST_DIR)/unit_tests_cov

$(HOST_UNIT): $(HOST_UNIT_DEPS)
	$(call msg,HOSTCC,$@)
	@mkdir -p $(@D)
	$(Q)$(HOST_CC) $(HOST_CFLAGS) $(UNIT_CFLAGS) $(HOST_INCLUDES) -o $@ $(HOST_UNIT_SRCS)

$(HOST_COV): $(HOST_UNIT_DEPS)
	$(call msg,HOSTCC,$@)
	@mkdir -p $(@D)
	$(Q)$(HOST_CC) $(HOST_CFLAGS) $(UNIT_CFLAGS) -fprofile-instr-generate -fcoverage-mapping \
		$(HOST_INCLUDES) -o $@ $(HOST_UNIT_SRCS)

.PHONY: test-host coverage test-tools

test-host: $(HOST_UNIT) ## Run the unit tests natively (fast)
	$(Q)$(HOST_UNIT)

coverage: $(HOST_COV) ## Unit tests natively with a source-coverage report
	$(Q)LLVM_PROFILE_FILE=$(HOST_DIR)/unit.profraw $(HOST_COV) >/dev/null
	$(Q)$(LLVM_PROFDATA) merge -sparse $(HOST_DIR)/unit.profraw -o $(HOST_DIR)/unit.profdata
	$(Q)$(LLVM_COV) report $(HOST_COV) -instr-profile=$(HOST_DIR)/unit.profdata \
		$(CORE_SRCS) $(HW_SRCS) src/demo.c
	$(Q)$(LLVM_COV) show $(HOST_COV) -instr-profile=$(HOST_DIR)/unit.profdata \
		-format=html -output-dir=$(HOST_DIR)/coverage $(CORE_SRCS) $(HW_SRCS) src/demo.c
	@echo "  HTML report: $(HOST_DIR)/coverage/index.html"

# The exporter's own tests need numpy and pillow; skipped if missing.
test-tools: ## Test tools/export_faces.py (needs numpy and pillow)
	$(Q)if $(PYTHON) -c 'import numpy, PIL' 2>/dev/null; then \
		$(PYTHON) -m unittest discover -s tools -p 'test_*.py'; \
	else \
		echo "  SKIP     tools tests ($(PYTHON) has no numpy/pillow)"; \
	fi
