BIN = rockbase-lcdui
CALIB = calibd
# Debug tools shipped in the image for closed-loop UI verification
# (DRM framebuffer screenshot + touch swipe injection). See skills:
# rockbase-screenshot / rockbase-swipe.
DEBUG_TOOLS = drmshot touchswipe

INCS += -I$(STAGING_DIR)/usr/include
INCS += -I$(STAGING_DIR)/usr/include/lvgl
INCS += -I$(STAGING_DIR)/usr/include/libdrm
INCS += -I$(STAGING_DIR)/usr/include/drm

LIBPATH += -L$(STAGING_DIR)/usr/lib
LIBS += -llvgl -ldisplay -ldrm -lpthread -lm

SRCS := $(wildcard *.cpp) $(wildcard core/*.cpp)
OBJS := $(SRCS:.cpp=.o)

# DSEG7 digital-tube fonts: generated C files (see font/gen_dseg7_fonts.py).
# Compiled as C (-x c) so the lv_font_t symbols stay unmangled and match
# LV_FONT_DECLARE's extern "C" declaration used from the C++ UI code.
FONT_SRCS := $(wildcard font/dseg7c*.c)
FONT_OBJS := $(FONT_SRCS:.c=.o)

all: $(BIN) $(CALIB) $(DEBUG_TOOLS)

$(OBJS): %.o: %.cpp *.h core/*.h
	$(CXX) -c $(CXXFLAGS) $< -o $@ $(INCS)

$(FONT_OBJS): font/%.o: font/%.c
	$(CXX) -x c -c $(CXXFLAGS) $< -o $@ $(INCS)

$(BIN): $(OBJS) $(FONT_OBJS)
	$(CXX) $(LDFLAGS) -s -o $(BIN) $(OBJS) $(FONT_OBJS) $(LIBPATH) $(LIBS)

$(CALIB): calibd.c
	$(CXX) $(CXXFLAGS) -O2 -s -o $@ $<

# Plain C debug tools. touchswipe is libc-only; drmshot needs libdrm and the
# kernel DRM headers (xf86drm.h pulls in <drm.h> from include/drm).
touchswipe: touchswipe.c
	$(CC) $(CFLAGS) -O2 -s -o $@ $<

drmshot: drmshot.c
	$(CC) $(CFLAGS) -O2 -s -o $@ $< -I$(STAGING_DIR)/usr/include/drm $(LIBPATH) -ldrm

# Host-side unit tests for the LVGL-free core layer. Uses the host compiler
# (never the cross toolchain) and installs nothing into the image.
HOST_CXX ?= g++
TEST_BIN := /tmp/rockbase-lcdui-core-test
test:
	$(HOST_CXX) -std=gnu++17 -Wall -Wextra -O1 -I. -o $(TEST_BIN) \
		core/test/test_core.cpp core/dashboard_data.cpp core/format_utils.cpp \
		core/json_parse.cpp core/ui_model.cpp
	$(TEST_BIN) $(CURDIR)/core/test/fixtures

clean:
	rm -f $(BIN) $(CALIB) $(DEBUG_TOOLS) $(OBJS) $(FONT_OBJS)
