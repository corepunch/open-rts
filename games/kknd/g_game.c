#include "game.h"
#include "kknd.h"
#include "info.h"

#define SPR(idx) "LEVELS/640/SPRITES.LVL|" #idx ".mobd"

static const mobjtype_t ACTOR_TYPES[] = {
    /* === Survivor Infantry === */
    { .id = MT_SURV_RIFLEMAN, .name = "Rifleman",
      .sprite_name = SPR(34),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 100,
      .attack = { .range = 4.0f, .damage = 10, .cooldown_ms = 650 },
    },
    { .id = MT_SURV_FLAMER, .name = "Flamer",
      .sprite_name = SPR(25),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 120,
      .attack = { .range = 3.0f, .damage = 15, .cooldown_ms = 500 },
    },
    { .id = MT_SURV_RPG_LAUNCHER, .name = "RPG Launcher",
      .sprite_name = SPR(59),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 80,
      .attack = { .range = 6.0f, .damage = 30, .cooldown_ms = 1200 },
    },
    { .id = MT_SURV_SNIPER, .name = "Sniper",
      .sprite_name = SPR(71),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.0f, .max_hp = 60,
      .attack = { .range = 8.0f, .damage = 50, .cooldown_ms = 2000 },
    },
    /* === Survivor Vehicles === */
    { .id = MT_SURV_DIRT_BIKE, .name = "Dirt Bike",
      .sprite_name = SPR(7),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 7.0f, .max_hp = 80,
      .attack = { .range = 4.0f, .damage = 8, .cooldown_ms = 400 },
    },
    { .id = MT_SURV_4X4_PICKUP, .name = "4x4 Pickup",
      .sprite_name = SPR(54),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 5.5f, .max_hp = 200,
      .attack = { .range = 5.0f, .damage = 12, .cooldown_ms = 500 },
    },
    { .id = MT_SURV_ANACONDA_TANK, .name = "Anaconda Tank",
      .sprite_name = SPR(77),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 500,
      .attack = { .range = 7.0f, .damage = 40, .cooldown_ms = 1500 },
    },
    { .id = MT_SURV_AUTOCANNON_TANK, .name = "Autocannon Tank",
      .sprite_name = SPR(11),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 350,
      .attack = { .range = 6.0f, .damage = 25, .cooldown_ms = 800 },
    },
    /* === Survivor Harvesters === */
    { .id = MT_SURV_OIL_TANKER, .name = "Oil Tanker",
      .sprite_name = SPR(73),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = 4.0f, .max_hp = 300,
      .harvest = { .capacity = 100 },
    },
    /* === Survivor Buildings === */
    { .id = MT_SURV_DRILLRIG, .name = "Drill Rig",
      .sprite_name = SPR(75),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 600,
    },
    { .id = MT_SURV_OUTPOST, .name = "Outpost",
      .sprite_name = SPR(52),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 800,
    },
    { .id = MT_SURV_MACHINE_SHOP, .name = "Machine Shop",
      .sprite_name = SPR(37),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 600,
    },
    /* === Mutant Infantry === */
    { .id = MT_MUTE_BERSERKER, .name = "Berserker",
      .sprite_name = SPR(5),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.5f, .max_hp = 120,
      .attack = { .range = 2.0f, .damage = 20, .cooldown_ms = 600 },
    },
    { .id = MT_MUTE_PYROMANIAC, .name = "Pyromaniac",
      .sprite_name = SPR(55),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 100,
      .attack = { .range = 3.0f, .damage = 15, .cooldown_ms = 500 },
    },
    { .id = MT_MUTE_SHOTGUNNER, .name = "Shotgunner",
      .sprite_name = SPR(68),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 100,
      .attack = { .range = 4.0f, .damage = 12, .cooldown_ms = 700 },
    },
    { .id = MT_MUTE_BAZOOKA, .name = "Bazooka",
      .sprite_name = SPR(60),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.5f, .max_hp = 80,
      .attack = { .range = 6.0f, .damage = 30, .cooldown_ms = 1200 },
    },
    /* === Mutant Vehicles === */
    { .id = MT_MUTE_DIRE_WOLF, .name = "Dire Wolf",
      .sprite_name = SPR(19),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 7.0f, .max_hp = 100,
      .attack = { .range = 2.0f, .damage = 15, .cooldown_ms = 400 },
    },
    { .id = MT_MUTE_MONSTER_TRUCK, .name = "Monster Truck",
      .sprite_name = SPR(47),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 5.0f, .max_hp = 250,
      .attack = { .range = 5.0f, .damage = 15, .cooldown_ms = 600 },
    },
    { .id = MT_MUTE_GIANT_SCORPION, .name = "Giant Scorpion",
      .sprite_name = SPR(64),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 4.0f, .max_hp = 400,
      .attack = { .range = 3.0f, .damage = 35, .cooldown_ms = 1000 },
    },
    { .id = MT_MUTE_WAR_MASTADONT, .name = "War Mastadont",
      .sprite_name = SPR(38),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_ATTACK,
      .speed = 3.0f, .max_hp = 600,
      .attack = { .range = 6.0f, .damage = 45, .cooldown_ms = 1500 },
    },
    /* === Mutant Harvesters === */
    { .id = MT_MUTE_OIL_TANKER, .name = "Mutant Oil Tanker",
      .sprite_name = SPR(48),
      .traits = MF_SELECTABLE | MF_MOBILE | MF_RENDERABLE | MF_HARVESTER,
      .speed = 4.0f, .max_hp = 250,
      .harvest = { .capacity = 100 },
    },
    /* === Mutant Buildings === */
    { .id = MT_MUTE_DRILLRIG, .name = "Mutant Drill Rig",
      .sprite_name = SPR(50),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_RESOURCE_BASE,
      .max_hp = 500,
    },
    { .id = MT_MUTE_CLANHALL, .name = "Clan Hall",
      .sprite_name = SPR(13),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 800,
    },
    { .id = MT_MUTE_BEAST_ENCLOSURE, .name = "Beast Enclosure",
      .sprite_name = SPR(3),
      .traits = MF_SELECTABLE | MF_RENDERABLE,
      .max_hp = 600,
    },
    /* === Survivor Towers === */
    { .id = MT_SURV_GUARD_TOWER, .name = "Guard Tower",
      .sprite_name = SPR(67),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 400,
      .attack = { .range = 6.0f, .damage = 12, .cooldown_ms = 500 },
    },
    /* === Mutant Towers === */
    { .id = MT_MUTE_MACHINEGUN_NEST, .name = "Machinegun Nest",
      .sprite_name = SPR(43),
      .traits = MF_SELECTABLE | MF_RENDERABLE | MF_ATTACK,
      .max_hp = 350,
      .attack = { .range = 5.0f, .damage = 10, .cooldown_ms = 400 },
    },
};

