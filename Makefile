CC ?= cc
AR ?= ar
PKG_CONFIG ?= pkg-config

CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -Idriver -Igame -Iplay -Irender -Iinterface -Ihud -Itests
DEPFLAGS = -MMD -MP
SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2)
SDL_LIBS := $(shell $(PKG_CONFIG) --libs sdl2)

BUILD_DIR := build
BIN_DIR   := $(BUILD_DIR)/bin

ANIM_EXTRACT_TARGET    := $(BUILD_DIR)/anim_extract
DC_INFO_GEN_TARGET     := $(BUILD_DIR)/dc_info_gen
DC_GAMESTAT_GEN_TARGET := $(BUILD_DIR)/dc_gamestat_gen
DC_LAYOUT_TEST_TARGET  := $(BIN_DIR)/test_dark_colony_sprite_layout
MODEL_COMMAND_TEST_SOURCE := tests/cross-game/test_model_commands.c

DATA_DIR         := data
DARK_REIGN_ROOT  := $(DATA_DIR)/REIGN/dark
DARK_COLONY_ROOT := $(DATA_DIR)/DCOLONY
KKND_ROOT        := $(DATA_DIR)/KKND

# ── per-game engine sources (shared by all game binaries) ────────────────────
ENGINE_SOURCES := $(sort $(shell find driver render hud interface play game -name '*.c'))

# ── game-specific sources ────────────────────────────────────────────────────
DR_GAME_SOURCES   := $(sort $(shell find games/dark-reign  -name '*.c'))
DC_GAME_SOURCES   := $(sort $(shell find games/dark-colony -name '*.c'))
SL_GAME_SOURCES   := $(sort $(shell find games/7legion     -name '*.c'))
KKND_GAME_SOURCES := $(sort $(shell find games/kknd        -name '*.c'))

# ── model engine sources (headless: no SDL display entry point or HUD) ───────
MODEL_ENGINE_SOURCES := $(sort $(shell find game driver play render -name '*.c' ! -name 'd_main.c'))

# ── tool sources ─────────────────────────────────────────────────────────────
ANIM_EXTRACT_SOURCE  := tools/anim_extract.c
DC_INFO_GEN_SOURCE   := tools/dc_info_gen.c
DC_GAMESTAT_GEN_SOURCE := tools/dc_gamestat_gen.c

DC_LAYOUT_TEST_SOURCE := tests/test_dark_colony_sprite_layout.c

.PHONY: all run mission-1 mission-2 test test-dark-colony test-dark-reign test-7legion test-kknd \
        test-headless test-model-commands test-ai test-layout dark-reign dark-colony \
        dark-colony-human02 dark-colony-human03 dark-colony-info dark-colony-gamestat 7legion kknd \
        kknd-check anim-extract clean help

# ── per-game binary rule template ────────────────────────────────────────────
# $(1) = binary name (e.g. dark-colony)
# $(2) = game-specific source list
# $(3) = game directory name (e.g. DarkColony)
# $(4) = game-specific preprocessor flags

define GAME_TARGET

ALL_SRCS_$(1) := $$(ENGINE_SOURCES) $(2)
ALL_OBJS_$(1) := $$(patsubst %.c,$(BUILD_DIR)/$(1)/%.o,$$(ALL_SRCS_$(1)))
ALL_DEPS_$(1) := $$(ALL_OBJS_$(1):.o=.d)

$(BIN_DIR)/$(1): $$(ALL_OBJS_$(1)) | $(BIN_DIR)
	$(CC) $$^ -o $$@ $(SDL_LIBS) -lm

$(BUILD_DIR)/$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	$(CC) $(CPPFLAGS) $(4) -I./games/$(3) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -c $$< -o $$@

-include $$(ALL_DEPS_$(1))
endef

$(eval $(call GAME_TARGET,dark-colony,$(DC_GAME_SOURCES),dark-colony,-DRTS_WORLD_Y_UP=1))
$(eval $(call GAME_TARGET,dark-reign,$(DR_GAME_SOURCES),dark-reign,-DRTS_WORLD_Y_UP=0))
$(eval $(call GAME_TARGET,7legion,$(SL_GAME_SOURCES),7legion,-DRTS_WORLD_Y_UP=0))
$(eval $(call GAME_TARGET,kknd,$(KKND_GAME_SOURCES),kknd,-DRTS_WORLD_Y_UP=0))

all: $(BIN_DIR)/dark-colony $(BIN_DIR)/dark-reign $(BIN_DIR)/7legion $(BIN_DIR)/kknd

