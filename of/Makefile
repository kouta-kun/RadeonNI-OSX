all: build/rdnc.elf build/rdnt1.elf build/rdnt2.elf build/rdnt3.elf build/rdnn.elf build/rdns.elf build/rdn8.elf build/rdn.elf build/rdn1.elf build/rdn0.elf
# Open Firmware client (PowerPC ELF32); see docs/OPEN-FIRMWARE-PLAN.md.
PPC_CC ?= ../third_party/ppc-toolchain/bin/powerpc-linux-gcc
HW = rdn_gart rdn_mc rdn_watermark rdn_pm rdn_cursor rdn_mem rdn_gpu rdn_cp rdn_blit rdn_selftest rdn_pattern rdn_modeset rdn_dp rdn_mode rdn_dmt rdn_i2c rdn_post rdn_atom
CF = -DEMBED_VBIOS=\"$(abspath ../private/vbios.rom)\" -std=gnu99 -O2 -g -Wall -Wextra -Wno-unused-parameter -ffreestanding -fno-builtin -fno-pic -msoft-float -mno-altivec -mcpu=powerpc -m32 -fno-stack-protector
build/rdn1.elf: build/link-1.ld build/link-2.ld build/link-3.ld start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	$(PPC_CC) $(CF) -DSTAGE=1 -nostdlib -static -Wl,-T,build/link-2.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1
build/rdn.elf: build/link-1.ld build/link-2.ld build/link-3.ld start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	$(PPC_CC) $(CF) -nostdlib -static -Wl,-T,build/link-3.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1
clean:
	rm -rf build

# host test under qemu-ppc (mock Open Firmware); needs private/vbios.rom
build/mock: mock_main.c of_main.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c
	mkdir -p build
	$(PPC_CC) -std=gnu99 -O1 -g -w -static -o $@ mock_main.c of_main.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c

build/rdn0.elf: build/link-1.ld build/link-2.ld build/link-3.ld start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	$(PPC_CC) $(CF) -DSTAGE=0 -nostdlib -static -Wl,-T,build/link-1.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1

# the freestanding client itself against the mock, under qemu-ppc
build/mockf: build/link-1.ld build/link-2.ld build/link-3.ld mockf_start.S start.S mockf.c of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	$(PPC_CC) $(CF) -w -nostdlib -static -Wl,-T,build/link-1.ld -Wl,-e,_mstart -Wl,--no-warn-rwx-segments -o $@ mockf_start.S start.S mockf.c of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1

build/link-1.ld build/link-2.ld build/link-3.ld: link.ld
	mkdir -p build
	for n in 1 2 3; do sed "s/LOADADDR/0x0$${n}000000/" link.ld > build/link-$$n.ld; done

build/rdn8.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	sed "s/LOADADDR/0x04000000/" link.ld > build/link-4.ld
	$(PPC_CC) $(CF) -DFB8 -nostdlib -static -Wl,-T,build/link-4.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1

build/rdns.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	sed "s/LOADADDR/0x05000000/" link.ld > build/link-5.ld
	$(PPC_CC) $(CF) -DSILENT -nostdlib -static -Wl,-T,build/link-5.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1

build/rdnn.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	sed "s/LOADADDR/0x06000000/" link.ld > build/link-6.ld
	$(PPC_CC) $(CF) -DSILENT -DSTAGE=-1 -nostdlib -static -Wl,-T,build/link-6.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1

build/rdnt1.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	sed "s/LOADADDR/0x07000000/" link.ld > build/link-t1.ld
	$(PPC_CC) $(CF) -DSILENT -DTAIL=1 -nostdlib -static -Wl,-T,build/link-t1.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1

build/rdnt2.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	sed "s/LOADADDR/0x08000000/" link.ld > build/link-t2.ld
	$(PPC_CC) $(CF) -DSILENT -DTAIL=2 -nostdlib -static -Wl,-T,build/link-t2.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1

build/rdnt3.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	sed "s/LOADADDR/0x09000000/" link.ld > build/link-t3.ld
	$(PPC_CC) $(CF) -DSILENT -DTAIL=3 -nostdlib -static -Wl,-T,build/link-t3.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1

build/rdnc.elf: start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c link.ld
	mkdir -p build
	sed "s/LOADADDR/0x0a000000/" link.ld > build/link-c.ld
	$(PPC_CC) $(CF) -DSILENT -DCHAIN_BOOT -nostdlib -static -Wl,-T,build/link-c.ld -Wl,--no-warn-rwx-segments -o $@ start.S of_main.c libc.c $(addprefix ../hw/,$(addsuffix .c,$(HW))) ../hw/atom/atom.c $$($(PPC_CC) -m32 -print-libgcc-file-name) 2>&1
