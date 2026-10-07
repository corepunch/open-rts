/* Native WAV entries and event assignments from Wargus wartool.h and
 * scripts/sound.lua; independently implemented over the Dark Colony mixer.
 * IDs 2..292 are SFXDAT.SUD entries; 1 is MAINDAT.WAR's UI click (432).
 * See docs/WAR2_EXE_FINDINGS.md for evidence and remaining unknowns. */
#include "w2_local.h"

enum {
    S_CLICK = 1, S_NATIVE_END = 293,
    S_HUMAN_SELECT = S_NATIVE_END, S_ORC_SELECT, S_HUMAN_ACK, S_ORC_ACK,
    S_PEASANT_SELECT, S_PEASANT_ACK, S_KNIGHT_SELECT, S_KNIGHT_ACK,
    S_OGRE_SELECT, S_OGRE_ACK, S_ARCHER_SELECT, S_ARCHER_ACK,
    S_TROLL_SELECT, S_TROLL_ACK, S_MAGE_SELECT, S_MAGE_ACK,
    S_DK_SELECT, S_DK_ACK, S_PALADIN_SELECT, S_PALADIN_ACK,
    S_OM_SELECT, S_OM_ACK, S_DWARF_SELECT, S_DWARF_ACK,
    S_SAPPER_SELECT, S_SAPPER_ACK, S_HSHIP_SELECT, S_HSHIP_ACK,
    S_OSHIP_SELECT, S_OSHIP_ACK, S_DRAGON_ACK, S_GRYPHON_ACK,
    S_SWORD, S_BUILDING_DEATH, S_CHOP, S_END
};

static const int16_t groups[S_END - S_NATIVE_END][MAXSFXLINKS] = {
    [S_HUMAN_SELECT - S_NATIVE_END] = {5, 7, 9, 11, 13, 15},
    [S_ORC_SELECT - S_NATIVE_END] = {6, 8, 10, 12, 14, 16},
    [S_HUMAN_ACK - S_NATIVE_END] = {32, 34, 36, 38},
    [S_ORC_ACK - S_NATIVE_END] = {33, 35, 37, 39},
    [S_PEASANT_SELECT - S_NATIVE_END] = {271, 272, 273, 274},
    [S_PEASANT_ACK - S_NATIVE_END] = {275, 276, 277, 278},
    [S_KNIGHT_SELECT - S_NATIVE_END] = {175, 176, 177, 178},
    [S_KNIGHT_ACK - S_NATIVE_END] = {179, 180, 181, 182},
    [S_OGRE_SELECT - S_NATIVE_END] = {201, 202, 203, 204},
    [S_OGRE_ACK - S_NATIVE_END] = {205, 206, 207},
    [S_ARCHER_SELECT - S_NATIVE_END] = {140, 141, 142, 143},
    [S_ARCHER_ACK - S_NATIVE_END] = {144, 145, 146, 147},
    [S_TROLL_SELECT - S_NATIVE_END] = {247, 248, 249},
    [S_TROLL_ACK - S_NATIVE_END] = {250, 251, 252},
    [S_MAGE_SELECT - S_NATIVE_END] = {257, 258, 259},
    [S_MAGE_ACK - S_NATIVE_END] = {260, 261, 262},
    [S_DK_SELECT - S_NATIVE_END] = {120, 121},
    [S_DK_ACK - S_NATIVE_END] = {122, 123, 124},
    [S_PALADIN_SELECT - S_NATIVE_END] = {187, 188, 189, 190},
    [S_PALADIN_ACK - S_NATIVE_END] = {191, 192, 193, 194},
    [S_OM_SELECT - S_NATIVE_END] = {212, 213, 214, 215},
    [S_OM_ACK - S_NATIVE_END] = {216, 217, 218},
    [S_DWARF_SELECT - S_NATIVE_END] = {129, 130},
    [S_DWARF_ACK - S_NATIVE_END] = {131, 132, 133, 134, 135},
    [S_SAPPER_SELECT - S_NATIVE_END] = {159, 160, 161, 162},
    [S_SAPPER_ACK - S_NATIVE_END] = {163, 164, 165, 166},
    [S_HSHIP_SELECT - S_NATIVE_END] = {227, 229, 231},
    [S_HSHIP_ACK - S_NATIVE_END] = {233, 235, 237},
    [S_OSHIP_SELECT - S_NATIVE_END] = {228, 230, 232},
    [S_OSHIP_ACK - S_NATIVE_END] = {234, 236, 238},
    [S_DRAGON_ACK - S_NATIVE_END] = {281, 282},
    [S_GRYPHON_ACK - S_NATIVE_END] = {284, 285},
    [S_SWORD - S_NATIVE_END] = {60, 61, 62},
    [S_BUILDING_DEATH - S_NATIVE_END] = {52, 53, 54},
    [S_CHOP - S_NATIVE_END] = {56, 57, 58, 59},
};

