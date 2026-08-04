CC = gcc
PKG_CONFIG ?= pkg-config
HAMMER_ROOT ?= ../..
HAMMER_VARIANT ?= opt
HAMMER_BUILD ?= $(HAMMER_ROOT)/build/$(HAMMER_VARIANT)/src
LOCAL_INCLUDE_DIR = .hammer-local/include
LOCAL_HAMMER_INCLUDE = $(LOCAL_INCLUDE_DIR)/hammer

ifneq ($(wildcard $(HAMMER_BUILD)/libhammer.so $(HAMMER_BUILD)/libhammer.a),)
HAMMER_DEPS = $(LOCAL_HAMMER_INCLUDE)
HAMMER_CFLAGS = -I$(LOCAL_INCLUDE_DIR)
HAMMER_LDFLAGS = -L$(HAMMER_BUILD) -Wl,-rpath,$(abspath $(HAMMER_BUILD)) -lhammer
else
HAMMER_DEPS =
HAMMER_CFLAGS = $(shell $(PKG_CONFIG) --cflags libhammer)
HAMMER_LDFLAGS = $(shell $(PKG_CONFIG) --libs libhammer)
endif

CFLAGS = -g $(HAMMER_CFLAGS)
LDFLAGS = $(HAMMER_LDFLAGS)
TARGET = ntp_parser
SRC = main.c

all: $(TARGET)

$(TARGET): $(HAMMER_DEPS) $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

$(LOCAL_HAMMER_INCLUDE):
	mkdir -p $(LOCAL_INCLUDE_DIR)
	ln -sfn $(abspath $(HAMMER_ROOT)/src) $@

clean:
	rm -f $(TARGET)
	rm -rf .hammer-local

# Real NTP packet captured
testp: $(TARGET)
	cat packet/ntp_stream.bin | ./$(TARGET)
