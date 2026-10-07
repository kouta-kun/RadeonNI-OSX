# Builds RadeonNIRomProbe.kext on Tiger with Apple's gcc 4.0.1, like
# ../RadeonNI. See RomProbe.cpp for what it is for.

KEXT     = RadeonNIRomProbe
KROOT    = /System/Library/Frameworks/Kernel.framework
BUILD    = build
BUNDLE   = $(BUILD)/$(KEXT).kext

CPPFLAGS = -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
	-nostdinc -I$(KROOT)/PrivateHeaders -I$(KROOT)/Headers
CFLAGS   = -arch ppc -Os -Wall -fno-builtin -fno-common -static \
	-mlong-branch -force_cpusubtype_ALL
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fapple-kext -fcheck-new

OBJS     = $(BUILD)/RomProbe.o $(BUILD)/kmod_info.o

all: $(BUNDLE)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	g++ $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	gcc $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUNDLE): $(OBJS) Info.plist
	@mkdir -p $(BUNDLE)/Contents/MacOS
	g++ -arch ppc -static -nostdlib -r $(OBJS) -lkmodc++ -lkmod -lcc_kext \
		-o $(BUNDLE)/Contents/MacOS/$(KEXT)
	cp Info.plist $(BUNDLE)/Contents/Info.plist

clean:
	rm -rf $(BUILD)

.PHONY: all clean
