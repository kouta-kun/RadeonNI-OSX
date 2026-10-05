# Builds RadeonNIGLDriver.bundle inside the Tiger guest with Apple's gcc.
# Run through scripts/gld.sh from the host.

NAME   = RadeonNIGLDriver
BUILD  = build
BUNDLE = $(BUILD)/$(NAME).bundle
SDK    = /Developer/SDKs/MacOSX10.4u.sdk
CFLAGS = -arch ppc -Os -Wall -isysroot $(SDK)

all: $(BUNDLE)

$(BUNDLE): $(NAME).c Info.plist
	@mkdir -p $(BUNDLE)/Contents/MacOS
	gcc $(CFLAGS) -bundle -o $(BUNDLE)/Contents/MacOS/$(NAME) $(NAME).c
	cp Info.plist $(BUNDLE)/Contents/Info.plist

clean:
	rm -rf $(BUILD)

.PHONY: all clean