static const uidefinition_t UI = {
    .logical_width = 640,
    .logical_height = 480,
    .world_viewport = { 0, 0, 592, 480 },
    .resources = {
        [0] = { .text = { 400, 3 }, .color = { 255, 255, 255, 255 } },
    },
    .resource_count = 1,
    .status_panel = {
        .rect = { 230, 0, 180, 28 },
        .fill = { 0, 0, 0, 255 },
        .border = { 255, 255, 255, 255 },
    },
    .status_elapsed_time = true,
    .sidebar_panel = {
        .rect = { 592, 0, 48, 480 },
        .fill = { 0, 0, 0, 255 },
        .border = { 104, 104, 96, 255 },
    },
    .sidebar_cell_size = 48,
};

/* ── game identity (Doom-style externs) ─────────────────────────────────── */

const char *const g_game_id            = "kknd";
const char *const g_game_name          = "KKnD";
const char *const g_game_default_root  = "data/KKND";
const char *const g_game_default_map   = "LEVELS/640/SURV_01.LVL";
const char *const g_game_default_sprite = "LEVELS/640/SPRITES.LVL|Infantry.mobd";
const int g_cell_w = 32;
const int g_cell_h = 32;
const uint16_t g_debug_enemy_type = MT_MUTE_BERSERKER;
const gameinfo_t *gameinfo = &game_info;
const mobjtype_t *const actor_types = ACTOR_TYPES;
const int num_actor_types =
    (int)(sizeof(ACTOR_TYPES) / sizeof(ACTOR_TYPES[0]));
const uidefinition_t *const gameui = &UI;

/* ── G_* / R_* interface ────────────────────────────────────────────────── */

void G_InitGame(void) {
}

bool G_DoLoadLevel(const char *path, level_t *out) {
    return load_kknd_map(path, out);
}

bool W_LoadAssets(SDL_Renderer *renderer, const char *root, const level_t *map,
                  const char *sprite, tileset_t *tileset, spritesheet_t *unit_sprite) {
    return load_assets(renderer, root, map, sprite, tileset, unit_sprite);
}

