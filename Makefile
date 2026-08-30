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
DC_GAMESTAT_GEN_TARGET := $(BUILD_DIR)/dc_gamestat_gen
MODEL_LIB_TARGET := $(LIBS_DIR)/libopen-rts-model.a
GAME_MODEL_TEST_TARGET := $(BIN_DIR)/test_game_model_headless
DC_LAYOUT_TEST_TARGET := $(BIN_DIR)/test_dark_colony_sprite_layout

DATA_DIR          := data
DARK_REIGN_ROOT   := $(DATA_DIR)/REIGN/dark
DARK_COLONY_ROOT  := $(DATA_DIR)/DCOLONY
KKND_ROOT         := $(DATA_DIR)/KKND

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
	client/game_ui.c \
	client/renderer_sdl.c \
	common/engine_base.c \
	common/engine_core.c \
	common/engine_path.c \
	common/engine_units.c \
	common/facing.c \
	common/plugin.c
MAIN_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(MAIN_SOURCES))
MAIN_DEPS    := $(MAIN_OBJECTS:.o=.d)

# ── dark-reign plugin ────────────────────────────────────────────────────────
DR_SOURCES := \
	games/DarkReign/plugin.c \
	games/DarkReign/dr_loader.c
DR_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DR_SOURCES))
DR_DEPS    := $(DR_OBJECTS:.o=.d)
DR_LIB     := $(LIBS_DIR)/dark-reign$(LIB_EXT)

# ── dark-colony plugin ───────────────────────────────────────────────────────
DC_SOURCES := \
	games/DarkColony/plugin.c \
	games/DarkColony/dc_loader.c \
	games/DarkColony/info.c
DC_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DC_SOURCES))
DC_DEPS    := $(DC_OBJECTS:.o=.d)
DC_LIB     := $(LIBS_DIR)/dark-colony$(LIB_EXT)

# ── 7th legion plugin ───────────────────────────────────────────────────────
SL_SOURCES := \
	games/7thLegion/plugin.c \
	games/7thLegion/sl_loader.c
SL_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SL_SOURCES))
SL_DEPS    := $(SL_OBJECTS:.o=.d)
SL_LIB     := $(LIBS_DIR)/7legion$(LIB_EXT)

# ── KKnD plugin ────────────────────────────────────────────────────────────────
KKND_SOURCES := \
	games/KKND/plugin.c \
	games/KKND/kknd_loader.c
KKND_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(KKND_SOURCES))
KKND_DEPS    := $(KKND_OBJECTS:.o=.d)
KKND_LIB     := $(LIBS_DIR)/kknd$(LIB_EXT)

# ── anim_extract tool ────────────────────────────────────────────────────────
ANIM_EXTRACT_SOURCE := tools/anim_extract.c
ANIM_EXTRACT_OBJECT := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ANIM_EXTRACT_SOURCE))
ANIM_EXTRACT_DEPS   := $(ANIM_EXTRACT_OBJECT:.o=.d)

DC_INFO_GEN_SOURCE := tools/dc_info_gen.c
DC_INFO_GEN_OBJECT := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DC_INFO_GEN_SOURCE))
DC_INFO_GEN_DEPS   := $(DC_INFO_GEN_OBJECT:.o=.d)

DC_GAMESTAT_GEN_SOURCE := tools/dc_gamestat_gen.c
DC_GAMESTAT_GEN_OBJECT := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DC_GAMESTAT_GEN_SOURCE))
DC_GAMESTAT_GEN_DEPS   := $(DC_GAMESTAT_GEN_OBJECT:.o=.d)

MODEL_SOURCES := \
	server/game_model.c \
	common/engine_base.c \
	common/engine_path.c \
	common/engine_units.c \
	common/facing.c \
	common/plugin.c
MODEL_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(MODEL_SOURCES))
MODEL_DEPS    := $(MODEL_OBJECTS:.o=.d)

GAME_MODEL_TEST_SOURCES := \
	tests/test_game_model_headless.c