.SECONDARY:

define MODEL_TESTS_FOR_GAME
$(1)_TEST_SOURCES := $$(wildcard tests/$(1)/test_*.c)
$(1)_TEST_ENGINE_OBJS := $$(patsubst %.c,$(BUILD_DIR)/model-test-$(1)/%.o,$(MODEL_ENGINE_SOURCES) $(2))
$(1)_TEST_BINS := $$(patsubst tests/$(1)/%.c,$(BIN_DIR)/tests/$(1)/%,$$($(1)_TEST_SOURCES))
$(BUILD_DIR)/model-test-$(1)/%.o: %.c
	@mkdir -p $$(dir $$@)
	$(CC) $(CPPFLAGS) $(3) -I./games/$(1) $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -c $$< -o $$@
$(BIN_DIR)/tests/$(1)/%: $(BUILD_DIR)/model-test-$(1)/tests/$(1)/%.o $$($(1)_TEST_ENGINE_OBJS) | $(BIN_DIR)
	@mkdir -p $$(dir $$@)
	$(CC) $$^ -o $$@ $(SDL_LIBS) -lm
test-$(1): $$($(1)_TEST_BINS)
	@ok=1; for t in $$($(1)_TEST_BINS); do echo "-- $$$$t --"; $$$$t || ok=0; done; test "$$$$ok" = 1
endef

$(eval $(call MODEL_TESTS_FOR_GAME,dark-colony,$(DC_GAME_SOURCES),-DRTS_WORLD_Y_UP=1 -DRTS_GAME_DARK_COLONY))
$(eval $(call MODEL_TESTS_FOR_GAME,dark-reign,$(DR_GAME_SOURCES),-DRTS_WORLD_Y_UP=0 -DRTS_GAME_DARK_REIGN))
$(eval $(call MODEL_TESTS_FOR_GAME,7legion,$(SL_GAME_SOURCES),-DRTS_WORLD_Y_UP=0 -DRTS_GAME_7LEGION))
$(eval $(call MODEL_TESTS_FOR_GAME,kknd,$(KKND_GAME_SOURCES),-DRTS_WORLD_Y_UP=0 -DRTS_GAME_KKND))

# ── DC layout test ───────────────────────────────────────────────────────────
DC_LAYOUT_TEST_OBJ := $(BUILD_DIR)/dc-test/$(DC_LAYOUT_TEST_SOURCE:.c=.o)
DC_LAYOUT_DC_INFO_OBJ := $(BUILD_DIR)/dc-test/games/dark-colony/info.o

$(BUILD_DIR)/dc-test/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -DRTS_WORLD_Y_UP=1 -I./games/dark-colony $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(DC_LAYOUT_TEST_TARGET): $(DC_LAYOUT_TEST_OBJ) $(DC_LAYOUT_DC_INFO_OBJ) | $(BIN_DIR)
	$(CC) $^ -o $@ -lm

test-layout: $(DC_LAYOUT_TEST_TARGET)
	$(DC_LAYOUT_TEST_TARGET)

# ── anim_extract and info generators ─────────────────────────────────────────
$(ANIM_EXTRACT_TARGET): $(BUILD_DIR)/tools/anim_extract.o
	$(CC) $^ -o $@

$(DC_INFO_GEN_TARGET): $(BUILD_DIR)/tools/dc_info_gen.o
	$(CC) $^ -o $@

$(DC_GAMESTAT_GEN_TARGET): $(BUILD_DIR)/tools/dc_gamestat_gen.o
	$(CC) $^ -o $@

$(BUILD_DIR)/tools/%.o: tools/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

-include $(BUILD_DIR)/tools/anim_extract.d
-include $(BUILD_DIR)/tools/dc_info_gen.d
-include $(BUILD_DIR)/tools/dc_gamestat_gen.d

# ── dark-colony-info / dark-colony-gamestat ───────────────────────────────────
dark-colony-info: $(DC_INFO_GEN_TARGET)
	$(DC_INFO_GEN_TARGET) $(DARK_COLONY_ROOT) games/dark-colony/info.h games/dark-colony/info.c

dark-colony-gamestat: $(DC_GAMESTAT_GEN_TARGET)
	$(DC_GAMESTAT_GEN_TARGET) $(DARK_COLONY_ROOT)/GAMESTAT games/dark-colony/gamestat.h

# ── run targets ───────────────────────────────────────────────────────────────
run: $(BIN_DIR)/dark-reign
	$(BIN_DIR)/dark-reign

