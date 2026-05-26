CC ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -I./src
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)

BUILD_DIR := build
TARGET := $(BUILD_DIR)/open-rts
SOURCES := \
	src/main.c \
	src/engine.c \
	src/plugin.c \
	src/renderer_sdl.c \
	plugins/DarkReign/plugin.c \
	plugins/DarkColony/plugin.c
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
DATA_DIR := data
DARK_REIGN_ROOT := $(DATA_DIR)/REIGN/dark
DARK_COLONY_ROOT := $(DATA_DIR)/DCOLONY

.PHONY: all run dark-reign dark-colony build-dark-reign build-dark-colony clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(SDL_LIBS) -lm

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	$(TARGET)

dark-reign: $(TARGET)
	$(TARGET) --game dark-reign $(DARK_REIGN_ROOT) scenario/MULTI/2NIC/2NIC.MAP ucfcnst0.spr

dark-colony: $(TARGET)
	$(TARGET) --game dark-colony $(DARK_COLONY_ROOT) SCENARIO/MPLAYER/D2PLAY01.MAP SPRITES/TROOPER1.SPR

build-dark-reign: dark-reign

build-dark-colony: dark-colony

clean:
	rm -rf $(BUILD_DIR)
