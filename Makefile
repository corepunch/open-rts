CC ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -I./src
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)

BUILD_DIR := build
TARGET := $(BUILD_DIR)/open-rts
SOURCES := src/main.c src/engine.c
OBJECTS := $(SOURCES:src/%.c=$(BUILD_DIR)/%.o)

.PHONY: all run dark-reign dark-colony build-dark-reign build-dark-colony clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(SDL_LIBS) -lm

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	$(TARGET)

dark-reign: $(TARGET)
	$(TARGET) --game dark-reign /Users/igor/Downloads/REIGN/dark scenario/MULTI/2NIC/2NIC.MAP ucfcnst0.spr

dark-colony: $(TARGET)
	$(TARGET) --game dark-colony /Users/igor/Downloads/DCOLONY SCENARIO/MPLAYER/D2PLAY01.MAP SPRITES/TROOPER1.SPR

build-dark-reign: dark-reign

build-dark-colony: dark-colony

clean:
	rm -rf $(BUILD_DIR)
