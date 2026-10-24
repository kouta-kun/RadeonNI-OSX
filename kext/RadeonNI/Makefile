# Builds RadeonNI.kext inside the Tiger guest with Apple's gcc 4.0.1.
# Run through scripts/kext.sh from the host; not meant for the host itself.
#
# The hardware library in ../../hw is compiled into the kext unchanged.

KEXT     = RadeonNI
KROOT    = /System/Library/Frameworks/Kernel.framework
HW       = ../../hw
# make LEOPARD=1: the same sources, plus leopard.s, for Mac OS X 10.5
# (see scripts/leopard.sh). Built apart, so that the Tiger kext is not touched.
LEOPARD  = 0
ifeq ($(LEOPARD),1)
BUILD    = build-leopard
else
BUILD    = build
endif
BUNDLE   = $(BUILD)/$(KEXT).kext

CPPFLAGS = -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
	-nostdinc -Icompat -I$(KROOT)/PrivateHeaders -I$(KROOT)/Headers -I$(HW)
CFLAGS   = -arch ppc -Os -Wall -fno-builtin -fno-common -static \
	-mlong-branch -force_cpusubtype_ALL
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fapple-kext -fcheck-new
# Files taken from Linux do not pass -Wall cleanly with this compiler.
HWCFLAGS = $(CFLAGS) -std=gnu99 -Wno-unused -Wno-sign-compare

HW_SRCS  = rdn_uvd.c rdn_gart.c rdn_mc.c rdn_watermark.c rdn_pm.c rdn_cursor.c rdn_mem.c rdn_gpu.c rdn_cp.c rdn_blit.c rdn_selftest.c rdn_post.c rdn_atom.c rdn_i2c.c rdn_mode.c rdn_dmt.c rdn_modeset.c rdn_dp.c \
	rdn_pattern.c atom/atom.c
HW_OBJS  = $(addprefix $(BUILD)/hw/,$(HW_SRCS:.c=.o))
HW_OBJS += $(BUILD)/hw/rdn_mc_layout.o $(BUILD)/hw/rdn_uvd_h264.o $(BUILD)/hw/rdn_cp_dma_readback.o
OBJS     = $(BUILD)/RadeonNI.o $(BUILD)/RadeonNIAccel.o $(BUILD)/RadeonNISurface.o $(BUILD)/kmod_info.o $(HW_OBJS)
OBJS    += $(BUILD)/RadeonNIVideo.o $(BUILD)/RadeonNIVideoMemory.o $(BUILD)/RadeonNIUvdSession.o
OBJS    += $(BUILD)/RadeonNIUvdFixture.o $(BUILD)/uvd_decode_fixture.o
ifeq ($(LEOPARD),1)
OBJS    += $(BUILD)/leopard.o
CPPFLAGS += -DRDN_LEOPARD
endif

all: $(BUNDLE)

$(BUILD)/leopard.o: leopard.s
	@mkdir -p $(dir $@)
	gcc -arch ppc -c -o $@ $<

$(BUILD)/%.o: %.cpp RadeonNI.h RadeonNIAccel.h RadeonNISurface.h RadeonNIVideo.h RadeonNIVideoMemory.h $(wildcard $(HW)/*.h $(HW)/atom/*.h)
	@mkdir -p $(dir $@)
	g++ $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	gcc $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/hw/%.o: $(HW)/%.c $(wildcard $(HW)/*.h $(HW)/atom/*.h)
	@mkdir -p $(dir $@)
	gcc $(CPPFLAGS) $(HWCFLAGS) -c -o $@ $<

$(BUILD)/uvd_decode_fixture.o: ../../tests/uvd_decode_fixture.c ../../tests/uvd_decode_fixture.h
	@mkdir -p $(dir $@)
	gcc $(CPPFLAGS) $(HWCFLAGS) -c -o $@ $<

$(BUILD)/RadeonNIUvdFixture.o: ../../tests/uvd_decode_fixture.h

# Objects first: kld needs the symbols libkmodc++ defines for them.
$(BUNDLE): $(OBJS) Info.plist
	@mkdir -p $(BUNDLE)/Contents/MacOS
	g++ -arch ppc -static -nostdlib -r $(OBJS) -lkmodc++ -lkmod -lcc_kext \
		-o $(BUNDLE)/Contents/MacOS/$(KEXT)
	cp Info.plist $(BUNDLE)/Contents/Info.plist
ifeq ($(LEOPARD),1)
	perl -0777 -pi -e 's/com\.apple\.kernel\.(iokit|libkern|mach)/com.apple.kpi.$$1/g; s/<string>6\.0<\/string>/<string>9.0.0<\/string>/g; s{(<key>OSBundleLibraries</key>\s*<dict>)}{$$1\n\t\t<key>com.apple.kpi.bsd</key>\n\t\t<string>9.0.0</string>\n\t\t<key>com.apple.kpi.unsupported</key>\n\t\t<string>9.0.0</string>}' $(BUNDLE)/Contents/Info.plist
endif

clean:
	rm -rf $(BUILD)

.PHONY: all clean
