.PHONY: all build run clean test test-unit test-integration test-e2e help


APP        := didimdwiki
DEBUG      ?= 0

BUILD      := .objs

# ------------------------------------------------------------------ sources

SRCS := $(wildcard src/*.c src/lib/*.c src/models/*.c src/views/*.c \
	src/middleware/*.c src/controllers/*.c src/routes/*.c)
OBJS := $(patsubst %.c,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# -------------------------------------------------------------- dependencies

ECEWO_PKGS := ecewo ecewo-cookie ecewo-fs ecewo-static ecewo-multipart \
	ecewo-helmet

PKGS := $(ECEWO_PKGS) libcjson

PKG_CFLAGS := $(shell pkg-config --cflags $(PKGS) 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs $(PKGS) 2>/dev/null)

# md4c and argon2 ship no pkg-config file on Debian.
# -lm is for libecewo, which uses log() but does not record the dependency.
EXTRA_LIBS := -lmd4c -lmd4c-html -largon2 -lm

# -------------------------------------------------------------------- flags

ifeq ($(DEBUG),1)
OPTFLAGS := -g -O0
else
OPTFLAGS := -O2 -DNDEBUG
endif

CFLAGS  += -std=c99 -D_POSIX_C_SOURCE=200809L
CFLAGS  += $(OPTFLAGS) -Wall -Wextra -MMD -MP
CFLAGS  += -Isrc/includes $(PKG_CFLAGS)
LDLIBS  += $(PKG_LIBS) $(EXTRA_LIBS)

# ------------------------------------------------------------------ targets

all: build

build: $(APP)

$(APP): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDLIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

run: build
	./$(APP) login_required=1 allow_registration=0

# ------------------------------------------------------------------- tests

test:
	$(MAKE) -C test all

test-unit:
	$(MAKE) -C test unit

test-integration:
	$(MAKE) -C test integration

test-e2e: build
	./test/e2e/run.sh

# ------------------------------------------------------------------- chores

clean:
	rm -rf $(BUILD) $(APP)
	-$(MAKE) -C test clean

help:
	@echo "build              compile ./$(APP)            (DEBUG=1 for a debug build)"
	@echo "run                build, then run it locally"
	@echo "test               unit + integration"
	@echo "test-unit          pure-function tests"
	@echo "test-integration   model tests against a scratch dir"
	@echo "test-e2e           live server + curl"
	@echo "clean              remove objects and the binary"

-include $(DEPS)
