RGB_CYCLE_DIR_NAME ?= rgb_cycle
RGB_CYCLE_DEMO_DIR ?= ${shell pwd}

BIN = rgb-cycle

INCS += -I./include -I$(STAGING_DIR)/usr/include -I$(STAGING_DIR)/usr/include/libdrm -I$(STAGING_DIR)/usr/include/drm
LIBPATH += -L$(STAGING_DIR)/usr/lib -L./

LIBS += -ldisplay -ldrm -lpthread

SRCS := $(wildcard *.cpp)
CPPOBJS := $(SRCS:.cpp=.o)

all: $(BIN)
	@rm -f $(CPPOBJS)

$(CPPOBJS): %.o: %.cpp
	$(CPP) -c $(CFLAGS) $< -o $@ $(INCS)

$(BIN): $(CPPOBJS)
	$(CPP) $(LDFLAGS) -o $(BIN) $(CPPOBJS) $(LIBPATH) $(LIBS)
	@rm -f $(CPPOBJS)

clean:
	@rm -f $(BIN) $(CPPOBJS)
