# Builds RadeonNI.kext inside the Tiger guest with Apple's gcc 4.0.1.
# Run through scripts/kext.sh from the host; not meant for the host itself.

KEXT     = RadeonNI
KROOT    = /System/Library/Frameworks/Kernel.framework
BUILD    = build
BUNDLE   = $(BUILD)/$(KEXT).kext

CPPFLAGS = -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
	-nostdinc -I$(KROOT)/PrivateHeaders -I$(KROOT)/Headers -I../../hw
CFLAGS   = -arch ppc -Os -Wall -fno-builtin -fno-common -static \
	-mlong-branch -force_cpusubtype_ALL
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti -fapple-kext -fcheck-new

OBJS     = $(BUILD)/RadeonNI.o $(BUILD)/kmod_info.o

all: $(BUNDLE)

$(BUILD)/%.o: %.cpp RadeonNI.h
	@mkdir -p $(BUILD)
	g++ $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.c
	@mkdir -p $(BUILD)
	gcc $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUNDLE): $(OBJS) Info.plist
	@mkdir -p $(BUNDLE)/Contents/MacOS
	g++ -arch ppc -static -nostdlib -r $(OBJS) -lkmodc++ -lkmod -lcc_kext \
		-o $(BUNDLE)/Contents/MacOS/$(KEXT)
	cp Info.plist $(BUNDLE)/Contents/Info.plist

clean:
	rm -rf $(BUILD)

.PHONY: all clean