int P_LoadThings(const char *path) {
    (void)path;
    int count = 0;
    float cx = level.width * 0.25f;
    float cy = level.height * 0.25f;

    /* Spawn player base + starting units */
    struct { uint16_t type; float dx; float dy; } player_units[] = {
        { MT_SURV_DRILLRIG,  0.0f,  0.0f },
        { MT_SURV_OIL_TANKER, -2.0f, 0.0f },
        { MT_SURV_RIFLEMAN,  2.0f, -1.0f },
        { MT_SURV_RIFLEMAN,  2.0f,  1.0f },
        { MT_SURV_RIFLEMAN,  3.0f,  0.0f },
        { MT_SURV_OUTPOST,  -3.0f, -2.0f },
    };
    for (int i = 0; i < (int)(sizeof(player_units) / sizeof(player_units[0])); ++i) {
        fvec2_t pos = { cx + player_units[i].dx, cy + player_units[i].dy };
        if (pos.x < 1) pos.x = 1;
        if (pos.y < 1) pos.y = 1;
        mobj_t *unit = P_SpawnMobj(fixed3_zero(), player_units[i].type);
        if (!unit) continue;
        unit->core.position = fixed3_from_fvec2(pos, 0);
        unit->owner = 0;
        unit->team = 0;
        unit->type_id = player_units[i].type;
        count++;
    }

    /* Spawn enemy base + units on the far side */
    float ex = level.width * 0.75f;
    float ey = level.height * 0.75f;
    struct { uint16_t type; float dx; float dy; } enemy_units[] = {
        { MT_MUTE_DRILLRIG,   0.0f,  0.0f },
        { MT_MUTE_OIL_TANKER, 2.0f,  0.0f },
        { MT_MUTE_BERSERKER, -2.0f, -1.0f },
        { MT_MUTE_BERSERKER, -2.0f,  1.0f },
        { MT_MUTE_SHOTGUNNER,-3.0f,  0.0f },
        { MT_MUTE_CLANHALL,   3.0f, -2.0f },
    };
    for (int i = 0; i < (int)(sizeof(enemy_units) / sizeof(enemy_units[0])); ++i) {
        fvec2_t pos = { ex + enemy_units[i].dx, ey + enemy_units[i].dy };
        if (pos.x >= level.width) pos.x = (float)level.width - 1.0f;
        if (pos.y >= level.height) pos.y = (float)level.height - 1.0f;
        mobj_t *unit = P_SpawnMobj(fixed3_zero(), enemy_units[i].type);
        if (!unit) continue;
        unit->core.position = fixed3_from_fvec2(pos, 0);
        unit->owner = 1;
        unit->team = 1;
        unit->allegiance = ALLEGIANCE_ENEMY;
        unit->type_id = enemy_units[i].type;
        count++;
    }

    /* Place resource vents near both bases */
    static const struct { float fx; float fy; } vent_rel[] = {
        { -6.0f, -4.0f }, { 6.0f, 4.0f },
        { -4.0f, 6.0f },  { 4.0f, -6.0f },
    };
    float bases[][2] = { { cx, cy }, { ex, ey } };
    for (int b = 0; b < 2; ++b) {
        for (int v = 0; v < (int)(sizeof(vent_rel) / sizeof(vent_rel[0])); ++v) {
            float vx = bases[b][0] + vent_rel[v].fx;
            float vy = bases[b][1] + vent_rel[v].fy;
            int ix = (int)vx, iy = (int)vy;
            if (!L_Contains(&level, ix, iy)) continue;
            resourcevent_t *vents = realloc(level.resource_vents,
                (size_t)(level.resource_vent_count + 1) * sizeof(resourcevent_t));
            if (!vents) break;
            level.resource_vents = vents;
            resourcevent_t *rv = &level.resource_vents[level.resource_vent_count++];
            rv->cell = (ivec2_t){ ix, iy };
            rv->attachment = (fvec2_t){ vx + 0.5f, vy + 0.5f };
            rv->amount = 5000;
            rv->rate = 25;
            rv->active = true;
            rv->resource_type = 0;
        }
    }

    level.player_resources[0][0] = 5000;
    level.player_resources[1][0] = 5000;
    level.has_camera = true;
    level.camera = (fvec2_t){ cx, cy };
    return count;
}

bool R_InitSprites(SDL_Renderer *renderer, const char *root, const level_t *map,
                          mobj_t *const *mobjs, int count, spritecache_t *cache) {
    (void)renderer; (void)root; (void)map; (void)mobjs; (void)count; (void)cache;
    return true;
}

bool HU_LoadFont(SDL_Renderer *renderer, const char *root, bitmapfont_t *font) {
    (void)renderer; (void)root; (void)font;
    return false;
}

void  G_MissionTicker(level_t *map, mobj_t *const *mobjs, int *count,
                      hudtext_t *hud, float dt) {
    (void)map; (void)mobjs; (void)count;
    (void)hud; (void)dt;
}

void *G_InitCustomUI(app_t *app, const char *data_root) {
    (void)app; (void)data_root;
    return NULL;
}

bool G_CustomUIResponder(void *ui, const app_t *app, level_t *map,
                         mobj_t *const *units, int unit_count, const SDL_Event *event) {
    (void)ui; (void)app; (void)map; (void)units; (void)unit_count; (void)event;
    return false;
}

void G_CustomUITicker(void *ui) {
    (void)ui;
}

void G_CustomUIDrawer(void *ui, app_t *app, const level_t *map,
                      mobj_t *const *units, int unit_count,
                      const spritecache_t *sprites, const hudtext_t *hud) {
    (void)ui; (void)app; (void)map; (void)units; (void)unit_count;
    (void)sprites; (void)hud;
}

bool G_UpdateProduction(void *ui, level_t *map, mobj_t *const *units, int *unit_count,
                        float dt) {
    (void)ui; (void)map; (void)units; (void)unit_count;
    (void)dt;
    return false;
}

void G_ShutdownCustomUI(void *ui) {
    (void)ui;
}

int G_WorldViewportWidth(const app_t *app) {
    if (!app) return 0;
    if (gameui && gameui->world_viewport.w > 0 && gameui->logical_width > 0)
        return gameui->world_viewport.w * app->win.w / gameui->logical_width;
    return app->win.w > 0 ? app->win.w : 1;
}
