#include "engine.h"
#include "info.h"
#include "rustfront.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Rustfront's single-player campaign (docs/CAMPAIGN.md). A mission is an ordinary map with
 * mission keys, which RF_LoadLevel hands here line by line:
 *
 *   mission FACTION N                  directorate | clans | concord, 1..5
 *   title TEXT
 *   briefing TEXT                      one paragraph a line, read by the front end
 *   objective KIND ARGS | TEXT         destroy_all, destroy TAG, protect TAG, survive SECONDS,
 *                                      hold X Y W H SECONDS, credits N, build UNIT [COUNT]
 *   tech UNIT...                       the only products the player may build (repeatable)
 *   trigger NAME time SECONDS [EVERY [TIMES]]
 *   trigger NAME area X Y W H [OWNER]  a unit of OWNER (the player) enters the cells
 *   trigger NAME destroyed TAG
 *   trigger NAME built UNIT            the player completes one
 *   spawn TRIGGER UNIT OWNER X Y [FACING] [@TAG]
 *   message TRIGGER TEXT
 *
 * and "unit ... @TAG" names a placed thing. Every objective but protect must be met to win;
 * a protected thing lost, or the player's last unit, loses. Enemy units a trigger spawns hunt
 * the player's nearest unit. The mission's progress is saved with the game (RF_CampaignSave, in g_save.c's block). */

enum { MAX_TRIGGERS = 32, MAX_ACTIONS = 160, MAX_TAGS = 24, MAX_TECH = 48, MAX_HUNTERS = 192,
       MAX_PENDING = 8, MESSAGE_TEXT = 200, MESSAGE_MS = 9000 };

typedef enum { OBJ_DESTROY_ALL = RF_OBJ_DESTROY_ALL, OBJ_DESTROY = RF_OBJ_DESTROY, OBJ_PROTECT = RF_OBJ_PROTECT,
               OBJ_SURVIVE = RF_OBJ_SURVIVE, OBJ_HOLD = RF_OBJ_HOLD, OBJ_CREDITS = RF_OBJ_CREDITS,
               OBJ_BUILD = RF_OBJ_BUILD, OBJ_KIND_COUNT } objkind_t;
static const char *const OBJ_NAMES[OBJ_KIND_COUNT] = {"destroy_all", "destroy", "protect", "survive",
                                                      "hold", "credits", "build"};

typedef enum { TRIG_TIME, TRIG_AREA, TRIG_DESTROYED, TRIG_BUILT } trigkind_t;

typedef struct {
    objkind_t kind;
    int tag, seconds, amount, count;
    irect_t area;
    uint16_t type;
    char text[RF_OBJECTIVE_TEXT];
} objective_t;

typedef struct {
    char name[24];
    trigkind_t kind;
    int at_ms, every_ms, times, owner, tag;
    irect_t area;
    uint16_t type;
} trigger_t;

typedef struct {
    int trigger;
    bool message;
    uint16_t type;
    int owner, facing, tag;
    ivec2_t cell;
    char text[MESSAGE_TEXT];
} action_t;

/* The mission as the map wrote it. */
static struct {
    bool active;
    int faction, index, errors;
    char title[64];
    int objective_count, trigger_count, action_count, tag_count, tech_count;
    objective_t objectives[RF_MAX_OBJECTIVES];
    trigger_t triggers[MAX_TRIGGERS];
    action_t actions[MAX_ACTIONS];
    char tags[MAX_TAGS][16];
    bool tag_defined[MAX_TAGS];
    bool tech_limited;
    uint16_t tech[MAX_TECH];
} def;

/* How far it has got: plain data, saved with the game. */
typedef struct {
    int32_t elapsed_ms;
    uint32_t tag_id[MAX_TAGS];
    uint8_t state[RF_MAX_OBJECTIVES];
    int32_t hold_ms[RF_MAX_OBJECTIVES];
    int32_t fired[MAX_TRIGGERS], next_ms[MAX_TRIGGERS];
    uint8_t done[MAX_TRIGGERS];
    uint32_t hunters[MAX_HUNTERS];
    int32_t hunter_count;
    uint8_t started, victory, defeat, recorded;
} runstate_t;
static runstate_t run;

static char pending[MAX_PENDING][MESSAGE_TEXT];
static int pending_count;
static char last_message[MESSAGE_TEXT];

static const char *const SLUGS[RF_FACTION_COUNT] = {"directorate", "clans", "concord"};

/* ── the campaign list and progress ────────────────────────────────────── */

const char *RF_CampaignName(int faction) {
    return faction >= 0 && faction < RF_FACTION_COUNT ? rf_factions[faction].name : NULL;
}

const char *RF_CampaignSlug(int faction) {
    return faction >= 0 && faction < RF_FACTION_COUNT ? SLUGS[faction] : NULL;
}

static int faction_of_slug(const char *slug) {
    for (int f = 0; f < RF_FACTION_COUNT; ++f)
        if (!strcmp(slug, SLUGS[f])) return f;
    return -1;
}

bool RF_MissionPath(int faction, int index, char *out, size_t size) {
    if (!RF_CampaignSlug(faction) || index < 0 || index >= RF_CAMPAIGN_MISSIONS || !out) return false;
    return snprintf(out, size, "maps/campaign/%s-%d.txt", SLUGS[faction], index + 1) < (int)size;
}

static bool progress_path(char *out, size_t size) {
    const char *dir = D_UserDirectory();
    if (!dir || !dir[0]) return false;
    M_PathJoin(out, size, dir, "campaign.txt");
    return true;
}

static void read_progress(int won[RF_FACTION_COUNT]) {
    memset(won, 0, sizeof(int) * RF_FACTION_COUNT);
    char path[1200], slug[32];
    if (!progress_path(path, sizeof(path))) return;
    FILE *file = fopen(path, "r");
    if (!file) return;
    int n;
    while (fscanf(file, "%31s %d", slug, &n) == 2) {
        int f = faction_of_slug(slug);
        if (f >= 0) won[f] = n < 0 ? 0 : n > RF_CAMPAIGN_MISSIONS ? RF_CAMPAIGN_MISSIONS : n;
    }
    fclose(file);
}

static bool write_progress(const int won[RF_FACTION_COUNT]) {
    char path[1200], temporary[1300];
    if (!progress_path(path, sizeof(path))) return false;
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    FILE *file = fopen(temporary, "w");
    if (!file) return false;
    for (int f = 0; f < RF_FACTION_COUNT; ++f) fprintf(file, "%s %d\n", SLUGS[f], won[f]);
    bool ok = fclose(file) == 0;
    if (ok) {
        remove(path);
        ok = rename(temporary, path) == 0;
    }
    return ok;
}

int RF_CampaignWon(int faction) {
    if (!RF_CampaignSlug(faction)) return 0;
    int won[RF_FACTION_COUNT];
    read_progress(won);
    return won[faction];
}

bool RF_MissionUnlocked(int faction, int index) {
    return RF_CampaignSlug(faction) && index >= 0 && index < RF_CAMPAIGN_MISSIONS &&
           index <= RF_CampaignWon(faction);
}

bool RF_CampaignRecordWin(int faction, int index) {
    if (!RF_CampaignSlug(faction) || index < 0 || index >= RF_CAMPAIGN_MISSIONS) return false;
    int won[RF_FACTION_COUNT];
    read_progress(won);
    if (won[faction] < index + 1) won[faction] = index + 1;
    return write_progress(won);
}

bool RF_CampaignResetProgress(void) {
    int won[RF_FACTION_COUNT] = {0};
    return write_progress(won);
}

/* ── parsing ───────────────────────────────────────────────────────────── */

static const char *skip_word(const char *s) {
    while (*s && !isspace((unsigned char)*s)) s++;
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static bool keyword(const char *line, const char *key, const char **rest) {
    size_t n = strlen(key);
    if (strncmp(line, key, n) || (line[n] && !isspace((unsigned char)line[n]))) return false;
    *rest = skip_word(line);
    return true;
}

static void copy_text(char *out, size_t size, const char *text) {
    while (*text && isspace((unsigned char)*text)) text++;
    snprintf(out, size, "%s", text);
    size_t n = strlen(out);
    while (n && isspace((unsigned char)out[n - 1])) out[--n] = '\0';
}

static int tag_slot(const char *name) {
    for (int i = 0; i < def.tag_count; ++i)
        if (!strcmp(def.tags[i], name)) return i;
    if (def.tag_count >= MAX_TAGS || !name[0]) return -1;
    snprintf(def.tags[def.tag_count], sizeof(def.tags[0]), "%s", name);
    return def.tag_count++;
}

static int trigger_named(const char *name) {
    for (int i = 0; i < def.trigger_count; ++i)
        if (!strcmp(def.triggers[i].name, name)) return i;
    return -1;
}

static bool bad(const char *line) {
    fprintf(stderr, "rustfront: bad mission line \"%s\"\n", line);
    def.errors++;
    return true;
}

static bool in_map(irect_t r, int width, int height) {
    return r.x >= 0 && r.y >= 0 && r.w > 0 && r.h > 0 && r.x + r.w <= width && r.y + r.h <= height;
}

/* "KIND ARGS | TEXT": the objective's own words, else a plain one. */
static bool parse_objective(const char *line, const char *rest, int width, int height) {
    if (def.objective_count >= RF_MAX_OBJECTIVES) return bad(line);
    objective_t o = {.tag = -1, .count = 1};
    char kind[24] = "", arg[24] = "";
    const char *bar = strchr(rest, '|');
    if (sscanf(rest, "%23s", kind) != 1) return bad(line);
    int k = 0;
    while (k < OBJ_KIND_COUNT && strcmp(kind, OBJ_NAMES[k])) k++;
    if (k == OBJ_KIND_COUNT) return bad(line);
    o.kind = (objkind_t)k;
    const char *args = skip_word(rest);
    switch (o.kind) {
    case OBJ_DESTROY_ALL:
        break;
    case OBJ_DESTROY:
    case OBJ_PROTECT:
        if (sscanf(args, "%15s", arg) != 1 || (o.tag = tag_slot(arg)) < 0) return bad(line);
        break;
    case OBJ_SURVIVE:
        if (sscanf(args, "%d", &o.seconds) != 1 || o.seconds <= 0) return bad(line);
        break;
    case OBJ_HOLD:
        if (sscanf(args, "%d %d %d %d %d", &o.area.x, &o.area.y, &o.area.w, &o.area.h, &o.seconds) != 5 ||
            !in_map(o.area, width, height) || o.seconds <= 0) return bad(line);
        break;
    case OBJ_CREDITS:
        if (sscanf(args, "%d", &o.amount) != 1 || o.amount <= 0) return bad(line);
        break;
    case OBJ_BUILD:
        if (sscanf(args, "%23s %d", arg, &o.count) < 1 || !(o.type = RF_UnitType(arg)) || o.count <= 0)
            return bad(line);
        break;
    default:
        return bad(line);
    }
    if (bar) copy_text(o.text, sizeof(o.text), bar + 1);
    if (!o.text[0]) snprintf(o.text, sizeof(o.text), "%s", OBJ_NAMES[o.kind]);
    def.objectives[def.objective_count++] = o;
    return true;
}

static bool parse_trigger(const char *line, const char *rest, int width, int height) {
    if (def.trigger_count >= MAX_TRIGGERS) return bad(line);
    trigger_t t = {.tag = -1};
    char kind[16], arg[24];
    if (sscanf(rest, "%23s %15s", t.name, kind) != 2 || trigger_named(t.name) >= 0) return bad(line);
    const char *args = skip_word(skip_word(rest));
    int seconds = 0, every = 0;
    if (!strcmp(kind, "time")) {
        t.kind = TRIG_TIME;
        if (sscanf(args, "%d %d %d", &seconds, &every, &t.times) < 1 || seconds < 0 || every < 0 || t.times < 0)
            return bad(line);
        t.at_ms = seconds * 1000;
        t.every_ms = every * 1000;
        if (!every) t.times = 1;
    } else if (!strcmp(kind, "area")) {
        t.kind = TRIG_AREA;
        if (sscanf(args, "%d %d %d %d %d", &t.area.x, &t.area.y, &t.area.w, &t.area.h, &t.owner) < 4 ||
            !in_map(t.area, width, height) || t.owner < 0 || t.owner >= MAXPLAYERS) return bad(line);
    } else if (!strcmp(kind, "destroyed")) {
        t.kind = TRIG_DESTROYED;
        if (sscanf(args, "%15s", arg) != 1 || (t.tag = tag_slot(arg)) < 0) return bad(line);
    } else if (!strcmp(kind, "built")) {
        t.kind = TRIG_BUILT;
        if (sscanf(args, "%23s", arg) != 1 || !(t.type = RF_UnitType(arg))) return bad(line);
    } else {
        return bad(line);
    }
    def.triggers[def.trigger_count++] = t;
    return true;
}

static bool parse_spawn(const char *line, const char *rest, int width, int height) {
    char trig[24], name[24];
    action_t a = {.tag = -1};
    int n = sscanf(rest, "%23s %23s %d %d %d %d", trig, name, &a.owner, &a.cell.x, &a.cell.y, &a.facing);
    if (n < 5 || def.action_count >= MAX_ACTIONS || (a.trigger = trigger_named(trig)) < 0 ||
        !(a.type = RF_UnitType(name)) || a.owner < 0 || a.owner >= MAXPLAYERS ||
        a.cell.x < 0 || a.cell.y < 0 || a.cell.x >= width || a.cell.y >= height) return bad(line);
    const char *at = strchr(rest, '@');
    if (at) {
        char tag[16];
        if (sscanf(at + 1, "%15s", tag) != 1 || (a.tag = tag_slot(tag)) < 0) return bad(line);
        def.tag_defined[a.tag] = true;
    }
    a.facing &= 15;
    def.actions[def.action_count++] = a;
    return true;
}

static bool parse_message(const char *line, const char *rest) {
    char trig[24];
    action_t a = {.message = true, .tag = -1};
    if (sscanf(rest, "%23s", trig) != 1 || def.action_count >= MAX_ACTIONS ||
        (a.trigger = trigger_named(trig)) < 0) return bad(line);
    copy_text(a.text, sizeof(a.text), skip_word(rest));
    if (!a.text[0]) return bad(line);
    def.actions[def.action_count++] = a;
    return true;
}

void RF_CampaignBeginLoad(void) {
    memset(&def, 0, sizeof(def));
    memset(&run, 0, sizeof(run));
    def.faction = def.index = -1;
    pending_count = 0;
    last_message[0] = '\0';
}

bool RF_CampaignParseLine(const char *line, int width, int height) {
    const char *rest;
    if (keyword(line, "mission", &rest)) {
        char slug[24];
        int n;
        if (sscanf(rest, "%23s %d", slug, &n) != 2 || faction_of_slug(slug) < 0 || n < 1 ||
            n > RF_CAMPAIGN_MISSIONS) return bad(line);
        def.active = true;
        def.faction = faction_of_slug(slug);
        def.index = n - 1;
    } else if (keyword(line, "title", &rest)) {
        copy_text(def.title, sizeof(def.title), rest);
    } else if (keyword(line, "briefing", &rest)) {
        /* the front end's (RF_MissionInfo) */
    } else if (keyword(line, "objective", &rest)) {
        return parse_objective(line, rest, width, height);
    } else if (keyword(line, "tech", &rest)) {
        def.tech_limited = true;
        char name[24];
        for (int n; sscanf(rest, "%23s%n", name, &n) == 1; rest += n) {
            uint16_t type = RF_UnitType(name);
            if (!type || def.tech_count >= MAX_TECH) return bad(line);
            def.tech[def.tech_count++] = type;
        }
    } else if (keyword(line, "trigger", &rest)) {
        return parse_trigger(line, rest, width, height);
    } else if (keyword(line, "spawn", &rest)) {
        return parse_spawn(line, rest, width, height);
    } else if (keyword(line, "message", &rest)) {
        return parse_message(line, rest);
    } else {
        return false;
    }
    return true;
}

void RF_CampaignTagged(const char *tag, const mobj_t *thing) {
    int slot = tag_slot(tag);
    if (slot >= 0 && thing) run.tag_id[slot] = thing->id;
}

/* Tags are defined by "unit ... @TAG" lines, which the loader parses itself, so they are
 * checked once the whole map is read (RF_LoadThings spawns them afterwards). */
void RF_CampaignEndLoad(const char *path) {
    if (!def.active) {
        if (def.objective_count || def.trigger_count) {
            fprintf(stderr, "rustfront: %s has mission keys but no mission line\n", path);
            def.errors++;
        }
        return;
    }
    if (!def.objective_count) {
        fprintf(stderr, "rustfront: %s: a mission needs an objective\n", path);
        def.errors++;
    }
    for (int i = 0; i < def.objective_count; ++i)
        if (def.objectives[i].kind == OBJ_PROTECT && def.objective_count == 1) {
            fprintf(stderr, "rustfront: %s: protect alone can never be won\n", path);
            def.errors++;
        }
}

/* The loader's own pass over "unit" lines marks their tags (w_rustfront.c keeps them). */
static bool tag_known(int slot) {
    return def.tag_defined[slot] || run.tag_id[slot];
}

int RF_MissionLoadErrors(void) {
    int errors = def.errors;
    for (int i = 0; i < def.tag_count; ++i)
        if (!tag_known(i)) {
            fprintf(stderr, "rustfront: mission tag @%s names no unit\n", def.tags[i]);
            errors++;
        }
    return errors;
}

bool RF_CampaignAllows(int owner, uint16_t type) {
    if (!def.active || !def.tech_limited || owner != 0 || netgame) return true;
    for (int i = 0; i < def.tech_count; ++i)
        if (def.tech[i] == type) return true;
    return false;
}

/* ── the running mission ───────────────────────────────────────────────── */

bool RF_InMission(int *faction, int *index) {
    if (!def.active) return false;
    if (faction) *faction = def.faction;
    if (index) *index = def.index;
    return true;
}

const char *RF_MissionTitle(void) { return def.active ? def.title : NULL; }
int RF_ObjectiveCount(void) { return def.active ? def.objective_count : 0; }
const char *RF_ObjectiveText(int i) {
    return i >= 0 && i < RF_ObjectiveCount() ? def.objectives[i].text : NULL;
}
int RF_ObjectiveState(int i) {
    return i >= 0 && i < RF_ObjectiveCount() ? run.state[i] : RF_OBJECTIVE_PENDING;
}
int RF_MissionElapsedMs(void) { return run.elapsed_ms; }

bool RF_ObjectiveDetail(int i, rf_objective_t *out) {
    if (i < 0 || i >= RF_ObjectiveCount() || !out) return false;
    const objective_t *o = &def.objectives[i];
    *out = (rf_objective_t){.kind = (int)o->kind, .seconds = o->seconds, .amount = o->amount, .count = o->count,
                            .held_ms = run.hold_ms[i], .area = o->area, .type = o->type,
                            .tag_id = o->tag >= 0 ? run.tag_id[o->tag] : 0};
    return true;
}
const char *RF_LastMissionMessage(void) { return last_message; }

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(last_message, sizeof(last_message), fmt, args);
    va_end(args);
    if (pending_count == MAX_PENDING) {
        memmove(pending[0], pending[1], sizeof(pending[0]) * (MAX_PENDING - 1));
        pending_count--;
    }
    snprintf(pending[pending_count++], sizeof(pending[0]), "%s", last_message);
}

static bool alive(const mobj_t *unit) {
    return unit && unit->thinker.function == P_MobjThinker && !unit->remove && unit->hp > 0;
}

static mobj_t *by_id(mobj_t *const *mobjs, int count, uint32_t id) {
    for (int i = 0; id && i < count; ++i)
        if (mobjs[i] && mobjs[i]->id == id) return mobjs[i];
    return NULL;
}

/* A unit that keeps an enemy side in the field: not an effect, a shot or the player's. */
static bool enemy(const mobj_t *unit) {
    return alive(unit) && unit->owner != 0 && unit->owner < MAXPLAYERS && unit->info &&
           G_PlayerSide(unit->owner) && G_UnitHoldsSide(unit);
}

static bool player_unit(const mobj_t *unit, int owner) {
    return alive(unit) && unit->owner == owner && unit->info && G_UnitHoldsSide(unit);
}

static bool inside(const mobj_t *unit, irect_t r) {
    ivec2_t c = fixed2_cell(fixed3_xy(unit->core.position));
    return c.x >= r.x && c.y >= r.y && c.x < r.x + r.w && c.y < r.y + r.h;
}

/* Tagged thing state: -1 not yet in the world, 0 lost, 1 standing. */
static int tag_state(mobj_t *const *mobjs, int count, int slot) {
    if (slot < 0 || !run.tag_id[slot]) return -1;
    return alive(by_id(mobjs, count, run.tag_id[slot])) ? 1 : 0;
}

static int built(mobj_t *const *mobjs, int count, uint16_t type) {
    int n = 0;
    for (int i = 0; i < count; ++i)
        n += player_unit(mobjs[i], 0) && mobjs[i]->type_id == type && !RF_IsSite(mobjs[i]);
    return n;
}

static void add_hunter(const mobj_t *unit) {
    if (run.hunter_count < MAX_HUNTERS) run.hunters[run.hunter_count++] = unit->id;
}

/* Enemy units a trigger brought in go for the player's nearest unit, and again whenever they
 * stand idle. */
static void hunt(level_t *map, mobj_t *const *mobjs, int count) {
    int kept = 0;
    for (int h = 0; h < run.hunter_count; ++h) {
        mobj_t *u = by_id(mobjs, count, run.hunters[h]);
        if (!alive(u)) continue;
        run.hunters[kept++] = run.hunters[h];
        if (!(u->traits & MF_MOBILE) || !(u->traits & MF_ATTACK) || P_HasMoveOrder(u) ||
            (u->attack.target && alive(u->attack.target))) continue;
        mobj_t *goal = NULL;
        int64_t best = 0;
        for (int i = 0; i < count; ++i) {
            if (!player_unit(mobjs[i], 0)) continue;
            int64_t d = fixed2_distance_squared64(fixed3_xy(mobjs[i]->core.position), fixed3_xy(u->core.position));
            if (!goal || d < best) { goal = mobjs[i]; best = d; }
        }
        if (!goal) continue;
        /* Move only: a unit picks up whatever comes into range on the way. (Chasing a distant
         * structure as its attack target can leave it with no route, for one on a mesa.) */
        mobj_t *one[1] = {u};
        P_MoveUnitsAt(map, one, 1, fixed3_xy(goal->core.position));
    }
    run.hunter_count = kept;
}

static void fire(int t) {
    run.fired[t]++;
    bool spawned = false;
    for (int i = 0; i < def.action_count; ++i) {
        const action_t *a = &def.actions[i];
        if (a->trigger != t) continue;
        if (a->message) { say("%s", a->text); continue; }
        mobj_t *mo = RF_SpawnThing(a->type, a->owner, a->facing, a->cell);
        if (!mo) continue;
        spawned = true;
        if (a->tag >= 0) run.tag_id[a->tag] = mo->id;
        if (a->owner != 0 && (mo->traits & MF_MOBILE)) add_hunter(mo);
    }
    if (spawned) RF_SyncBlocking(&level);
}

static void tick_triggers(mobj_t *const *mobjs, int count) {
    for (int t = 0; t < def.trigger_count; ++t) {
        const trigger_t *tr = &def.triggers[t];
        if (run.done[t]) continue;
        bool now = false;
        switch (tr->kind) {
        case TRIG_TIME:
            if (!run.fired[t] && !run.next_ms[t]) run.next_ms[t] = tr->at_ms;
            now = run.elapsed_ms >= run.next_ms[t];
            break;
        case TRIG_AREA:
            for (int i = 0; i < count && !now; ++i)
                now = player_unit(mobjs[i], tr->owner) && (mobjs[i]->traits & MF_MOBILE) && inside(mobjs[i], tr->area);
            break;
        case TRIG_DESTROYED:
            now = tag_state(mobjs, count, tr->tag) == 0;
            break;
        case TRIG_BUILT:
            now = built(mobjs, count, tr->type) > 0;
            break;
        }
        if (!now) continue;
        fire(t);
        if (tr->kind == TRIG_TIME && tr->every_ms && (!tr->times || run.fired[t] < tr->times))
            run.next_ms[t] += tr->every_ms;
        else
            run.done[t] = 1;
    }
}

static void announce_time(const objective_t *o, int before_ms, int now_ms) {
    int total = o->seconds * 1000;
    for (int left = 60000; left < total; left += 60000)
        if (total - before_ms > left && total - now_ms <= left) {
            if (left == 60000) say("One minute remains.");
            else say("%d minutes remain.", left / 60000);
        }
    if (total > 30000 && total - before_ms > 30000 && total - now_ms <= 30000) say("30 seconds remain.");
}

static bool objective_met(int i, mobj_t *const *mobjs, int count, int dt_ms, int before_ms) {
    objective_t *o = &def.objectives[i];
    switch (o->kind) {
    case OBJ_DESTROY_ALL:
        for (int k = 0; k < count; ++k)
            if (enemy(mobjs[k])) return false;
        return true;
    case OBJ_DESTROY:
        return tag_state(mobjs, count, o->tag) == 0;
    case OBJ_PROTECT:
        return false;
    case OBJ_SURVIVE:
        announce_time(o, before_ms, run.elapsed_ms);
        return run.elapsed_ms >= o->seconds * 1000;
    case OBJ_HOLD: {
        bool mine = false, theirs = false;
        for (int k = 0; k < count; ++k) {
            const mobj_t *u = mobjs[k];
            if (!inside(u, o->area)) continue;
            if (player_unit(u, 0) && (u->traits & MF_MOBILE) && !(u->traits & MF_HARVESTER)) mine = true;
            if (enemy(u)) theirs = true;
        }
        int was = run.hold_ms[i];
        run.hold_ms[i] = mine && !theirs ? run.hold_ms[i] + dt_ms : 0;
        if (!was && run.hold_ms[i]) say("Holding the ground: %d seconds to go.", o->seconds);
        else if (was && !run.hold_ms[i]) say("The ground is lost. Take it back.");
        return run.hold_ms[i] >= o->seconds * 1000;
    }
    case OBJ_CREDITS:
        return level.player_resources[0][0] >= o->amount;
    case OBJ_BUILD:
        return built(mobjs, count, o->type) >= o->count;
    default:
        return false;
    }
}

void RF_CampaignTicker(level_t *map, mobj_t *const *mobjs, int count, hudtext_t *hud, int dt_ms) {
    if (def.active && !netgame && !level.outcome && !run.victory && !run.defeat) {
        if (!run.started) {
            run.started = 1;
            if (def.title[0]) say("%s", def.title);
            for (int i = 0; i < def.objective_count; ++i)
                say("%s: %s", def.objectives[i].kind == OBJ_PROTECT ? "Protect" : "Objective",
                    def.objectives[i].text);
        }
        int before = run.elapsed_ms;
        run.elapsed_ms += dt_ms;
        tick_triggers(mobjs, count);
        if (run.elapsed_ms / 2000 != before / 2000) hunt(map, mobjs, count);
        bool all = true;
        for (int i = 0; i < def.objective_count; ++i) {
            const objective_t *o = &def.objectives[i];
            if (o->kind == OBJ_PROTECT) {
                if (tag_state(mobjs, count, o->tag) == 0) {
                    run.state[i] = RF_OBJECTIVE_FAILED;
                    run.defeat = 1;
                    say("Mission failed: %s", o->text);
                }
                continue;
            }
            if (run.state[i] != RF_OBJECTIVE_DONE && objective_met(i, mobjs, count, dt_ms, before)) {
                run.state[i] = RF_OBJECTIVE_DONE;
                say("Objective complete: %s", o->text);
            }
            all = all && run.state[i] == RF_OBJECTIVE_DONE;
        }
        if (all && !run.defeat) {
            run.victory = 1;
            say("Mission accomplished.");
        }
    }
    for (int i = 0; i < pending_count; ++i) HU_PushMessage(hud, pending[i], MESSAGE_MS);
    pending_count = 0;
}

/* The engine asks once a tic: the mission's own end, then elimination for defeat only, so a
 * mission is never won by killing every enemy unless it says so. */
unsigned G_LevelObjectives(mobj_t *const *units, int count) {
    (void)units; (void)count;
    if (!def.active || netgame) return ELIMINATE_ALL;
    if (run.defeat) {
        G_EndLevel(OUTCOME_DEFEAT);
    } else if (run.victory) {
        if (!run.recorded) {
            run.recorded = 1;
            RF_CampaignRecordWin(def.faction, def.index);
        }
        G_EndLevel(OUTCOME_VICTORY);
    }
    return ELIMINATE_DEFEAT;
}

/* ── saved games ───────────────────────────────────────────────────────── */

size_t RF_CampaignSaveSize(void) { return def.active ? sizeof(run) : 0; }
void RF_CampaignSave(void *out) { memcpy(out, &run, sizeof(run)); }
bool RF_CampaignLoad(const void *data, size_t size) {
    if (!size) return true;
    if (size != sizeof(run) || !def.active) return false;
    memcpy(&run, data, sizeof(run));
    pending_count = 0;
    return true;
}

/* ── the front end's view of a mission file ────────────────────────────── */

bool RF_MissionInfo(const char *root, int faction, int index, rf_missioninfo_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!RF_MissionPath(faction, index, out->map, sizeof(out->map))) return false;
    out->faction = faction;
    out->index = index;
    char path[1200];
    M_PathJoin(path, sizeof(path), root && root[0] ? root : g_game_default_root, out->map);
    blob_t file;
    if (!W_ReadFile(path, &file)) return false;
    size_t used = 0;
    for (size_t at = 0; at < file.size;) {
        size_t end = at;
        while (end < file.size && file.bytes[end] != '\n') end++;
        char line[600];
        snprintf(line, sizeof(line), "%.*s", (int)(end - at), (const char *)file.bytes + at);
        at = end + 1;
        size_t n = strlen(line);
        if (n && line[n - 1] == '\r') line[--n] = '\0';
        const char *rest;
        if (keyword(line, "title", &rest)) {
            copy_text(out->title, sizeof(out->title), rest);
        } else if (keyword(line, "briefing", &rest)) {
            char para[600];
            copy_text(para, sizeof(para), rest);
            int wrote = snprintf(out->briefing + used, sizeof(out->briefing) - used, "%s%s", used ? "\n" : "", para);
            if (wrote > 0) used += (size_t)wrote;
            if (used >= sizeof(out->briefing)) used = sizeof(out->briefing) - 1;
        } else if (keyword(line, "objective", &rest) && out->objective_count < RF_MAX_OBJECTIVES) {
            const char *bar = strchr(rest, '|');
            copy_text(out->objectives[out->objective_count++], RF_OBJECTIVE_TEXT, bar ? bar + 1 : rest);
        }
    }
    W_FreeFile(&file);
    return out->title[0] != '\0';
}
