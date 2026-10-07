# Host-side build: hardware library, Linux tools and tests.
#
#   make            build everything for x86 and big-endian PowerPC
#   make test       run the tests natively and under qemu-ppc
#
# The kext is not built here; it is built inside the Tiger guest.

PPC_CC   ?= third_party/ppc-toolchain/bin/powerpc-linux-gcc
QEMU_PPC ?= third_party/qemu/qemu-ppc

VBIOS      ?= private/vbios.rom
REF_PHASE  ?= traces/ref-radeon-2.phase-a1.txt
EDID       ?= private/monitor-edid.bin
# A Linux modeset to the EDID's preferred mode (phase a4 of the trace guest)
REF_MODESET ?= traces/ref-radeon-3.phase-a4.txt
# The modeset before it in the same run: from the preferred mode to the
# EDID's second detailed timing (phase a3)
REF_MODESET2 ?= traces/ref-radeon-3.phase-a3.txt
# The same guest's driver start, for the one-time display setup
REF_INIT   ?= traces/ref-radeon-3.phase-a1.txt

FW_PFP     ?= firmware/TURKS_pfp.bin
FW_ME      ?= firmware/TURKS_me.bin
FW_MC      ?= firmware/TURKS_mc.bin

CFLAGS_COMMON = -std=gnu99 -O2 -g -Wall -Wextra -Wno-unused-parameter

# Files taken from Linux are built with the warnings they do not pass
# switched off, so that our own code can keep -Wextra.
CFLAGS_UPSTREAM = $(CFLAGS_COMMON) -Wno-sign-compare -Wno-type-limits \
	-Wno-unused-variable -Wno-unused-but-set-variable

HW_OBJS   = hw/rdn_gart.o hw/rdn_mc.o hw/rdn_watermark.o hw/rdn_pm.o hw/rdn_cursor.o hw/rdn_mem.o hw/rdn_gpu.o hw/rdn_cp.o hw/rdn_blit.o hw/rdn_selftest.o hw/rdn_pattern.o hw/rdn_modeset.o hw/rdn_dp.o hw/rdn_mode.o hw/rdn_i2c.o hw/rdn_post.o hw/rdn_atom.o hw/atom/atom.o
HW_HDRS   = $(wildcard hw/*.h hw/atom/*.h hw/linux/*.h)

TESTS     = atom_replay i2c_edid dp_link modeset_replay accel_replay blit_ops mem_alloc pm_states mc_replay gart_replay

X86_TESTS = $(addprefix build/x86/,$(TESTS))
PPC_TESTS = $(addprefix build/ppc/,$(TESTS))

# The tool talks to the real card through sysfs, so it is only useful on the
# x86 host. The PowerPC build is there to keep it compiling big-endian.
all: $(X86_TESTS) $(PPC_TESTS) build/x86/rdn_tool build/ppc/rdn_tool

build/x86/rdn_tool: build/x86/tools/rdn_tool.o $(addprefix build/x86/,$(HW_OBJS))
	$(CC) -o $@ $^

build/ppc/rdn_tool: build/ppc/tools/rdn_tool.o $(addprefix build/ppc/,$(HW_OBJS))
	$(PPC_CC) -static -o $@ $^

build/x86/hw/atom/%.o: hw/atom/%.c $(HW_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_UPSTREAM) -c -o $@ $<

build/ppc/hw/atom/%.o: hw/atom/%.c $(HW_HDRS)
	@mkdir -p $(dir $@)
	$(PPC_CC) $(CFLAGS_UPSTREAM) -c -o $@ $<

build/x86/%.o: %.c $(HW_HDRS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -c -o $@ $<

build/ppc/%.o: %.c $(HW_HDRS)
	@mkdir -p $(dir $@)
	$(PPC_CC) $(CFLAGS_COMMON) -c -o $@ $<

build/x86/%: build/x86/tests/%.o $(addprefix build/x86/,$(HW_OBJS))
	$(CC) -o $@ $^

build/ppc/%: build/ppc/tests/%.o $(addprefix build/ppc/,$(HW_OBJS))
	$(PPC_CC) -static -o $@ $^

.SECONDARY:

# The inputs are not in the repository (VBIOS dump, reference trace). Without
# them the tests are skipped, loudly. Each test runs natively and under
# qemu-ppc; the two outputs must be identical and end in a PASS line.
run_test = \
	x86=$$(build/x86/$(1) $(2)); ppc=$$($(QEMU_PPC) build/ppc/$(1) $(2)); \
	echo "x86 $(1): $$(echo "$$x86" | tail -1)"; \
	echo "ppc $(1): $$(echo "$$ppc" | tail -1)"; \
	[ "$$x86" = "$$ppc" ] || { echo "FAIL $(1): x86 and ppc differ"; exit 1; }; \
	case "$$(echo "$$x86" | tail -1)" in PASS*) ;; *) echo "$$x86"; exit 1 ;; esac

test: all
	@if [ ! -f $(VBIOS) ] || [ ! -f $(REF_PHASE) ]; then \
		echo "SKIP: need $(VBIOS) and $(REF_PHASE)"; \
	else \
		set -e; \
		$(call run_test,mem_alloc,); \
		$(call run_test,blit_ops,); \
		$(call run_test,atom_replay,$(VBIOS) $(REF_PHASE)); \
		$(call run_test,i2c_edid,$(VBIOS)); \
		$(call run_test,dp_link,$(VBIOS)); \
		$(call run_test,pm_states,$(VBIOS)); \
		if [ -f $(REF_MODESET) ] && [ -f $(EDID) ]; then \
			$(call run_test,modeset_replay,$(VBIOS) $(EDID) $(REF_MODESET) - 0 $(REF_INIT)); \
			$(call run_test,modeset_replay,$(VBIOS) $(EDID) $(REF_MODESET2) 0 1); \
		else echo "SKIP modeset_replay: need $(EDID) and $(REF_MODESET)"; fi; \
		if [ -f $(FW_PFP) ] && [ -f $(FW_ME) ] && [ -f $(REF_INIT) ]; then \
			$(call run_test,accel_replay,$(VBIOS) $(FW_PFP) $(FW_ME) $(REF_INIT)); \
		else echo "SKIP accel_replay: need $(FW_PFP), $(FW_ME) and $(REF_INIT)"; fi; \
		if [ -f $(FW_MC) ] && [ -f $(REF_INIT) ]; then \
			$(call run_test,mc_replay,$(FW_MC) $(REF_INIT)); \
		else echo "SKIP mc_replay: need $(FW_MC) and $(REF_INIT)"; fi; \
		if [ -f $(REF_INIT) ]; then \
			$(call run_test,gart_replay,$(REF_INIT)); \
		else echo "SKIP gart_replay: need $(REF_INIT)"; fi; \
	fi

# Mesa-based tests need scripts/build-mesa.sh to have run.
MESA_BUILD ?= third_party/mesa-26.2.4/build-x86/src/gallium/targets/rdn
build/x86/rdn_gltest: mesa/tests/rdn_gltest.c $(MESA_BUILD)/librdngl.so
	$(CC) -O2 -Wall -Imesa/frontend/include -Ithird_party/mesa-26.2.4/include \
		-o $@ $< -L$(MESA_BUILD) -lrdngl -Wl,-rpath,$(CURDIR)/$(MESA_BUILD)

clean:
	rm -rf build/x86 build/ppc

.PHONY: all test clean
