#include "dark-colony.h"
#include "info.h"

/* Doom p_saveg.c: archive thinkers, reconstruct their functions and info from
 * tables, then link individually allocated mobjs. IDs preserve RTS references.
 * These versioned engine saves are deliberately distinct from retail saves. */

typedef struct {
    char magic[8];
    uint32_t version, header_size, object_size, body_size, checksum, signature;
    dc_saveinfo_t info;
    int width, height, time, speed, vents, objects;
    bool paused;
    fvec2_t camera;
    daylight_t daylight;
    int resources[8][RTS_MAX_RESOURCES];
    uint8_t offers[2][8], random;
    uint32_t peace[8], vision[8], next_id, next_order;
    uint16_t income_scale[8];
    int exo_income[8];
    uint8_t upgrades[sizeof(level.upgrades)], purchases[sizeof(level.purchases)];
    size_t mission_size;
    AiContext ai;
    hudtext_t hud;
} save_t;

typedef struct {
    mobj_t object;
    uint32_t target, attack, base;
    bool production, removed;
    production_t queue;
} savedmobj_t;

static uint32_t checksum(const void *data, size_t length) {
    const uint8_t *bytes = data;
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < length; ++i) hash = (hash ^ bytes[i]) * UINT32_C(16777619);
    return hash;
}

static const mobjtype_t *type_info(uint16_t type) {
    for (int i = 0; i < num_actor_types; ++i)
        if (actor_types[i].id == type) return &actor_types[i];
    return NULL;
}

static bool save_signature(const char *path, uint32_t *signature) {
    blob_t file;
    if (!W_ReadFile(path, &file)) return false;
    uint32_t hash = checksum(file.bytes, file.size);
    W_FreeFile(&file);
    for (int i = 0; i < NUMSTATES; ++i) {
        const state_t *state = &states[i];
        int fields[] = {i, state->sprite, state->frame, state->count, state->tics,
                        state->nextstate, state->group};
        hash = (hash ^ checksum(fields, sizeof(fields))) * UINT32_C(16777619);
        for (int j = 0; j < P_StateFrames(state); ++j)
            hash = (hash ^ (uint32_t)P_StateTics(state, j)) * UINT32_C(16777619);
    }
    *signature = hash;
    return true;
}

static bool read_save(const char *path, blob_t *file, save_t *save) {
    if (!W_ReadFile(path, file)) return false;
    if (file->size < sizeof(*save)) goto fail;
    memcpy(save, file->bytes, sizeof(*save));
    if (memcmp(save->magic, "ORTSDC1", 8) || save->version != 1 ||
        save->header_size != sizeof(*save) || save->object_size != sizeof(savedmobj_t) ||
        save->body_size != file->size - sizeof(*save) || save->width <= 0 ||
        save->height <= 0 || save->width > 4096 || save->height > 4096 ||
        save->objects < 0 || save->vents < 0 || save->speed < 10 || save->speed > 200 ||
        !memchr(save->info.map, 0, sizeof(save->info.map)) || !save->info.map[0] ||
        !memchr(save->info.name, 0, sizeof(save->info.name)) || save->ai.game) goto fail;
    size_t cells = (size_t)save->width * save->height;
    if ((size_t)save->objects > save->body_size / sizeof(savedmobj_t) ||
        (size_t)save->vents > save->body_size / sizeof(resourcevent_t) ||
        save->mission_size > save->body_size) goto fail;
    size_t expected = cells * (sizeof(uint32_t) + 1) +
        (size_t)save->vents * sizeof(resourcevent_t) +
        (size_t)save->objects * sizeof(savedmobj_t) + save->mission_size;
    uint32_t stored = save->checksum;
    save_t checked;
    memcpy(&checked, save, sizeof(checked));
    checked.checksum = 0;
    /* The checksum covers header fields as well as object and mission data. */
    if (expected != save->body_size ||
        (checksum(&checked, sizeof(checked)) ^
         checksum(file->bytes + sizeof(*save), save->body_size)) != stored) goto fail;
    const uint8_t *records = file->bytes + sizeof(*save) + cells * 5 +
        (size_t)save->vents * sizeof(resourcevent_t);
    for (int i = 0; i < save->objects; ++i) {
        savedmobj_t record;
        memcpy(&record, records + (size_t)i * sizeof(record), sizeof(record));
        const mobj_t *object = &record.object;
        if (!object->id || !type_info(object->type_id) || object->info || object->target ||
            object->attack.target || object->harvest.base || object->production ||
            object->thinker.next || object->thinker.prev || object->thinker.function ||
            object->owner >= 8 || object->team >= 16 || object->core.state_id < 0 ||
            object->core.state_id >= NUMSTATES || object->waypoints.count < 0 ||
            object->waypoints.count > MAXWAYPOINTS || object->movement.path.count < 0 ||
            object->movement.path.count > NAV_MAX_WAYPOINTS) goto fail;
        for (int j = 0; j < i; ++j) {
            savedmobj_t previous;
            memcpy(&previous, records + (size_t)j * sizeof(previous), sizeof(previous));
            if (previous.object.id == object->id) goto fail;
        }
    }
    uint32_t signature;
    if (!save_signature(save->info.map, &signature) || signature != save->signature ||
        !DC_ValidateMissionArchive(records + (size_t)save->objects * sizeof(savedmobj_t),
                                  save->mission_size)) goto fail;
    return true;
fail:
    W_FreeFile(file);
    return false;
}

