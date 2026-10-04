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

CFLAGS_COMMON = -std=gnu99 -O2 -g -Wall -Wextra -Wno-unused-parameter

# Files taken from Linux are built with the warnings they do not pass
# switched off, so that our own code can keep -Wextra.
CFLAGS_UPSTREAM = $(CFLAGS_COMMON) -Wno-sign-compare -Wno-type-limits \
	-Wno-unused-variable -Wno-unused-but-set-variable

HW_OBJS   = hw/rdn_atom.o hw/atom/atom.o
HW_HDRS   = $(wildcard hw/*.h hw/atom/*.h)

TESTS     = atom_replay

X86_TESTS = $(addprefix build/x86/,$(TESTS))
PPC_TESTS = $(addprefix build/ppc/,$(TESTS))

all: $(X86_TESTS) $(PPC_TESTS)

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
# them the test is skipped, loudly.
test: all
	@if [ ! -f $(VBIOS) ] || [ ! -f $(REF_PHASE) ]; then \
		echo "SKIP atom_replay: need $(VBIOS) and $(REF_PHASE)"; \
	else \
		set -e; \
		x86=$$(build/x86/atom_replay $(VBIOS) $(REF_PHASE)); \
		echo "x86: $$x86"; \
		ppc=$$($(QEMU_PPC) build/ppc/atom_replay $(VBIOS) $(REF_PHASE)); \
		echo "ppc: $$ppc"; \
		[ "$$x86" = "$$ppc" ] || { echo "FAIL: x86 and ppc differ"; exit 1; }; \
		case "$$x86" in PASS*) ;; *) exit 1 ;; esac; \
	fi

clean:
	rm -rf build/x86 build/ppc

.PHONY: all test clean
