PROG			= exodus
CC        = gcc
SRC_DIR   = src
BUILD_DIR = build
PREFIX    = /usr/local

CFLAGS    = $(shell pkg-config --cflags sqlite3)
LIBS      = $(shell pkg-config --libs sqlite3)

KIK_DEV_CFLAGS  ?= -std=c23 -D_POSIX_C_SOURCE=200809L -O0 -Wall -Wextra -Wpedantic -Wformat=2 -Werror -g3 -ggdb3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address,undefined,pointer-compare -fno-stack-clash-protection -fstack-check
KIK_PROD_CFLAGS ?= -std=c23 -D_POSIX_C_SOURCE=200809L -O2 -pipe -march=native

FILES     = $(wildcard $(SRC_DIR)/**/*.c) $(wildcard $(SRC_DIR)/*.c)
OBJ       = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(FILES))
OBJDEV    = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o-dev, $(FILES))

.PHONY: all dev install clean analyze

all: $(BUILD_DIR)/$(PROG)

$(BUILD_DIR)/$(PROG): $(OBJ)
	@mkdir -p $(dir $@)
	$(CC) $(KIK_PROD_CFLAGS) $(CFLAGS) $^ -o $@ $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KIK_PROD_CFLAGS) $(CFLAGS) -c $< -o $@

dev: $(BUILD_DIR)/$(PROG)-dev
	@which ctags &>/dev/null && ctags --kinds-C=+p $(FILES) $(wildcard $(SRC_DIR)/**/*.h) $(wildcard $(SRC_DIR)/*.h) $(shell gcc -M $(FILES) $(CFLAGS) $(LIBS) | sed -e 's/[\\ ]/\n/g' | sed -e '/^$$/d' -e '/\.o:[ \t]*$$/d' | sort | uniq) || exit 0

$(BUILD_DIR)/$(PROG)-dev: $(OBJDEV)
	@mkdir -p $(dir $@)
	$(CC) $(KIK_DEV_CFLAGS) $(CFLAGS) $^ -o $@ $(LIBS)

$(BUILD_DIR)/%.o-dev: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(KIK_DEV_CFLAGS) $(CFLAGS) -c $< -o $@

install: $(BUILD_DIR)/$(PROG)
	install -D $< $(PREFIX)/bin/$(PROG)

clean:
	rm -rf $(BUILD_DIR)

analyze:
	scan-build clang $(KIK_PROD_CFLAGS) $(CFLAGS) $(FILES) -o /dev/null $(LIBS)