typedef struct { int select, ack, attack, death, ready; } unitsounds_t;
static const unitsounds_t unitsounds[NUMMOBJTYPES] = {
    /* Selection, acknowledgement, weapon, death, ready. Native entry ids
     * remain literal; groups above preserve the reference's membership. */
    [MT_FOOTMAN] = {S_HUMAN_SELECT, S_HUMAN_ACK, S_SWORD, 49, 43},
    [MT_GRUNT] = {S_ORC_SELECT, S_ORC_ACK, S_SWORD, 50, 44},
    [MT_PEASANT] = {S_PEASANT_SELECT, S_PEASANT_ACK, 81, 49, 263},
    [MT_PEON] = {S_ORC_SELECT, S_ORC_ACK, 81, 50, 115},
    [MT_BALLISTA] = {S_CLICK, 292, 55, 31, 43},
    [MT_CATAPULT] = {S_CLICK, 292, 55, 31, 44},
    [MT_KNIGHT] = {S_KNIGHT_SELECT, S_KNIGHT_ACK, S_SWORD, 49, 174},
    [MT_OGRE] = {S_OGRE_SELECT, S_OGRE_ACK, 63, 50, 200},
    [MT_ARCHER] = {S_ARCHER_SELECT, S_ARCHER_ACK, 66, 49, 139},
    [MT_AXETHROWER] = {S_TROLL_SELECT, S_TROLL_ACK, 77, 50, 246},
    [MT_MAGE] = {S_MAGE_SELECT, S_MAGE_ACK, 111, 49, 256},
    [MT_DEATH_KNIGHT] = {S_DK_SELECT, S_DK_ACK, 112, 50, 119},
    [MT_PALADIN] = {S_PALADIN_SELECT, S_PALADIN_ACK, S_SWORD, 49, 186},
    [MT_OGRE_MAGE] = {S_OM_SELECT, S_OM_ACK, 63, 50, 211},
    [MT_DEMOLITION_SQUAD] = {S_DWARF_SELECT, S_DWARF_ACK, S_SWORD, 31, 128},
    [MT_GOBLIN_SAPPERS] = {S_SAPPER_SELECT, S_SAPPER_ACK, S_SWORD, 31, 158},
    [MT_ATTACK_PEASANT] = {S_PEASANT_SELECT, S_PEASANT_ACK, 81, 49, 263},
    [MT_ATTACK_PEON] = {S_ORC_SELECT, S_ORC_ACK, 81, 50, 115},
    [MT_RANGER] = {S_ARCHER_SELECT, S_ARCHER_ACK, 66, 49, 139},
    [MT_BERSERKER] = {S_TROLL_SELECT, S_TROLL_ACK, 77, 50, 246},
    /* Base-game hero mappings; the installed archive has no expansion voices. */
    [MT_ALLERIA] = {S_ARCHER_SELECT, S_ARCHER_ACK, 66, 49, 0},
    [MT_TERON_GOREFIEND] = {S_ORC_SELECT, S_DK_ACK, 112, 50, 0},
    [MT_KURDRAN] = {S_HUMAN_SELECT, S_GRYPHON_ACK, 111, 49, 0},
    [MT_DENTARG] = {S_OGRE_SELECT, S_OGRE_ACK, 63, 50, 0},
    [MT_KHADGAR] = {S_MAGE_SELECT, S_MAGE_ACK, 111, 49, 0},
    [MT_GROM_HELLSCREAM] = {S_ORC_SELECT, S_ORC_ACK, S_SWORD, 50, 0},
    [MT_HUMAN_OIL_TANKER] = {S_HSHIP_SELECT, 78, 0, 51, 225},
    [MT_ORC_OIL_TANKER] = {S_OSHIP_SELECT, 78, 0, 51, 226},
    [MT_HUMAN_TRANSPORT] = {S_HSHIP_SELECT, S_HSHIP_ACK, 0, 51, 225},
    [MT_ORC_TRANSPORT] = {S_OSHIP_SELECT, S_OSHIP_ACK, 0, 51, 226},
    [MT_HUMAN_DESTROYER] = {S_HSHIP_SELECT, S_HSHIP_ACK, 65, 51, 225},
    [MT_ORC_DESTROYER] = {S_OSHIP_SELECT, S_OSHIP_ACK, 65, 51, 226},
    [MT_BATTLESHIP] = {S_HSHIP_SELECT, S_HSHIP_ACK, 65, 51, 225},
    [MT_OGRE_JUGGERNAUGHT] = {S_OSHIP_SELECT, S_OSHIP_ACK, 65, 51, 226},
    [MT_DEATHWING] = {S_ORC_SELECT, S_DRAGON_ACK, 65, 31, 0},
    [MT_GNOMISH_SUBMARINE] = {S_HSHIP_SELECT, S_HSHIP_ACK, 65, 51, 225},
    [MT_GIANT_TURTLE] = {S_OSHIP_SELECT, S_OSHIP_ACK, 65, 51, 226},
    [MT_FLYING_MACHINE] = {S_CLICK, 154, 0, 31, 153},
    [MT_ZEPPELIN] = {S_CLICK, 170, 0, 31, 169},
    [MT_GRYPHON_RIDER] = {283, S_GRYPHON_ACK, 111, 49, 284},
    [MT_DRAGON] = {280, S_DRAGON_ACK, 65, 31, 279},
    [MT_TURALYON] = {S_KNIGHT_SELECT, S_KNIGHT_ACK, S_SWORD, 49, 0},
    [MT_EYE_OF_KILROGG] = {.select = S_CLICK},
    [MT_DANATH] = {S_HUMAN_SELECT, S_HUMAN_ACK, S_SWORD, 49, 0},
    [MT_KARGATH_BLADEFIST] = {S_ORC_SELECT, S_ORC_ACK, S_SWORD, 50, 0},
    [MT_CHOGALL] = {S_OM_SELECT, S_OM_ACK, 63, 50, 0},
    [MT_LOTHAR] = {S_KNIGHT_SELECT, S_KNIGHT_ACK, S_SWORD, 49, 0},
    [MT_GULDAN] = {S_DK_SELECT, S_DK_ACK, 112, 50, 0},
    [MT_UTHER_LIGHTBRINGER] = {S_PALADIN_SELECT, S_PALADIN_ACK, S_SWORD, 49, 0},
    [MT_ZULJIN] = {S_TROLL_SELECT, S_TROLL_ACK, 77, 50, 0},
    [MT_SKELETON] = {S_CLICK, S_CLICK, 79, 50, 0},
    [MT_DAEMON] = {S_CLICK, 0, 0, 31, 0},
    [MT_FARM] = {.select = 74}, [MT_PIG_FARM] = {.select = 75},
    [MT_HUMAN_BARRACKS] = {.select = S_CLICK}, [MT_ORC_BARRACKS] = {.select = S_CLICK},
    [MT_CHURCH] = {.select = 70}, [MT_ALTAR_OF_STORMS] = {.select = 71},
    [MT_HUMAN_WATCH_TOWER] = {.select = S_CLICK}, [MT_ORC_WATCH_TOWER] = {.select = S_CLICK},
    [MT_STABLES] = {.select = 72}, [MT_OGRE_MOUND] = {.select = 73},
    [MT_INVENTOR] = {.select = 90}, [MT_ALCHEMIST] = {.select = 91},
    [MT_GRYPHON_AVIARY] = {.select = 87}, [MT_DRAGON_ROOST] = {.select = 88},
    [MT_HUMAN_SHIPYARD] = {.select = 80}, [MT_ORC_SHIPYARD] = {.select = 80},
    [MT_TOWN_HALL] = {.select = S_CLICK}, [MT_GREAT_HALL] = {.select = S_CLICK},
    [MT_ELVEN_LUMBER_MILL] = {.select = 84}, [MT_TROLL_LUMBER_MILL] = {.select = 84},
    [MT_HUMAN_FOUNDRY] = {.select = 89}, [MT_ORC_FOUNDRY] = {.select = 89},
    [MT_MAGE_TOWER] = {.select = 92}, [MT_TEMPLE_OF_THE_DAMNED] = {.select = 93},
    [MT_HUMAN_BLACKSMITH] = {.select = 69}, [MT_ORC_BLACKSMITH] = {.select = 69},
    [MT_HUMAN_REFINERY] = {.select = 83}, [MT_ORC_REFINERY] = {.select = 83},
    [MT_HUMAN_OIL_PLATFORM] = {.select = 82}, [MT_ORC_OIL_PLATFORM] = {.select = 82},
    [MT_KEEP] = {.select = S_CLICK}, [MT_STRONGHOLD] = {.select = S_CLICK},
    [MT_CASTLE] = {.select = S_CLICK}, [MT_FORTRESS] = {.select = S_CLICK},
    [MT_GOLD_MINE] = {.select = 76}, [MT_OIL_PATCH] = {.select = S_CLICK},
    /* Wargus leaves tower attack sound assignments unknown. */
    [MT_HUMAN_GUARD_TOWER] = {.select = S_CLICK}, [MT_ORC_GUARD_TOWER] = {.select = S_CLICK},
    [MT_HUMAN_CANNON_TOWER] = {.select = S_CLICK}, [MT_ORC_CANNON_TOWER] = {.select = S_CLICK},
};