GAME_MODEL_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(GAME_MODEL_TEST_SOURCES))
GAME_MODEL_TEST_DEPS    := $(GAME_MODEL_TEST_OBJECTS:.o=.d)

DC_LAYOUT_TEST_SOURCES := \
	tests/test_dark_colony_sprite_layout.c
DC_LAYOUT_TEST_OBJECTS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(DC_LAYOUT_TEST_SOURCES))
DC_LAYOUT_TEST_DEPS    := $(DC_LAYOUT_TEST_OBJECTS:.o=.d)

.PHONY: all run mission-1 mission-2 test test-headless dark-reign dark-colony dark-colony-human02 dark-colony-info dark-colony-gamestat 7legion kknd kknd-check anim-extract clean help

all: $(TARGET) $(DR_LIB) $(DC_LIB) $(SL_LIB) $(KKND_LIB) $(MODEL_LIB_TARGET)

# ── link main binary ─────────────────────────────────────────────────────────
$(TARGET): $(MAIN_OBJECTS) | $(BIN_DIR)
	$(CC) $(MAIN_OBJECTS) -o $@ $(SDL_LIBS) -lm -ldl

# ── link plugin shared libs ──────────────────────────────────────────────────
$(DR_LIB): $(DR_OBJECTS) | $(LIBS_DIR)
	$(CC) $(LIB_FLAGS) -fPIC $(DR_OBJECTS) -o $@ $(SDL_LIBS) -lm

$(DC_LIB): $(DC_OBJECTS) | $(LIBS_DIR)
	$(CC) $(LIB_FLAGS) -fPIC $(DC_OBJECTS) -o $@ $(SDL_LIBS) -lm

$(SL_LIB): $(SL_OBJECTS) | $(LIBS_DIR)
	$(CC) $(LIB_FLAGS) -fPIC $(SL_OBJECTS) -o $@ $(SDL_LIBS) -lm

$(KKND_LIB): $(KKND_OBJECTS) | $(LIBS_DIR)
	$(CC) $(LIB_FLAGS) -fPIC $(KKND_OBJECTS) -o $@ $(SDL_LIBS) -lm

# ── anim_extract ─────────────────────────────────────────────────────────────
$(ANIM_EXTRACT_TARGET): $(ANIM_EXTRACT_OBJECT)
	$(CC) $(ANIM_EXTRACT_OBJECT) -o $@

$(DC_INFO_GEN_TARGET): $(DC_INFO_GEN_OBJECT)
	$(CC) $(DC_INFO_GEN_OBJECT) -o $@

$(DC_GAMESTAT_GEN_TARGET): $(DC_GAMESTAT_GEN_OBJECT)
	$(CC) $(DC_GAMESTAT_GEN_OBJECT) -o $@

$(MODEL_LIB_TARGET): $(MODEL_OBJECTS) | $(LIBS_DIR)
	rm -f $@
	$(AR) rcs $@ $(MODEL_OBJECTS)

$(GAME_MODEL_TEST_TARGET): $(GAME_MODEL_TEST_OBJECTS) $(MODEL_LIB_TARGET) $(DC_LIB) $(DR_LIB) | $(BIN_DIR)
	$(CC) $(GAME_MODEL_TEST_OBJECTS) $(MODEL_LIB_TARGET) -o $@ -lm -ldl

$(DC_LAYOUT_TEST_TARGET): $(DC_LAYOUT_TEST_OBJECTS) $(BUILD_DIR)/games/DarkColony/info.o | $(BIN_DIR)
	$(CC) $(DC_LAYOUT_TEST_OBJECTS) $(BUILD_DIR)/games/DarkColony/info.o -o $@ -lm

dark-colony-info: $(DC_INFO_GEN_TARGET)
	$(DC_INFO_GEN_TARGET) $(DARK_COLONY_ROOT) games/DarkColony/info.h games/DarkColony/info.c

dark-colony-gamestat: $(DC_GAMESTAT_GEN_TARGET)
	$(DC_GAMESTAT_GEN_TARGET) $(DARK_COLONY_ROOT)/GAMESTAT games/DarkColony/gamestat.h

