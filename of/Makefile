all: build/rdn.elf build/rdn1.elf build/rdn0.elf
# Open Firmware client (PowerPC ELF32); see docs/OPEN-FIRMWARE-PLAN.md.
PPC_CC ?= ../third_party/ppc-toolchain/bin/powerpc-linux-gcc
HW = rdn_gart rdn_mc rdn_watermark rdn_pm rdn_cursor rdn_mem rdn_gpu rdn_cp rdn_blit rdn_selftest rdn_pattern rdn_modeset rdn_dp rdn_mode rdn_dmt rdn_i2c rdn_post rdn_atom
CF = -DEMBED_VBIOS=\"$(abspath ../private/vbios.rom)\" -std=gnu99 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffreestanding -fno-builtin -fno-pic -msoft-float -mno-altivec -mcpu=powerpc -m32 -fno-stack-protector
build/rdn1.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	$(PPC_CC) $(CF) -DSTAGE=1 -nostdlib -static -Wl,-T,link.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1
build/rdn.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	$(PPC_CC) $(CF) -nostdlib -static -Wl,-T,link.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1
clean:
	rm -rf build

# host test under qemu-ppc (mock Open Firmware); needs private/vbios.rom
build/mock: mock_main.c of_main.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c
	mkdir -p build
	$(PPC_CC) -std=gnu99 -O1 -g -w -static -o $@ mock_main.c of_main.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c

build/rdn0.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	$(PPC_CC) $(CF) -DSTAGE=0 -nostdlib -static -Wl,-T,link.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1

# the freestanding client itself against the mock, under qemu-ppc
build/mockf: mockf_start.S start.S mockf.c of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	$(PPC_CC) $(CF) -w -nostdlib -static -Wl,-T,link.ld -Wl,-e,_mstart -Wl,--no-warn-rwx-segments -o $@ mockf_start.S start.S mockf.c of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1