mission-1: $(BIN_DIR)/dark-reign
	$(BIN_DIR)/dark-reign $(DARK_REIGN_ROOT) scenario/FIXED/M01F/M01F.SCN ucfcnst0.spr

mission-2: $(BIN_DIR)/dark-reign
	$(BIN_DIR)/dark-reign $(DARK_REIGN_ROOT) scenario/FIXED/M02F/M02F.SCN ucfcnst0.spr

dark-reign: $(BIN_DIR)/dark-reign
	$(BIN_DIR)/dark-reign $(DARK_REIGN_ROOT) scenario/MULTI/2NIC/2NIC.SCN ucfcnst0.spr

dark-colony: $(BIN_DIR)/dark-colony
	$(BIN_DIR)/dark-colony $(DARK_COLONY_ROOT) SCENARIO/HUMAN/HUMAN01.MAP SPRITES/TROOPER1.SPR

dark-colony-human02: $(BIN_DIR)/dark-colony
	$(BIN_DIR)/dark-colony $(DARK_COLONY_ROOT) SCENARIO/HUMAN/HUMAN02.MAP SPRITES/TROOPER1.SPR

dark-colony-human03: $(BIN_DIR)/dark-colony
	$(BIN_DIR)/dark-colony $(DARK_COLONY_ROOT) SCENARIO/HUMAN/HUMAN03.MAP SPRITES/TROOPER1.SPR

7legion: $(BIN_DIR)/7legion
	$(BIN_DIR)/7legion data/7LEGION

kknd: $(BIN_DIR)/kknd
	$(BIN_DIR)/kknd

kknd-check: $(BIN_DIR)/kknd
	env SDL_VIDEODRIVER=dummy $(BIN_DIR)/kknd --check

anim-extract: $(ANIM_EXTRACT_TARGET)

test: test-dark-colony test-dark-reign test-7legion test-kknd test-model-commands test-layout

test-headless: test-dark-colony
test-ai: test-dark-colony

$(BIN_DIR)/test_model_commands_dark-colony: $(BUILD_DIR)/cmd-dc/$(MODEL_COMMAND_TEST_SOURCE:.c=.o) $(patsubst %.c,$(BUILD_DIR)/cmd-dc/%.o,$(MODEL_ENGINE_SOURCES) $(DC_GAME_SOURCES)) | $(BIN_DIR)
	$(CC) $^ -o $@ $(SDL_LIBS) -lm

$(BUILD_DIR)/cmd-dc/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -DRTS_WORLD_Y_UP=1 -DRTS_GAME_DARK_COLONY -I./games/dark-colony $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -c $< -o $@

$(BIN_DIR)/test_model_commands_dark-reign: $(BUILD_DIR)/cmd-dr/$(MODEL_COMMAND_TEST_SOURCE:.c=.o) $(patsubst %.c,$(BUILD_DIR)/cmd-dr/%.o,$(MODEL_ENGINE_SOURCES) $(DR_GAME_SOURCES)) | $(BIN_DIR)
	$(CC) $^ -o $@ $(SDL_LIBS) -lm

$(BUILD_DIR)/cmd-dr/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) -DRTS_WORLD_Y_UP=0 -DRTS_GAME_DARK_REIGN -I./games/dark-reign $(CFLAGS) $(DEPFLAGS) $(SDL_CFLAGS) -c $< -o $@

test-model-commands: $(BIN_DIR)/test_model_commands_dark-colony $(BIN_DIR)/test_model_commands_dark-reign
	$(BIN_DIR)/test_model_commands_dark-colony
	$(BIN_DIR)/test_model_commands_dark-reign

help:
	@echo "Usage: make [target]"
	@echo ""
	@echo "Build:"
	@echo "  all                  Build all four game binaries (default)"
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
	@echo "Smoke tests (headless):"
	@echo "  env SDL_VIDEODRIVER=dummy build/bin/dark-colony --check"
	@echo "  env SDL_VIDEODRIVER=dummy build/bin/dark-reign --check"
	@echo "  env SDL_VIDEODRIVER=dummy build/bin/dark-colony --screenshot /tmp/dc.bmp"
	@echo ""
	@echo "Tools:"
	@echo "  anim-extract         Build the anim_extract tool"
	@echo "  dark-colony-info     Regenerate Dark Colony info.h/info.c from game data"
	@echo "  dark-colony-gamestat Regenerate Dark Colony gamestat.h from game data"

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)