bool DC_SaveInfo(const char *path, dc_saveinfo_t *info) {
    blob_t file;
    save_t save;
    if (!read_save(path, &file, &save)) return false;
    *info = save.info;
    W_FreeFile(&file);
    return true;
}

bool DC_SaveGame(const char *path, const char *name, const app_t *app,
                 const AiContext *ai, const hudtext_t *hud) {
    if (netgame || !level.map_path[0] || !level.blocked || !level.sight.cells) return false;
    save_t save = {.magic = "ORTSDC1", .version = 1, .header_size = sizeof(save),
        .object_size = sizeof(savedmobj_t), .width = level.width, .height = level.height,
        .time = leveltime, .speed = game_speed, .vents = level.resource_vent_count,
        .paused = paused, .camera = app->cam, .daylight = level.daylight,
        .random = level.random_index, .next_id = level.next_mobj_id,
        .next_order = level.next_move_order_id, .ai = *ai, .hud = *hud};
    save.ai.game = NULL;
    snprintf(save.info.name, sizeof(save.info.name), "%s", name);
    snprintf(save.info.map, sizeof(save.info.map), "%s", level.map_path);
    if (!save_signature(level.map_path, &save.signature)) return false;
    const dc_skirmish_t *setup = DC_LevelSkirmish(&level);
    if (setup) { save.info.skirmish = true; save.info.setup = *setup; }
    memcpy(save.resources, level.player_resources, sizeof(save.resources));
    memcpy(save.income_scale, level.income_scale, sizeof(save.income_scale));
    memcpy(save.offers, level.alliance_offers, sizeof(save.offers));
    memcpy(save.peace, level.peace, sizeof(save.peace));
    memcpy(save.vision, level.sight.allies, sizeof(save.vision));
    memcpy(save.exo_income, level.exo_income, sizeof(save.exo_income));
    memcpy(save.upgrades, level.upgrades, sizeof(save.upgrades));
    memcpy(save.purchases, level.purchases, sizeof(save.purchases));
    const void *mission = DC_MissionArchive(&save.mission_size);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) ++save.objects;
    size_t cells = (size_t)level.width * level.height;
    size_t size = cells * 5 + (size_t)save.vents * sizeof(resourcevent_t) +
        (size_t)save.objects * sizeof(savedmobj_t) + save.mission_size;
    if (size > UINT32_MAX) return false;
    save.body_size = size;
    uint8_t *body = malloc(size);
    if (!body) return false;
    uint8_t *next = body;
    memcpy(next, level.blocked, cells); next += cells;
    memcpy(next, level.sight.cells, cells * 4); next += cells * 4;
    if (save.vents) memcpy(next, level.resource_vents, (size_t)save.vents * sizeof(resourcevent_t));
    next += (size_t)save.vents * sizeof(resourcevent_t);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *object = (mobj_t *)th;
        savedmobj_t record = {.object = *object, .removed = !th->function,
            .target = object->target ? object->target->id : 0,
            .attack = object->attack.target ? object->attack.target->id : 0,
            .base = object->harvest.base ? object->harvest.base->id : 0,
            .production = object->production != NULL};
        if (record.production) record.queue = *object->production;
        record.object.thinker = (thinker_t){0};
        record.object.info = NULL;
        record.object.target = record.object.attack.target = record.object.harvest.base = NULL;
        record.object.production = NULL;
        memcpy(next, &record, sizeof(record)); next += sizeof(record);
    }
    if (save.mission_size) memcpy(next, mission, save.mission_size);
    save.checksum = checksum(&save, sizeof(save)) ^ checksum(body, size);
    char temporary[1280];
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE *file = fopen(temporary, "wb");
    bool ok = file && fwrite(&save, sizeof(save), 1, file) == 1 && fwrite(body, size, 1, file) == 1;
    if (file && fclose(file)) ok = false;
    free(body);
    if (ok && !rename(temporary, path)) return true;
    remove(temporary);
    return false;
}

