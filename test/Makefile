# didimdwiki test suites.
#
#   make unit         Unity  - pure functions, no fixtures
#   make integration  Unity  - models against a scratch data directory
#   make e2e          curl   - a live server
#   make all          unit + integration
#
# Nothing from the web framework is linked in: everything under test is
# either a model or a pure function, and the routes and controllers are
# covered by the e2e suite instead.

VENDORIZE_UNITY ?= 1
VENDORIZE_MD4C  ?= 0

BUILD = build

ifeq ($(VENDORIZE_UNITY),1)
UNITY_DIR  := ../external/unity
UNITY_OBJS := $(BUILD)/unity.o
else
UNITY_DIR  := /usr/local/include/unity
UNITY_OBJS :=
LDLIBS     += -L/usr/local/lib -lunity
endif

ifeq ($(VENDORIZE_MD4C),1)
MD4C_DIR  := ../external/md4c
CFLAGS    += -I$(MD4C_DIR)
MD4C_OBJS := $(BUILD)/md4c.o $(BUILD)/md4c-html.o $(BUILD)/entity.o
else
MD4C_DIR  :=
MD4C_OBJS :=
LDLIBS    += -lmd4c -lmd4c-html
endif

APP_SRC = ../src

CC      ?= cc
CFLAGS  += -std=c99 -D_POSIX_C_SOURCE=200809L -g -O0 -Wall -Wextra -Wno-unused-parameter
CFLAGS  += -I$(APP_SRC) -I$(APP_SRC)/includes -I$(UNITY_DIR) -Isupport
LDLIBS  += -largon2

# Support code the models are written against.
LIB_OBJS = \
	$(BUILD)/wbuf.o $(BUILD)/xmalloc.o $(BUILD)/strutil.o \
	$(BUILD)/form.o $(BUILD)/validate.o

# App code under test. Anything touching ecewo is excluded: routes and
# controllers are covered by the e2e suite instead.
APP_OBJS = \
	$(BUILD)/store.o $(BUILD)/page.o $(BUILD)/revision.o \
	$(BUILD)/upload.o $(BUILD)/user.o $(BUILD)/session.o \
	$(BUILD)/diff.o $(BUILD)/helpers.o $(BUILD)/markdown.o \
	$(LIB_OBJS) \
	$(BUILD)/test_support.o \
	$(BUILD)/app_globals.o

# Same, minus store.o: test_page_mocked links a stub instead. An object file
# provides all of its symbols or none, so the real one has to come out.
APP_OBJS_NO_STORE = $(filter-out $(BUILD)/store.o,$(APP_OBJS))

UNIT_SRCS = $(wildcard unit/test_*.c)
UNIT_BINS = $(patsubst unit/%.c,$(BUILD)/%,$(UNIT_SRCS))

INT_SRCS = $(filter-out integration/test_page_mocked.c,$(wildcard integration/test_*.c))
INT_BINS = $(patsubst integration/%.c,$(BUILD)/%,$(INT_SRCS)) $(BUILD)/test_page_mocked

.PHONY: all unit integration e2e clean

all: unit integration

$(BUILD):
	@mkdir -p $@

# ---------------------------------------------------------------- objects

$(BUILD)/%.o: support/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/unity.o: $(UNITY_DIR)/unity.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(APP_SRC)/models/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(APP_SRC)/views/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(APP_SRC)/lib/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(MD4C_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# ------------------------------------------------------------------ unity

$(BUILD)/test_%: unit/test_%.c $(UNITY_OBJS) $(APP_OBJS) $(MD4C_OBJS)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

unit: $(UNIT_BINS)
	@echo "== unit =="
	@fail=0; for t in $(UNIT_BINS); do ./$$t || fail=1; done; exit $$fail

# ----------------------------------------------------------- integration

$(BUILD)/test_%: integration/test_%.c $(UNITY_OBJS) $(APP_OBJS) $(MD4C_OBJS)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

$(BUILD)/test_page_mocked: integration/test_page_mocked.c $(UNITY_OBJS) \
    $(BUILD)/store_stub.o $(APP_OBJS_NO_STORE) $(MD4C_OBJS)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

integration: $(INT_BINS)
	@echo "== integration =="
	@fail=0; for t in $(INT_BINS); do ./$$t || fail=1; done; exit $$fail

# -------------------------------------------------------------------- e2e

e2e:
	@./e2e/run.sh

clean:
	rm -rf $(BUILD)
