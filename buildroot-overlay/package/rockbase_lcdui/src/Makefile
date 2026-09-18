BIN = rockbase-lcdui
CALIB = calibd

INCS += -I$(STAGING_DIR)/usr/include
INCS += -I$(STAGING_DIR)/usr/include/lvgl
INCS += -I$(STAGING_DIR)/usr/include/libdrm
INCS += -I$(STAGING_DIR)/usr/include/drm

LIBPATH += -L$(STAGING_DIR)/usr/lib
LIBS += -llvgl -ldisplay -ldrm -lpthread -lm

SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)

# DSEG7 digital-tube fonts: generated C files (see font/gen_dseg7_fonts.py).
# Compiled as C (-x c) so the lv_font_t symbols stay unmangled and match
# LV_FONT_DECLARE's extern "C" declaration used from the C++ UI code.
FONT_SRCS := $(wildcard font/dseg7c*.c)
FONT_OBJS := $(FONT_SRCS:.c=.o)

all: $(BIN) $(CALIB)

$(OBJS): %.o: %.cpp *.h
	$(CXX) -c $(CXXFLAGS) $< -o $@ $(INCS)

$(FONT_OBJS): font/%.o: font/%.c
	$(CXX) -x c -c $(CXXFLAGS) $< -o $@ $(INCS)

$(BIN): $(OBJS) $(FONT_OBJS)
	$(CXX) $(LDFLAGS) -s -o $(BIN) $(OBJS) $(FONT_OBJS) $(LIBPATH) $(LIBS)

$(CALIB): calibd.c
	$(CXX) $(CXXFLAGS) -O2 -s -o $@ $<

clean:
	rm -f $(BIN) $(CALIB) $(OBJS) $(FONT_OBJS)
