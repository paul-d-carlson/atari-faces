# mk/test-target.mk -- the unit tests on a real (emulated) 6502.
#
# tests/unit/*.c are compiled with cc65 into four Atari test images (all
# of them together would not fit in 48K next to the code under test):
#
#   math  ef_kernel, ef_data and fmt
#   model ef_model on synthetic faces with known answers
#   ref   disk, and the full reference comparison on D:FACES.DAT
#   hw    screen, sound and sys against the real (emulated) hardware
#
# Each image boots from its own DOS 2.5 disk under libatari800. It writes
# results into page six ($0600); tests/driver/target_runner.c reads them back,
# prints the 6502 timings and line coverage from the execution trace.

TGT_DIR := $(BUILD_DIR)/target

TGT_COMMON := tests/unit/tf.c tests/unit/tutil.c

math_SRCS := tests/target/main_math.c $(TGT_COMMON) \
	tests/unit/test_kernel.c tests/unit/test_data.c tests/unit/test_fmt.c \
	src/ef_kernel.c src/ef_data.c src/fmt.c

model_SRCS := tests/target/main_model.c $(TGT_COMMON) \
	tests/unit/test_model.c src/ef_kernel.c src/ef_model.c

ref_SRCS := tests/target/main_ref.c $(TGT_COMMON) \
	tests/unit/test_disk.c tests/unit/test_reference.c $(CORE_SRCS)

# Coverage is reported per image for the code each one exercises.
math_COVER := src/ef_kernel.c src/ef_data.c src/fmt.c
model_COVER := src/ef_model.c
ref_COVER := src/disk.c src/ef_model.c src/ef_kernel.c

hw_SRCS := tests/target/main_hw.c $(TGT_COMMON) \
	tests/unit/test_screen.c tests/unit/test_sound.c tests/unit/test_sys.c \
	$(HW_SRCS)
hw_COVER := $(HW_SRCS)

TGT_IMAGES := math model ref hw

$(TGT_DIR)/test_%.xex: $(APP_CFG) tools/memreport.sh
	$(call link_xex,$@,$(call obj,$($*_SRCS)))

# Static pattern rules cannot take per-image object lists, so spell out
# the object prerequisites here.
$(TGT_DIR)/test_math.xex: $(call obj,$(math_SRCS))
$(TGT_DIR)/test_model.xex: $(call obj,$(model_SRCS))
$(TGT_DIR)/test_ref.xex: $(call obj,$(ref_SRCS))
$(TGT_DIR)/test_hw.xex: $(call obj,$(hw_SRCS))
$(OBJ_DIR)/tests/unit/test_reference.o: tests/fixtures/ef_reference.h

$(TGT_DIR)/test_%.atr: $(TGT_DIR)/test_%.xex $(DATA) $(DISK_PREREQS)
	$(call msg,ATR,$@)
	$(Q)$(MKATR) 720 $@ AUTORUN.SYS=$< FACES.DAT=$(DATA)

TGT_RUNNER := $(TGT_DIR)/target_runner
EMU_SRCS := tests/driver/emu.c tests/driver/dbgcov.c
EMU_HDRS := tests/driver/emu.h tests/driver/dbgcov.h

$(TGT_RUNNER): tests/driver/target_runner.c $(EMU_SRCS) $(EMU_HDRS) tests/common/test_protocol.h $(LIBATARI800)
	$(call msg,HOSTCC,$@)
	@mkdir -p $(@D)
	$(Q)$(HOST_CC) $(HOST_CFLAGS) -I tests/driver -I tests/common $(LIBATARI800_CFLAGS) \
		-o $@ tests/driver/target_runner.c $(EMU_SRCS) $(LIBATARI800) $(LIBATARI800_LIBS) -lm

.PHONY: build-target test-target

build-target: $(TGT_RUNNER) $(foreach i,$(TGT_IMAGES),$(TGT_DIR)/test_$(i).atr) ## Build the 6502 test images and runner

test-target: build-target ## Run the unit tests on the emulated 6502
	$(Q)status=0; for i in $(TGT_IMAGES); do \
		echo "== $$i =="; \
		case $$i in math) cover="$(math_COVER)";; model) cover="$(model_COVER)";; \
			ref) cover="$(ref_COVER)";; \
			hw) cover="$(hw_COVER)";; esac; \
		args=""; for f in $$cover; do args="$$args --cover $$f"; done; \
		$(TGT_RUNNER) $(TGT_DIR)/test_$$i.atr $(TGT_DIR)/test_$$i.dbg $$args || status=1; \
	done; exit $$status
