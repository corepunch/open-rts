CC ?= cc
PKG_CONFIG ?= pkg-config

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -I./src
DEPFLAGS = -MMD -MP
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)

BUILD_DIR := build
TARGET := $(BUILD_DIR)/open-rts
ANIM_EXTRACT_TARGET := $(BUILD_DIR)/anim_extract
SOURCES := \
	src/main.c \
	src/engine.c \
	src/plugin.c \
	src/renderer_sdl.c \
	plugins/DarkReign/plugin.c \
	plugins/DarkColony/plugin.c
OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SOURCES))
DEPS := $(OBJECTS:.o=.d)
ANIM_EXTRACT_SOURCE := tools/anim_extract.c
ANIM_EXTRACT_OBJECT := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ANIM_EXTRACT_SOURCE))
ANIM_EXTRACT_DEPS := $(ANIM_EXTRACT_OBJECT:.o=.d)
DATA_DIR := data
DARK_REIGN_ROOT := $(DATA_DIR)/REIGN/dark
DARK_COLONY_ROOT := $(DATA_DIR)/DCOLONY

.PHONY: all run dark-reign dark-colony build-dark-reign build-dark-colony anim-extract clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(SDL_LIBS) -lm

$(ANIM_EXTRACT_TARGET): $(ANIM_EXTRACT_OBJECT)
	$(CC) $(ANIM_EXTRACT_OBJECT) -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -c $< -o $@

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

-include $(DEPS)
-include $(ANIM_EXTRACT_DEPS)
