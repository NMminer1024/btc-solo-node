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

all: $(BIN) $(CALIB)

$(OBJS): %.o: %.cpp *.h
	$(CXX) -c $(CXXFLAGS) $< -o $@ $(INCS)

$(BIN): $(OBJS)
	$(CXX) $(LDFLAGS) -s -o $(BIN) $(OBJS) $(LIBPATH) $(LIBS)

$(CALIB): calibd.c
	$(CXX) $(CXXFLAGS) -O2 -s -o $@ $<

clean:
	rm -f $(BIN) $(CALIB) $(OBJS)