static int w2_actor_sound(const mobj_t *actor, soundevent_t event) {
    if (actor->type_id <= 0 || actor->type_id >= NUMMOBJTYPES) return 0;
    const unitsounds_t *sounds = &unitsounds[actor->type_id];
    switch (event) {
    case SE_SELECT: return sounds->select;
    case SE_ACK: return sounds->ack;
    case SE_ATTACK: return sounds->attack;
    case SE_DEATH:
        return (mobjinfo[actor->type_id].w2.flags & W2_STRUCTURE) ? S_BUILDING_DEATH : sounds->death;
    case SE_READY: return sounds->ready;
    case SE_WORK: return S_CHOP;
    case SE_WORK_COMPLETE: return actor->type_id == MT_PEON ? 41 : 42;
    case SE_RESEARCH_COMPLETE: {
        const w2_pud_t *pud = level.native_data;
        return pud && actor->owner < 16 && pud->sides[actor->owner] == 1 ? 41 : 40;
    }
    default: return 0;
    }
}

void A_W2_Chop(mobj_t *actor) {
    S_ActorSound(actor, SE_WORK);
}

static bool load_sample(const w2_archive_t *archive, int entry, int id, const char *name) {
    w2_blob_t wav;
    if (!w2_archive_extract(archive, entry, &wav)) return false;
    sfxinfo_t *sfx = S_Sfx(id);
    sfx->data = I_LoadSampleMemory(wav.data, wav.size);
    snprintf(sfx->name, sizeof(sfx->name), "%s#%d", name, entry);
    w2_blob_free(&wav);
    if (!sfx->data) fprintf(stderr, "warcraft-2: cannot decode sound %s\n", sfx->name);
    return sfx->data != NULL;
}