# ── compile rules ────────────────────────────────────────────────────────────
# Plugin objects need -fPIC for shared libs
$(BUILD_DIR)/games/%.o: games/%.c | $(BUILD_DIR)
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

mission-1: all
	$(TARGET) --game dark-reign $(DARK_REIGN_ROOT) scenario/FIXED/M01F/M01F.SCN ucfcnst0.spr

mission-2: all
	$(TARGET) --game dark-reign $(DARK_REIGN_ROOT) scenario/FIXED/M02F/M02F.SCN ucfcnst0.spr

dark-reign: all
	$(TARGET) --game dark-reign $(DARK_REIGN_ROOT) scenario/MULTI/2NIC/2NIC.SCN ucfcnst0.spr

dark-colony: all
	$(TARGET) --game dark-colony $(DARK_COLONY_ROOT) SCENARIO/HUMAN/HUMAN01.MAP SPRITES/TROOPER1.SPR

dark-colony-human02: all
	$(TARGET) --game dark-colony $(DARK_COLONY_ROOT) SCENARIO/HUMAN/HUMAN02.MAP SPRITES/TROOPER1.SPR

7legion: all
	$(TARGET) --game 7legion data/7LEGION

kknd: all
	$(TARGET) --game kknd

kknd-check: all
	env SDL_VIDEODRIVER=dummy $(TARGET) --check --game kknd

anim-extract: $(ANIM_EXTRACT_TARGET)

test: test-headless

test-headless: $(GAME_MODEL_TEST_TARGET) $(DC_LAYOUT_TEST_TARGET)
	$(GAME_MODEL_TEST_TARGET)
	$(DC_LAYOUT_TEST_TARGET)

help:
	@echo "Usage: make [target]"
	@echo ""
	@echo "Build:"
	@echo "  all                  Build binary + all plugins (default)"
	@echo "  clean                Remove build directory"
	@echo ""
	@echo "Run games:"
	@echo "  run                  Dark Reign (multiplayer map)"
	@echo "  dark-reign           Dark Reign (2NIC multiplayer map)"
	@echo "  mission-1            Dark Reign campaign mission 1"
	@echo "  mission-2            Dark Reign campaign mission 2"
	@echo "  dark-colony          Dark Colony HUMAN01 scenario"
	@echo "  dark-colony-human02  Dark Colony HUMAN02 scenario"
	@echo "  7legion              7th Legion"
	@echo "  kknd                 KKnD"
	@echo ""
	@echo "Test / check:"
	@echo "  test                 Run all headless tests"
	@echo "  kknd-check           Headless smoke check for KKnD"
	@echo ""
	@echo "Tools:"
	@echo "  anim-extract         Build the anim_extract tool"
	@echo "  dark-colony-info     Regenerate Dark Colony info.h/info.c from game data"
	@echo "  dark-colony-gamestat Regenerate Dark Colony gamestat.h from game data"
	@echo ""
	@echo "Run the binary directly for extra flags:"
	@echo "  build/bin/open-rts --game 7legion data/7LEGION --software"
	@echo "  build/bin/open-rts --game dark-colony ... --software   (fix Metal/GPU tile bugs)"
	@echo "  env SDL_VIDEODRIVER=dummy build/bin/open-rts --check --game dark-colony"
	@echo "  env SDL_VIDEODRIVER=dummy build/bin/open-rts --screenshot out.bmp --game kknd"

clean:
	rm -rf $(BUILD_DIR)

-include $(MAIN_DEPS)
-include $(DR_DEPS)
-include $(DC_DEPS)
-include $(SL_DEPS)
-include $(KKND_DEPS)
-include $(ANIM_EXTRACT_DEPS)
-include $(DC_INFO_GEN_DEPS)
-include $(DC_GAMESTAT_GEN_DEPS)
-include $(MODEL_DEPS)
-include $(GAME_MODEL_TEST_DEPS)
-include $(DC_LAYOUT_TEST_DEPS)
