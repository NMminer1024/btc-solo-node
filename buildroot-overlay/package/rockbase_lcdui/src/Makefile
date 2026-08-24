BIN = rockbase-lcdui

INCS += -I$(STAGING_DIR)/usr/include
INCS += -I$(STAGING_DIR)/usr/include/lvgl
INCS += -I$(STAGING_DIR)/usr/include/libdrm
INCS += -I$(STAGING_DIR)/usr/include/drm

LIBPATH += -L$(STAGING_DIR)/usr/lib
LIBS += -llvgl -ldisplay -ldrm -lpthread -lm

SRCS := $(wildcard *.cpp)
OBJS := $(SRCS:.cpp=.o)

all: $(BIN)

$(OBJS): %.o: %.cpp *.h
	$(CXX) -c $(CXXFLAGS) $< -o $@ $(INCS)

$(BIN): $(OBJS)
	$(CXX) $(LDFLAGS) -o $(BIN) $(OBJS) $(LIBPATH) $(LIBS)

clean:
	rm -f $(BIN) $(OBJS)