static mobj_t *reference(mobj_t **objects, int count, uint32_t id) {
    for (int i = 0; id && i < count; ++i) if (objects[i]->id == id) return objects[i];
    return NULL;
}

bool DC_LoadGame(const char *path, app_t *app, AiContext *ai, hudtext_t *hud) {
    blob_t file;
    save_t save;
    if (netgame || !read_save(path, &file, &save)) return false;
    bool ok = false;
    size_t cells = (size_t)save.width * save.height;
    const uint8_t *next = file.bytes + sizeof(save) + cells * 5;
    resourcevent_t *vents = save.vents ? malloc((size_t)save.vents * sizeof(*vents)) : NULL;
    mobj_t **objects = calloc((size_t)save.objects + 1, sizeof(*objects));
    if ((save.vents && !vents) || !objects || save.width != level.width ||
        save.height != level.height || strcmp(save.info.map, level.map_path)) goto done;
    if (save.vents) memcpy(vents, next, (size_t)save.vents * sizeof(*vents));
    next += (size_t)save.vents * sizeof(*vents);
    for (int i = 0; i < save.objects; ++i) {
        savedmobj_t record;
        memcpy(&record, next + (size_t)i * sizeof(record), sizeof(record));
        objects[i] = malloc(sizeof(*objects[i]));
        if (!objects[i]) goto done;
        *objects[i] = record.object;
        objects[i]->info = type_info(objects[i]->type_id);
        objects[i]->thinker.function = record.removed ? NULL : P_MobjThinker;
        if (record.production) {
            objects[i]->production = malloc(sizeof(record.queue));
            if (!objects[i]->production) goto done;
            *objects[i]->production = record.queue;
        }
    }
    for (int i = 0; i < save.objects; ++i) {
        savedmobj_t record;
        memcpy(&record, next + (size_t)i * sizeof(record), sizeof(record));
        objects[i]->target = reference(objects, save.objects, record.target);
        objects[i]->attack.target = reference(objects, save.objects, record.attack);
        objects[i]->harvest.base = reference(objects, save.objects, record.base);
    }
    next += (size_t)save.objects * sizeof(savedmobj_t);
    if (!DC_RestoreMission(next, save.mission_size)) goto done;
    P_FreeThinkers();
    for (int i = 0; i < save.objects; ++i) P_AddThinker(&objects[i]->thinker);
    memcpy(level.blocked, file.bytes + sizeof(save), cells);
    memcpy(level.sight.cells, file.bytes + sizeof(save) + cells, cells * 4);
    free(level.resource_vents);
    level.resource_vents = vents; vents = NULL;
    level.resource_vent_count = save.vents;
    leveltime = save.time;
    paused = save.paused;
    D_SetGameSpeed(save.speed);
    app->cam = save.camera;
    level.daylight = save.daylight;
    level.random_index = save.random;
    level.next_mobj_id = save.next_id;
    level.next_move_order_id = save.next_order;
    P_NavFree(&level);
    memcpy(level.player_resources, save.resources, sizeof(save.resources));
    memcpy(level.income_scale, save.income_scale, sizeof(save.income_scale));
    memcpy(level.alliance_offers, save.offers, sizeof(save.offers));
    memcpy(level.peace, save.peace, sizeof(save.peace));
    memcpy(level.sight.allies, save.vision, sizeof(save.vision));
    memcpy(level.exo_income, save.exo_income, sizeof(save.exo_income));
    memcpy(level.upgrades, save.upgrades, sizeof(save.upgrades));
    memcpy(level.purchases, save.purchases, sizeof(save.purchases));
    *ai = save.ai;
    P_AiAttachGame(ai, G_AiInterface());
    *hud = save.hud;
    G_ClearTiccmds();
    ok = true;
done:
    if (!ok && objects) for (int i = 0; i < save.objects; ++i) {
        if (objects[i]) free(objects[i]->production);
        free(objects[i]);
    }
    free(objects); free(vents);
    W_FreeFile(&file);
    return ok;
}
