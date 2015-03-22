CXX=clang++
CC=clang++

# Raspberry Pi tested with 2014-12-24-wheezy-raspbian but required changes are
# not included in the current sources.

#RASPBERRYPI=y
ifdef RASPBERRYPI
CXX=arm-linux-gnueabihf-g++
CC=arm-linux-gnueabihf-g++
SYSROOT=--sysroot=$(HOME)/raspberrypi/rootfs
endif

CPPFLAGS=-std=gnu++11 -MMD -MP $(SYSROOT)
LDFLAGS=-lrt -ljpeg -lzmq -lpthread $(SYSROOT)

#DEBUG=y

ifdef DEBUG
CPPFLAGS+=-fsanitize=address -fno-omit-frame-pointer -O1 -g
LDFLAGS+=-fsanitize=address -fno-omit-frame-pointer
else
CPPFLAGS+=-O3
endif

PROGS=capture
CAPTURE_SRCS=analyze.cpp  capture.cpp  frame.cpp  main.cpp  output.cpp
ALL_SRCS=$(CAPTURE_SRCS)

all: $(PROGS)

capture: $(CAPTURE_SRCS:.cpp=.o)

.PHONY: clean distclean
clean:
	@rm -f $(PROGS) $(ALL_SRCS:.cpp=.o) $(ALL_SRCS:.cpp=.d) *.pyc tags 
distclean: clean
	@rm -rf html/import

# dependency generation magick together with CPPFLAGS -MMD -MP
-include $(ALL_SRCS:.cpp=.d)
