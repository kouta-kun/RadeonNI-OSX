# Builds RadeonNI.kext inside the Tiger guest with Apple's gcc 4.0.1.
# Run through scripts/kext.sh from the host; not meant for the host itself.
#
# The hardware library in ../../hw is compiled into the kext unchanged.

KEXT     = RadeonNI
KROOT    = /System/Library/Frameworks/Kernel.framework
HW       = ../../hw
BUILD    = build
BUNDLE   = $(BUILD)/$(KEXT).kext

CPPFLAGS = -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
	-nostdinc -Icompat -I$(KROOT)/PrivateHeaders -I$(KROOT)/Headers -I$(HW)
CFLAGS   = -arch ppc -Os -Wall -fno-builtin -fno-common -static \
	-mlong-branch -force_cpusubtype_ALL
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fapple-kext -fcheck-new
# Files taken from Linux do not pass -Wall cleanly with this compiler.
HWCFLAGS = $(CFLAGS) -std=gnu99 -Wno-unused -Wno-sign-compare

HW_SRCS  = rdn_cursor.c rdn_mem.c rdn_gpu.c rdn_cp.c rdn_selftest.c rdn_post.c rdn_atom.c rdn_i2c.c rdn_mode.c rdn_modeset.c \
	rdn_pattern.c atom/atom.c
HW_OBJS  = $(addprefix $(BUILD)/hw/,$(HW_SRCS:.c=.o))
OBJS     = $(BUILD)/RadeonNI.o $(BUILD)/RadeonNIAccel.o $(BUILD)/RadeonNISurface.o $(BUILD)/kmod_info.o $(HW_OBJS)

all: $(BUNDLE)

$(BUILD)/%.o: %.cpp RadeonNI.h RadeonNIAccel.h RadeonNISurface.h $(wildcard $(HW)/*.h $(HW)/atom/*.h)
	@mkdir -p $(dir $@)
	g++ $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	gcc $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/hw/%.o: $(HW)/%.c $(wildcard $(HW)/*.h $(HW)/atom/*.h)
	@mkdir -p $(dir $@)
	gcc $(CPPFLAGS) $(HWCFLAGS) -c -o $@ $<

# Objects first: kld needs the symbols libkmodc++ defines for them.
$(BUNDLE): $(OBJS) Info.plist
	@mkdir -p $(BUNDLE)/Contents/MacOS
	g++ -arch ppc -static -nostdlib -r $(OBJS) -lkmodc++ -lkmod -lcc_kext \
		-o $(BUNDLE)/Contents/MacOS/$(KEXT)
	cp Info.plist $(BUNDLE)/Contents/Info.plist

clean:
	rm -rf $(BUILD)

.PHONY: all clean
