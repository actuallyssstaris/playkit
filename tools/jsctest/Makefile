# JavaScriptCore smoke test for the PS5 payload toolchain.
#
# Usage: make SDK=/path/to/ps5-payload-sdk JSC=/path/to/build/jsc

SDK ?= /root/projects/PlaySurf/.deps/native/ps5-payload-sdk
JSC ?= /root/projects/PlayKit/build/jsc
ICU ?= /root/projects/PlayKit/build/icu-ps5
OUT ?= build/jsctest.elf

CC := $(SDK)/bin/prospero-clang
CXX := $(SDK)/bin/prospero-clang++
CFLAGS := -O2 -Wall -Wextra -I$(JSC)/JavaScriptCore/Headers -I$(JSC)/WTF/Headers -I$(JSC)/bmalloc/Headers
LDFLAGS := -Wl,--gc-sections -Wl,--build-id=none
LDLIBS := -L$(JSC)/lib -lJavaScriptCore -lWTF -lbmalloc \
          $(ICU)/lib/libicui18n.a $(ICU)/lib/libicuuc.a $(ICU)/lib/libicudata.a

all: $(OUT)

$(OUT): main.c
	@mkdir -p $(dir $@)
	$(CXX) -x c $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -f $(OUT)

.PHONY: all clean
