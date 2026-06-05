CC ?= cc
AR ?= ar
PKG_CONFIG ?= pkg-config

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -I./common -I./client -I./server -I./utility
DEPFLAGS = -MMD -MP
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)

BUILD_DIR     := build
BIN_DIR       := $(BUILD_DIR)/bin
LIBS_DIR      := $(BUILD_DIR)/libs
TARGET        := $(BIN_DIR)/open-rts
ANIM_EXTRACT_TARGET := $(BUILD_DIR)/anim_extract
DC_INFO_GEN_TARGET := $(BUILD_DIR)/dc_info_gen
MODEL_LIB_TARGET := $(LIBS_DIR)/libopen-rts-model.a
GAME_MODEL_TEST_TARGET := $(BIN_DIR)/test_game_model_headless

DATA_DIR          := data
DARK_REIGN_ROOT   := $(DATA_DIR)/REIGN/dark
DARK_COLONY_ROOT  := $(DATA_DIR)/DCOLONY

# Shared library extension (dylib on macOS, so on Linux)
UNAME := $(shell uname)
ifeq ($(UNAME),Darwin)
  LIB_EXT      := .dylib
  LIB_FLAGS    := -dynamiclib -undefined dynamic_lookup
else
  LIB_EXT      := .so
  LIB_FLAGS    := -shared
endif

# ── main binary ─────────────────────────────────────────────────────────────
MAIN_SOURCES := \
	client/main.c \
	client/engine_view.c \
	client/renderer_sdl.c \
	common/engine_base.c \
	common/engine_core.c \
	common/engine_path.c \
	common/engine_units.c \
	common/plugin.c
MAIN_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(MAIN_SOURCES))
MAIN_DEPS    := $(MAIN_OBJECTS:.o=.d)

# ── dark-reign plugin ────────────────────────────────────────────────────────
DR_SOURCES := \
	plugins/DarkReign/plugin.c \
	plugins/DarkReign/dr_loader.c
DR_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DR_SOURCES))
DR_DEPS    := $(DR_OBJECTS:.o=.d)
DR_LIB     := $(LIBS_DIR)/dark-reign$(LIB_EXT)

# ── dark-colony plugin ───────────────────────────────────────────────────────
DC_SOURCES := \
	plugins/DarkColony/plugin.c \
	plugins/DarkColony/dc_loader.c \
	plugins/DarkColony/info.c
DC_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DC_SOURCES))
DC_DEPS    := $(DC_OBJECTS:.o=.d)
DC_LIB     := $(LIBS_DIR)/dark-colony$(LIB_EXT)

# ── anim_extract tool ────────────────────────────────────────────────────────
ANIM_EXTRACT_SOURCE := tools/anim_extract.c
ANIM_EXTRACT_OBJECT := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ANIM_EXTRACT_SOURCE))
ANIM_EXTRACT_DEPS   := $(ANIM_EXTRACT_OBJECT:.o=.d)

DC_INFO_GEN_SOURCE := tools/dc_info_gen.c
DC_INFO_GEN_OBJECT := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DC_INFO_GEN_SOURCE))
DC_INFO_GEN_DEPS   := $(DC_INFO_GEN_OBJECT:.o=.d)

MODEL_SOURCES := \
	server/game_model.c \
	common/engine_base.c \
	common/engine_path.c \
	common/engine_units.c \
	common/plugin.c
MODEL_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(MODEL_SOURCES))
MODEL_DEPS    := $(MODEL_OBJECTS:.o=.d)

GAME_MODEL_TEST_SOURCES := \
	tests/test_game_model_headless.c
GAME_MODEL_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(GAME_MODEL_TEST_SOURCES))
GAME_MODEL_TEST_DEPS    := $(GAME_MODEL_TEST_OBJECTS:.o=.d)

.PHONY: all run test-headless dark-reign dark-colony dark-colony-human02 dark-colony-info anim-extract clean

all: $(TARGET) $(DR_LIB) $(DC_LIB) $(MODEL_LIB_TARGET)

# ── link main binary ─────────────────────────────────────────────────────────
$(TARGET): $(MAIN_OBJECTS) | $(BIN_DIR)
	$(CC) $(MAIN_OBJECTS) -o $@ $(SDL_LIBS) -lm -ldl

# ── link plugin shared libs ──────────────────────────────────────────────────
$(DR_LIB): $(DR_OBJECTS) | $(LIBS_DIR)
	$(CC) $(LIB_FLAGS) -fPIC $(DR_OBJECTS) -o $@ $(SDL_LIBS) -lm

$(DC_LIB): $(DC_OBJECTS) | $(LIBS_DIR)
	$(CC) $(LIB_FLAGS) -fPIC $(DC_OBJECTS) -o $@ $(SDL_LIBS) -lm

# ── anim_extract ─────────────────────────────────────────────────────────────
$(ANIM_EXTRACT_TARGET): $(ANIM_EXTRACT_OBJECT)
	$(CC) $(ANIM_EXTRACT_OBJECT) -o $@

$(DC_INFO_GEN_TARGET): $(DC_INFO_GEN_OBJECT)
	$(CC) $(DC_INFO_GEN_OBJECT) -o $@

$(MODEL_LIB_TARGET): $(MODEL_OBJECTS) | $(LIBS_DIR)
	rm -f $@
	$(AR) rcs $@ $(MODEL_OBJECTS)

$(GAME_MODEL_TEST_TARGET): $(GAME_MODEL_TEST_OBJECTS) $(MODEL_LIB_TARGET) $(DC_LIB) | $(BIN_DIR)
	$(CC) $(GAME_MODEL_TEST_OBJECTS) $(MODEL_LIB_TARGET) -o $@ -lm -ldl

dark-colony-info: $(DC_INFO_GEN_TARGET)
	$(DC_INFO_GEN_TARGET) $(DARK_COLONY_ROOT) plugins/DarkColony/info.h plugins/DarkColony/info.c

# ── compile rules ────────────────────────────────────────────────────────────
# Plugin objects need -fPIC for shared libs
$(BUILD_DIR)/plugins/%.o: plugins/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -fPIC -c $< -o $@

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(LIBS_DIR):
	mkdir -p $(LIBS_DIR)

# ── run targets ──────────────────────────────────────────────────────────────
run: all
	$(TARGET) --game dark-reign

dark-reign: all
	$(TARGET) --game dark-reign $(DARK_REIGN_ROOT) scenario/MULTI/2NIC/2NIC.SCN ucfcnst0.spr

dark-colony: all
	$(TARGET) --game dark-colony $(DARK_COLONY_ROOT) SCENARIO/HUMAN/HUMAN01.MAP SPRITES/TROOPER1.SPR

dark-colony-human02: all
	$(TARGET) --game dark-colony $(DARK_COLONY_ROOT) SCENARIO/HUMAN/HUMAN02.MAP SPRITES/TROOPER1.SPR

anim-extract: $(ANIM_EXTRACT_TARGET)

test-headless: $(GAME_MODEL_TEST_TARGET)
	$(GAME_MODEL_TEST_TARGET)

clean:
	rm -rf $(BUILD_DIR)

-include $(MAIN_DEPS)
-include $(DR_DEPS)
-include $(DC_DEPS)
-include $(ANIM_EXTRACT_DEPS)
-include $(DC_INFO_GEN_DEPS)
-include $(MODEL_DEPS)
-include $(GAME_MODEL_TEST_DEPS)
