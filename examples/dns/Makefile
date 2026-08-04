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

CFLAGS = -g -Wall -I. $(HAMMER_CFLAGS)
LDFLAGS = $(HAMMER_LDFLAGS)

TARGET = dns_parser
SRCS = main.c dns.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(HAMMER_DEPS) $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c dns.h $(HAMMER_DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

$(LOCAL_HAMMER_INCLUDE):
	mkdir -p $(LOCAL_INCLUDE_DIR)
	ln -sfn $(abspath $(HAMMER_ROOT)/src) $@

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf .hammer-local