static bool w2_sound_init(const char *root) {
    bool used[S_NATIVE_END] = {[S_CLICK] = true, [40] = true, [41] = true, [42] = true};
    for (int type = 1; type < NUMMOBJTYPES; ++type) {
        const unitsounds_t *s = &unitsounds[type];
        const int ids[] = {s->select, s->ack, s->attack, s->death, s->ready};
        for (unsigned i = 0; i < sizeof(ids) / sizeof(*ids); ++i)
            if (ids[i] > 0 && ids[i] < S_NATIVE_END) used[ids[i]] = true;
    }
    for (int id = 1; id < S_END; ++id) {
        sfxinfo_t sfx = {0};
        if (id >= S_NATIVE_END) {
            memcpy(sfx.links, groups[id - S_NATIVE_END], sizeof(sfx.links));
            while (sfx.numlinks < MAXSFXLINKS && sfx.links[sfx.numlinks])
                used[sfx.links[sfx.numlinks++]] = true;
            sfx.norepeat = true;
        }
        if (S_AddSfx(&sfx) != id) return false;
    }
    char path[1024];
    w2_archive_t archive;
    M_PathJoin(path, sizeof(path), root, "DATA/MAINDAT.WAR");
    if (!w2_archive_open(&archive, path)) return false;
    bool ok = load_sample(&archive, 432, S_CLICK, "DATA/MAINDAT.WAR");
    w2_archive_close(&archive);
    if (!ok) return false;
    M_PathJoin(path, sizeof(path), root, "DATA/SFXDAT.SUD");
    if (!w2_archive_open(&archive, path)) return false;
    for (int id = 2; ok && id < S_NATIVE_END; ++id)
        if (used[id]) ok = load_sample(&archive, id, id, "DATA/SFXDAT.SUD");
    w2_archive_close(&archive);
    return ok;
}

const soundinfo_t w2_soundinfo = {
    .init = w2_sound_init,
    .actor_sound = w2_actor_sound,
    .ui = {[UI_SOUND_CLICK] = S_CLICK, [UI_SOUND_MESSAGE] = S_CLICK},
    /* Keep shared stereo positioning; no invented retail distance curve. */
};
