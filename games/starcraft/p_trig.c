#include "sc_local.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* CHK triggers are 2400 bytes: 16 conditions, 64 actions, then a 28-byte
 * execution mask at +2372. Victory and Defeat end the local human's scenario
 * even when the trigger runs for another force. Retail campaign maps do not
 * use Set Next Scenario; the next file is the next numbered mission. */

static sc_mission_t *mission(level_t *map) {
    return map && map->mission ? map->mission : NULL;
}

/* OpenBW's synchronized LCG; triggers and weapon launch rolls share it. */
uint32_t sc_random(void) {
    sc_mission_t *m = mission(&level);
    if (!m) return 0;
    m->rng = m->rng * UINT32_C(22695477) + 1;
    return (m->rng >> 16) & 0x7fff;
}

int sc_mission_result(void) {
    sc_mission_t *m = mission(&level);
    return m ? m->result : 0;
}

bool sc_take_camera(fvec2_t *cell) {
    sc_mission_t *m = mission(&level);
    if (!m || !m->view_pending || !cell) return false;
    m->view_pending = false;
    *cell = m->view;
    return true;
}

const char *sc_objectives_text(void) {
    sc_mission_t *m = mission(&level);
    return m ? m->objectives : "";
}

int sc_owner_kind(int owner) {
    sc_mission_t *m = mission(&level);
    return m && owner >= 0 && owner < 12 ? m->owners[owner] : -1;
}

int sc_player_side(int owner) {
    sc_mission_t *m = mission(&level);
    return m && owner >= 0 && owner < 12 ? m->side[owner] : 1;
}

bool sc_player_ai(int owner) {
    sc_mission_t *m = mission(&level);
    return m && owner >= 0 && owner < 8 && m->ai_on[owner];
}

uint8_t sc_allegiance_for(uint8_t owner) {
    sc_mission_t *m = mission(&level);
    if (!m || owner >= 8) return ALLEGIANCE_NEUTRAL;
    uint8_t kind = m->owners[owner];
    if (kind == 0 || kind == 3 || kind == 7) return ALLEGIANCE_NEUTRAL;
    return owner == consoleplayer ? ALLEGIANCE_PLAYER : ALLEGIANCE_ENEMY;
}

bool sc_campaign_next(const char *map_path, char *out, size_t size) {
    sc_mission_t *m = mission(&level);
    if (!out || !size) return false;
    out[0] = '\0';
    if (m && m->next_scenario[0] && access(m->next_scenario, R_OK) == 0) {
        snprintf(out, size, "%s", m->next_scenario);
        return true;
    }
    if (!map_path) return false;
    const char *camp = strstr(map_path, "campaign/");
    if (!camp) return false;
    const char *race = camp + 9, *slash = strchr(race, '/');
    if (!slash || slash == race) return false;
    char race_name[32];
    size_t rlen = (size_t)(slash - race);
    if (rlen >= sizeof(race_name)) return false;
    memcpy(race_name, race, rlen);
    race_name[rlen] = '\0';
    const char *folder = slash + 1, *folder_end = strchr(folder, '/');
    if (!folder_end) return false;
    char next_folder[32];
    if ((size_t)(folder_end - folder) == 8 && !memcmp(folder, "tutorial", 8))
        snprintf(next_folder, sizeof(next_folder), "%s01", race_name);
    else if ((size_t)(folder_end - folder) > rlen && !memcmp(folder, race_name, rlen)) {
        int num = atoi(folder + rlen);
        if (num < 1) return false;
        snprintf(next_folder, sizeof(next_folder), "%s%02d", race_name, num + 1);
    } else return false;
    char rel[256], replaced[1024];
    snprintf(rel, sizeof(rel), "install/campaign/%s/%s/staredit/scenario.chk", race_name, next_folder);
    size_t prefix = (size_t)(folder - map_path);
    if (prefix >= sizeof(replaced)) return false;
    memcpy(replaced, map_path, prefix);
    if (snprintf(replaced + prefix, sizeof(replaced) - prefix, "%s%s", next_folder, folder_end) < 0)
        return false;
    char rooted[1024];
    snprintf(rooted, sizeof(rooted), "data/STARCRAFT/%s", rel);
    if (access(replaced, R_OK) && access(rel, R_OK) && access(rooted, R_OK)) return false;
    if (strlen(rel) >= size) return false;
    memcpy(out, rel, strlen(rel) + 1);
    return true;
}

static bool slot_active(const sc_mission_t *m, int p) {
    return p >= 0 && p < 8 && m->owners[p] != 0;
}

static bool allied_to(int self, int other) {
    return self >= 0 && self < 8 && other >= 0 && other < 8 &&
        (G_AllianceMasks(&level)[self] & (UINT32_C(0x40000000) >> other));
}

/* PyMS TRG.py player ids: 0..11 players, 13 Current Player, 14 Foes,
 * 15 Allies, 16 Neutral Players, 17 All Players, 18..21 Forces, 26 Non
 * Allied Victory Players. Each trigger runs once per owning player, so the
 * current player is always one slot. */
static bool in_group(const sc_mission_t *m, unsigned group, int p, int current) {
    if (p < 0 || p >= 8) return false;
    if (group < 8) return p == (int)group;
    if (group == 13) return p == current;
    if (group == 16) return m->owners[p] == 7;
    if (group == 17) return slot_active(m, p);
    if (group >= 18 && group <= 21) return slot_active(m, p) && m->force[p] == (int)group - 18;
    if (group == 14 || group == 15 || group == 26) {
        if (p == current || !slot_active(m, p) || m->owners[p] == 7 || m->owners[p] == 3) return false;
        bool ally = allied_to(current, p);
        return group == 15 ? ally : !ally;
    }
    return false;
}

/* The players a trigger belongs to: its own bits and the groups it names. */
static bool owns_trigger(const sc_mission_t *m, const uint8_t *mask, int p) {
    if (mask[p] == 1) return true;
    if (mask[17] == 1 && slot_active(m, p)) return true;
    for (int f = 0; f < 4; f++) if (mask[18 + f] == 1 && slot_active(m, p) && m->force[p] == f) return true;
    return false;
}

static int collect_players(const sc_mission_t *m, unsigned group, int current, int *out) {
    int n = 0;
    for (int p = 0; p < 8; p++) if (in_group(m, group, p, current)) out[n++] = p;
    return n;
}

static bool loc_box(const sc_mission_t *m, unsigned id, int *l, int *t, int *r, int *b) {
    if (id == 64 || id == 0) {
        *l = *t = 0;
        *r = level.width * 32;
        *b = level.height * 32;
        return true;
    }
    if (id > 64) return false;
    int i = (int)id - 1;
    if (i >= m->loc_count) return false;
    *l = m->loc_left[i]; *t = m->loc_top[i];
    *r = m->loc_right[i]; *b = m->loc_bottom[i];
    return true;
}

static bool inside(const mobj_t *mo, int l, int t, int r, int b) {
    fixed2_t at = fixed3_xy(mo->core.position);
    int x = at.x >> 11, y = at.y >> 11; /* 32 map pixels per cell */
    return x >= l && x <= r && y >= t && y <= b;
}

static bool location_height_ok(const sc_mission_t *m, const mobj_t *mo, unsigned loc) {
    if (!loc || loc > (unsigned)m->loc_count || loc > 64) return true;
    unsigned flags = m->loc_elevation[loc - 1];
    if (!flags) return true;
    if (!m->terrain || !L_Contains(&level, fixed_floor_int(mo->core.position.x),
                                   fixed_floor_int(mo->core.position.y))) return true;
    unsigned terrain = sc_terrain_flags(fixed3_xy(mo->core.position));
    int height = (terrain & SC_TERRAIN_HIGH) ? 2 : (terrain & SC_TERRAIN_MIDDLE) ? 1 : 0;
    return !(flags & (1u << (height + ((mo->traits & MF_FLY) ? 0 : 3))));
}

static bool kind_ok(const mobj_t *mo, unsigned id) {
    if (!mo->type_id || mo->type_id > SC_TYPES) return false;
    unsigned type = mo->type_id - 1;
    if (sc_units[type].flags & SC_UNIT_TURRET) return false;
    int race = sc_units[type].race;
    if (id == 229) return true;
    if (id == 228) return false;
    if (id == 230) return (race & 8) != 0;
    if (id == 231) return (race & 16) != 0;
    if (id == 232) return (race & 32) != 0;
    return type == id;
}

static bool alive_unit(const mobj_t *mo) {
    return mo->hp > 0 && !mo->remove && !(mo->sc.flags & SC_HALLUCINATION);
}

/* OpenBW clears completed status while Zerg units and buildings morph. */
static bool trigger_completed(const mobj_t *mo) {
    if (!mo->production || !mo->production->queue_count) return true;
    if (mo->type_id == MT_EGG || mo->type_id == MT_COCOON) return false;
    const sc_unit_t *type = sc_unit(mo);
    return !type || !(type->flags & SC_UNIT_BUILDING) || !(type->race & 1) ||
           mo->production->product_class != RTS_PRODUCT_BUILDING;
}

static int count_units(const int *players, int nplayers, unsigned unit, unsigned loc,
                       const sc_mission_t *m, int cargo, bool completed) {
    int l, t, r, b;
    bool anywhere = !loc || loc == 64;
    if (!anywhere && !loc_box(m, loc, &l, &t, &r, &b)) return 0;
    int n = 0;
    if (!thinkercap.next) return 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || mo->hp <= 0 || mo->remove ||
            !sc_unit(mo) ||
            (cargo < 0 && (mo->sc.flags & SC_HALLUCINATION)) ||
            (completed && !trigger_completed(mo))) continue;
        bool virtual_hangar = cargo > 0 && mo->sc.hangar > 0 &&
            ((sc_hangar_type(mo->type_id) == MT_INTERCEPTOR && unit == MT_INTERCEPTOR - 1) ||
             (mo->type_id == MT_NUCLEAR_SILO && unit == MT_NUCLEAR_MISSILE - 1));
        if (!kind_ok(mo, unit) && !virtual_hangar) continue;
        const mobj_t *position = mo;
        if (cargo >= 0 && (mo->sc.flags & SC_LOADED)) {
            if (!cargo) continue;
            position = P_MobjById(mo->sc.parent);
            if (!position || position->remove || position->hp <= 0) continue;
        }
        bool mine = false;
        for (int i = 0; i < nplayers; i++) if (players[i] == mo->owner) mine = true;
        if (!mine || !location_height_ok(m, position, loc) || (!anywhere && !inside(position, l, t, r, b))) continue;
        if (kind_ok(mo, unit)) n++;
        if (virtual_hangar) n += mo->sc.hangar;
    }
    return n;
}

static bool cmp_num(uint8_t op, uint64_t value, uint32_t qty) {
    if (op == 0) return value >= qty;
    if (op == 1) return value <= qty;
    if (op == 10) return value == qty;
    return false;
}

static uint64_t player_kills(const sc_mission_t *m, int player, unsigned unit) {
    if (unit < SC_TYPES) return m->kills[player][unit];
    uint64_t total = 0;
    for (int t = 0; t < SC_TYPES; t++) total += m->kills[player][t];
    return total;
}

static bool condition_ok(sc_mission_t *m, const uint8_t *c, int current) {
    uint8_t type = c[15], op = c[14];
    unsigned group = read_u32_le(c + 4), qty = read_u32_le(c + 8);
    unsigned unit = read_u16_le(c + 12), loc = read_u32_le(c);
    int players[8], n = collect_players(m, group, current, players);
    if (type == 22) return true;
    if (type == 23) return false;
    if (type == 1) return cmp_num(op, m->countdown_ms / 1000, qty);
    if (type == 12) return cmp_num(op, m->elapsed_ms / 1000, qty);
    if (type == 11) {
        unsigned id = c[16];
        if (id > 255) return false;
        if (op == 2) return m->switches[id];
        if (op == 3) return !m->switches[id];
        return false;
    }
    if (type == 2 || type == 3) {
        unsigned where = type == 3 ? loc : 0;
        return cmp_num(op, count_units(players, n, unit, where, m, type == 3 ? op != 1 : -1, op != 1), qty);
    }
    if (type == 4) {
        uint64_t total = 0;
        for (int i = 0; i < n; i++) {
            if (c[16] == 0 || c[16] == 2) total += level.player_resources[players[i]][0];
            if (c[16] == 1 || c[16] == 2) total += level.player_resources[players[i]][1];
        }
        return cmp_num(op, total, qty);
    }
    if (type == 5 || type == 15) {
        uint64_t total = 0;
        for (int i = 0; i < n; i++) {
            if (unit < SC_TYPES) total += type == 15 ? m->deaths[players[i]][unit] : m->kills[players[i]][unit];
            else if (unit == 229) for (int t = 0; t < SC_TYPES; t++)
                total += type == 15 ? m->deaths[players[i]][t] : m->kills[players[i]][t];
        }
        return cmp_num(op, total, qty);
    }
    if (type == 8) {
        if (unit >= SC_TYPES && unit != 229) return false;
        uint64_t mine = player_kills(m, current, unit);
        for (int p = 0; p < 8; p++)
            if (p != current && slot_active(m, p) && player_kills(m, p, unit) > mine) return false;
        return true;
    }
    if (type == 6 || type == 7 || type == 16 || type == 17) {
        players[0] = current;
        n = 1;
        unsigned where = (type == 7 || type == 17) ? loc : 0;
        int mine = count_units(players, n, unit, where, m, -1, false), others = 0;
        bool most = type == 6 || type == 7;
        for (int p = 0; p < 8; p++) {
            bool in = false;
            for (int i = 0; i < n; i++) if (players[i] == p) in = true;
            if (in || !slot_active(m, p)) continue;
            int one = p;
            int count = count_units(&one, 1, unit, where, m, -1, false);
            others++;
            if (most ? count >= mine : count <= mine) return false;
        }
        return others > 0 || mine > 0;
    }
    if (type == 14) {
        int foes = 0;
        for (int i = 0; i < n; i++) for (int p = 0; p < 8; p++)
            if (G_PlayerSide(players[i]) && p != players[i] && G_PlayerSide(p) &&
                (!allied_to(players[i], p) || !allied_to(p, players[i]))) foes++;
        return cmp_num(op, foes, qty);
    }
    return false;
}

static bool conditions_ok(sc_mission_t *m, const uint8_t *trig, int current) {
    bool any = false;
    for (int i = 0; i < 16; i++) {
        const uint8_t *c = trig + i * 20;
        if (!c[15]) break;
        any = true;
        if (c[17] & 2) continue;
        if (!condition_ok(m, c, current)) return false;
    }
    return any;
}

static const char *str_text(const sc_mission_t *m, unsigned id, char *buf, size_t n) {
    if (!n) return "";
    buf[0] = '\0';
    if (!m->str || m->str_size < 2 || !id) return buf;
    unsigned count = read_u16_le(m->str);
    if (id > count || 2u + count * 2u > m->str_size) return buf;
    unsigned off = read_u16_le(m->str + id * 2);
    if (off >= m->str_size) return buf;
    size_t w = 0;
    for (size_t i = off; i < m->str_size && m->str[i] && w + 1 < n; i++) {
        unsigned char ch = m->str[i];
        if (ch == '\r') continue;
        if (ch < 32 && ch != '\n') continue;
        buf[w++] = (char)ch;
    }
    buf[w] = '\0';
    return buf;
}

static void say(hudtext_t *hud, const char *text, int ttl) {
    if (text && text[0]) HU_PushMessage(hud, text, ttl);
}

static fixed2_t loc_center(const sc_mission_t *m, unsigned id) {
    int l, t, r, b;
    if (!loc_box(m, id, &l, &t, &r, &b)) return (fixed2_t){level.width * (FIXED_ONE / 2), level.height * (FIXED_ONE / 2)};
    return (fixed2_t){(l + r) * (FIXED_ONE / 64), (t + b) * (FIXED_ONE / 64)};
}

/* Order actions use a modified Anywhere record like any other location. */
static bool order_loc_box(const sc_mission_t *m, unsigned id, int *l, int *t, int *r, int *b) {
    if (id != 64 || m->loc_count < 64) return loc_box(m, id, l, t, r, b);
    *l = m->loc_left[63]; *t = m->loc_top[63];
    *r = m->loc_right[63]; *b = m->loc_bottom[63];
    return true;
}

static fixed2_t order_loc_center(const sc_mission_t *m, unsigned id) {
    int l, t, r, b;
    if (!order_loc_box(m, id, &l, &t, &r, &b)) return loc_center(m, id);
    return (fixed2_t){(l + r) * (FIXED_ONE / 64), (t + b) * (FIXED_ONE / 64)};
}

static void look_at(sc_mission_t *m, unsigned id) {
    m->view = fvec2_from_fixed2(loc_center(m, id));
    m->view_pending = true;
    level.has_camera = true;
    level.camera = m->view;
}

static int gather(const sc_mission_t *m, const int *players, int nplayers, unsigned unit,
                  unsigned loc, int limit, mobj_t **out, int cap) {
    int l, t, r, b, n = 0;
    bool anywhere = !loc || loc == 64;
    if (!anywhere && !loc_box(m, loc, &l, &t, &r, &b)) return 0;
    if (!thinkercap.next) return 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && n < cap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || mo->hp <= 0 || mo->remove || !sc_unit(mo) ||
            (sc_unit(mo)->flags & SC_UNIT_TURRET) || (mo->sc.flags & SC_LOADED) ||
            !location_height_ok(m, mo, loc) || (!anywhere && !inside(mo, l, t, r, b))) continue;
        bool matches = false;
        if (sc_unit(mo)->space_provided) for (int c = 0; c < SC_CARGO; c++) {
            mobj_t *passenger = P_MobjById(mo->sc.cargo[c]);
            if (!passenger || !(passenger->sc.flags & SC_LOADED) || passenger->sc.parent != mo->id ||
                !kind_ok(passenger, unit)) continue;
            for (int i = 0; i < nplayers; i++) if (players[i] == passenger->owner) matches = true;
        }
        if (kind_ok(mo, unit)) for (int i = 0; i < nplayers; i++) if (players[i] == mo->owner) matches = true;
        if (!matches) continue;
        out[n++] = mo;
        if (limit && n >= limit) break;
    }
    return n;
}

/* A quantity of zero means all matching units, including more than 256. */
static void modify_vitals(const sc_mission_t *m, const int *players, int nplayers,
                          unsigned unit, unsigned loc, unsigned limit, unsigned pct, int id) {
    int l, t, r, b;
    bool anywhere = !loc || loc == 64;
    if (!anywhere && !loc_box(m, loc, &l, &t, &r, &b)) return;
    if (!thinkercap.next) return;
    if (pct > 100) pct = 100;
    unsigned count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || !alive_unit(mo) || !kind_ok(mo, unit)) continue;
        bool mine = false;
        for (int i = 0; i < nplayers; i++) if (players[i] == mo->owner) mine = true;
        if (!mine || (!location_height_ok(m, mo, loc) || (!anywhere && !inside(mo, l, t, r, b)))) continue;
        const sc_unit_t *type = &sc_units[mo->type_id - 1];
        if (id == 49) {
            if (!pct) P_DamageMobj(mo, NULL, mo->hp);
            else { mo->hp = mo->max_hp * (int)pct / 100; if (mo->hp < 1) mo->hp = 1; mo->sc.wound = 0; }
        } else if ((id == 50 && (type->flags & SC_UNIT_SPELLCASTER)) || (id == 51 && type->shields)) {
            sc_start(mo);
            if (id == 50) mo->sc.energy = sc_max_energy(mo) * (int)pct / 100;
            else mo->sc.shields = (type->shields << 8) * (int)pct / 100;
        }
        if (limit && ++count >= limit) break;
    }
}

/* Resource deposits are level vents linked to the native resource mobj. */
static void modify_deposits(const sc_mission_t *m, unsigned player, int current,
                            unsigned loc, unsigned amount, unsigned limit) {
    int l, t, r, b;
    bool anywhere = !loc || loc == 64;
    if (!anywhere && !loc_box(m, loc, &l, &t, &r, &b)) return;
    if (!thinkercap.next) return;
    unsigned changed = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || !alive_unit(mo)) continue;
        bool resource = (mo->type_id >= MT_MINERAL_FIELD1 && mo->type_id <= MT_MINERAL_FIELD3) ||
            mo->type_id == MT_VESPENE_GEYSER || mo->type_id == MT_REFINERY ||
            mo->type_id == MT_EXTRACTOR || mo->type_id == MT_ASSIMILATOR;
        if (!resource) continue;
        bool matches = mo->owner < 8 ? in_group(m, player, mo->owner, current) :
            player == mo->owner || (player == 17 && mo->owner < 12);
        if (!matches || (!location_height_ok(m, mo, loc) || (!anywhere && !inside(mo, l, t, r, b)))) continue;
        for (int i = 0; i < level.resource_vent_count; i++) {
            resourcevent_t *vent = &level.resource_vents[i];
            if (vent->source_id != mo->id) continue;
            vent->amount = amount > INT_MAX ? INT_MAX : (int)amount;
            vent->depleted = !vent->amount && vent->depleted_rate > 0;
            vent->rate = vent->depleted ? vent->depleted_rate : 8;
            vent->active = mo->type_id != MT_VESPENE_GEYSER &&
                (vent->amount > 0 || vent->depleted || vent->exhausts_source);
            changed++;
            break;
        }
        if (limit && changed >= limit) break;
    }
}

/* CHK action 53 adds fighters to selected hangars, up to their capacity. */
static void modify_hangars(const sc_mission_t *m, const int *players, int nplayers,
                           unsigned unit, unsigned loc, unsigned limit, unsigned add) {
    int l, t, r, b;
    bool anywhere = !loc || loc == 64;
    if (!anywhere && !loc_box(m, loc, &l, &t, &r, &b)) return;
    if (!thinkercap.next) return;
    unsigned changed = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || !alive_unit(mo) || !kind_ok(mo, unit) ||
            (sc_hangar_type(mo->type_id) != MT_INTERCEPTOR && sc_hangar_type(mo->type_id) != MT_SCARAB) ||
            (!location_height_ok(m, mo, loc) || (!anywhere && !inside(mo, l, t, r, b)))) continue;
        bool mine = false;
        for (int i = 0; i < nplayers; i++) if (players[i] == mo->owner) mine = true;
        if (!mine) continue;
        int room = sc_hangar_capacity(mo) - sc_hangar_count(mo);
        if (room > 0) mo->sc.hangar += add < (unsigned)room ? (int)add : room;
        if (limit && ++changed >= limit) break;
    }
}

/* Move Unit is an immediate teleport, not a movement order. The shared
 * Recall placement searches for an available spot near the target. */
static void move_units(const sc_mission_t *m, const int *players, int nplayers,
                       unsigned unit, unsigned loc, unsigned destloc, unsigned limit) {
    int l, t, r, b, dl, dt, dr, db;
    if (!loc || !destloc || !loc_box(m, loc, &l, &t, &r, &b) ||
        !loc_box(m, destloc, &dl, &dt, &dr, &db) || !thinkercap.next) return;
    fixed2_t dest = loc_center(m, destloc);
    if (!L_Contains(&level, fixed_floor_int(dest.x), fixed_floor_int(dest.y))) return;
    unsigned moved = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function != P_MobjThinker || !alive_unit(mo) || !kind_ok(mo, unit) ||
            (mo->sc.flags & SC_LOADED) || !location_height_ok(m, mo, loc) || !inside(mo, l, t, r, b)) continue;
        bool mine = false;
        for (int i = 0; i < nplayers; i++) if (players[i] == mo->owner) mine = true;
        if (!mine) continue;
        if (!(mo->traits & MF_MOBILE)) {
            const sc_unit_t *type = sc_unit(mo);
            bool addon = sc_addon_parent(mo->type_id) != MT_NONE;
            if (!mo->info || !(type->flags & SC_UNIT_BUILDING) || mo->production || (mo->sc.parent && !addon) ||
                mo->type_id == MT_NYDUS_CANAL ||
                mo->type_id == MT_REFINERY || mo->type_id == MT_EXTRACTOR || mo->type_id == MT_ASSIMILATOR) continue;
            mobj_t *parent = addon && mo->sc.parent ? P_MobjById(mo->sc.parent) : NULL;
            if (mo->sc.parent && (!parent || sc_addon_of(parent) != mo)) continue;
            bool linked = false;
            for (thinker_t *child = thinkercap.next; child != &thinkercap; child = child->next)
                if (child->function == P_MobjThinker && ((mobj_t *)child)->id != mo->sc.addon &&
                    ((mobj_t *)child)->sc.parent == mo->id) { linked = true; break; }
            if (linked) continue;
            ivec2_t cell = fixed2_cell(dest);
            cell = ivec2_sub(cell, (ivec2_t){mo->info->footprint.w / 2, mo->info->footprint.h / 2});
            unsigned traits = mo->traits;
            mo->traits |= MF_NOBLOCKMAP;
            P_SyncBuildingBlocking();
            bool clear = P_CanPlaceBuilding(mo->type_id, cell, mo);
            mo->traits = traits;
            if (clear) {
                if (addon && mo->sc.parent) {
                    sc_detach_addon(parent);
                } else if (!addon) sc_detach_addon(mo);
                mo->core.position = fixed3_with_xy(mo->core.position, P_BuildingPosition(mo->type_id, cell));
                if (!addon) sc_reconnect_addon(mo, cell);
            }
            P_SyncBuildingBlocking();
            if (!clear) continue;
        } else if (!sc_place_near(mo, dest)) continue;
        moved++;
        if (limit && moved >= limit) break;
    }
    if (moved) P_UpdateSight();
}

/* CHK Order applies to every matching mobile unit, not a limited group. */
static void order_units(const sc_mission_t *m, const int *players, int nplayers,
                        unsigned unit, unsigned loc, unsigned destloc, unsigned order) {
    int l, t, r, b, dl, dt, dr, db;
    if (order > 2 || !order_loc_box(m, loc, &l, &t, &r, &b) ||
        !order_loc_box(m, destloc, &dl, &dt, &dr, &db) || !thinkercap.next) return;
    fixed2_t dest = order_loc_center(m, destloc);
    ticcmd_t cmd = {.order = order == 0 ? TC_MOVE : order == 1 ? TC_PATH : TC_ATTACK,
                    .position = fixed3_from_fixed2(dest, 0), .count = 1};
    if (order == 1) cmd.path = (waypoints_t){.count = 1, .mode = WP_BACKTRACK,
                                             .points = {fixed2_cell(dest)}};
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function != P_MobjThinker) continue;
        mobj_t *mo = (mobj_t *)th;
        bool emerging = (mo->sc.flags & SC_BURROWED) || mo->sc.order.kind == SC_ORDER_UNBURROW;
        if (mo->hp <= 0 || mo->remove || !kind_ok(mo, unit) ||
            mo->type_id == MT_INTERCEPTOR || mo->type_id == MT_SCARAB || mo->type_id == MT_NUCLEAR_MISSILE ||
            (mo->owner < 8 && (m->owners[mo->owner] == 3 || m->owners[mo->owner] == 4)) ||
            ((sc_unit(mo)->flags & SC_UNIT_BUILDING) &&
             (mo->sc.order.kind == SC_ORDER_LIFT || mo->sc.order.kind == SC_ORDER_LAND)) ||
            (!((mo->traits & MF_MOBILE) || (emerging && mo->info &&
                                           (mo->info->traits & MF_MOBILE)))) ||
            (mo->sc.flags & SC_LOADED) || !location_height_ok(m, mo, loc) || !inside(mo, l, t, r, b)) continue;
        bool mine = false;
        for (int i = 0; i < nplayers; i++) if (players[i] == mo->owner) mine = true;
        if (!mine) continue;
        if (emerging) {
            if (mo->sc.flags & SC_BURROWED) sc_unburrow(mo);
            mo->sc.queue = (sc_order_queue_t){.active = TC_SPELL, .owner = mo->owner, .count = 1,
                .pending = {{.order = cmd.order, .position = cmd.position,
                             .point = cmd.path.points[0], .mode = cmd.path.mode}}};
            continue;
        }
        cmd.units[0] = mo->id;
        G_RunTiccmd(mo->owner, &cmd);
    }
}

static void apply_property(mobj_t *mo, const sc_mission_t *m, unsigned index) {
    if (!m->uprp || !index || index > m->uprp_size / 20) return;
    const uint8_t *p = m->uprp + (index - 1) * 20;
    unsigned special = read_u16_le(p), data = read_u16_le(p + 2), state = read_u16_le(p + 14);
    if ((data & 1) && p[4] < 8) {
        mo->owner = mo->team = p[4];
        mo->allegiance = sc_allegiance_for(p[4]);
    }
    if (data & 2) sc_initial_hp(mo, p[5]);
    sc_initial_vitals(mo, data, p[6], p[7]);
    if (data & 0x20) sc_initial_hangar(mo, read_u16_le(p + 12));
    if ((special & 16) && (state & 16)) mo->sc.flags |= SC_INVINCIBLE;
    if ((special & 8) && (state & 8)) sc_initial_hallucination(mo);
}

static void create_units(sc_mission_t *m, const uint8_t *a, int current) {
    unsigned player = read_u32_le(a + 16), loc = read_u32_le(a);
    unsigned unit = read_u16_le(a + 24), number = read_u32_le(a + 20);
    int players[8], n = collect_players(m, player, current, players);
    if (!n || unit >= SC_TYPES) return;
    int l, t, r, b;
    if (!loc_box(m, loc ? loc : 64, &l, &t, &r, &b)) return;
    bool props = (a[28] & 8) != 0;
    int count = a[27];
    for (int i = 0; i < n; i++) for (int c = 0; c < count; c++) {
        int col = c % 8, row = c / 8;
        ivec2_t pixel = {(l + r) / 2 + col * 32, (t + b) / 2 + row * 32};
        if (pixel.x < 0) pixel.x = 0;
        if (pixel.y < 0) pixel.y = 0;
        bool resource = (unit + 1 >= MT_MINERAL_FIELD1 && unit + 1 <= MT_MINERAL_FIELD3) ||
                        unit + 1 == MT_VESPENE_GEYSER;
        bool refinery = unit + 1 == MT_REFINERY || unit + 1 == MT_EXTRACTOR || unit + 1 == MT_ASSIMILATOR;
        int amount = unit + 1 >= MT_MINERAL_FIELD1 && unit + 1 <= MT_MINERAL_FIELD3 ? 1500 : 5000;
        if (props && number && m->uprp && number <= m->uprp_size / 20) {
            const uint8_t *p = m->uprp + (number - 1) * 20;
            if (read_u16_le(p + 2) & 16) {
                unsigned value = read_u32_le(p + 8);
                amount = value > INT_MAX ? INT_MAX : (int)value;
            }
        }
        mobj_t *mo = sc_spawn_actor(unit, pixel,
                                    resource || sc_addon_parent(unit + 1) ? 11 : (uint8_t)players[i]);
        if (mo && (resource || refinery) && !sc_add_resource_vent(mo, amount)) {
            P_RemoveMobj(mo);
            continue;
        }
        if (mo && props) apply_property(mo, m, number);
        if (mo && (sc_unit(mo)->flags & SC_UNIT_BUILDING) && !sc_addon_parent(mo->type_id)) {
            irect_t bounds = P_MobjCells(mo);
            ivec2_t cell = {bounds.x, bounds.y};
            if (fixed2_equal(fixed3_xy(mo->core.position), P_BuildingPosition(mo->type_id, cell)))
                sc_reconnect_addon(mo, cell);
        }
    }
}

static void alter_resource(int player, int which, int mod, uint32_t amount) {
    for (int r = 0; r < 2; r++) {
        if (which == r || which == 2) {
            int *slot = &level.player_resources[player][r];
            int64_t value = *slot;
            if (mod == 7) value = amount;
            else if (mod == 8) value += amount;
            else if (mod == 9) value -= amount;
            *slot = value < 0 ? 0 : value > INT_MAX ? INT_MAX : (int)value;
        }
    }
}

static void set_alliance(int a, int b, int status) {
    if (a < 0 || b < 0 || a >= 8 || b >= 8 || a == b) return;
    sc_mission_t *m = mission(&level);
    if (!m) return;
    uint32_t bit = UINT32_C(0x40000000) >> b;
    if (status == 0) m->allies[a] &= ~bit;
    else m->allies[a] |= bit;
}

/* OpenBW give_unit_to grants each unit's native technologies and copies the
 * giver's higher unit-specific upgrade levels. Bits are DAT row numbers. */
#define SC_BIT(n) (UINT64_C(1) << (n))
static void give_abilities(const mobj_t *mo, uint8_t owner) {
    uint64_t upgrades = 0, techs = 0;
    switch (mo->type_id) {
    case MT_MARINE: upgrades = SC_BIT(16); techs = SC_BIT(SC_TECH_STIM_PACKS); break;
    case MT_FIREBAT: techs = SC_BIT(SC_TECH_STIM_PACKS); break;
    case MT_GHOST: upgrades = SC_BIT(20)|SC_BIT(21); techs = SC_BIT(SC_TECH_LOCKDOWN)|SC_BIT(SC_TECH_PERSONNEL_CLOAKING); break;
    case MT_VULTURE: upgrades = SC_BIT(17); techs = SC_BIT(SC_TECH_SPIDER_MINES); break;
    case MT_SIEGE_TANK: case MT_SIEGE_MODE: techs = SC_BIT(SC_TECH_SIEGE_MODE); break;
    case MT_WRAITH: upgrades = SC_BIT(22); techs = SC_BIT(SC_TECH_CLOAKING_FIELD); break;
    case MT_SCIENCE_VESSEL: upgrades = SC_BIT(19); techs = SC_BIT(SC_TECH_DEFENSIVE_MATRIX)|SC_BIT(SC_TECH_IRRADIATE)|SC_BIT(SC_TECH_EMP); break;
    case MT_BATTLECRUISER: upgrades = SC_BIT(23); techs = SC_BIT(SC_TECH_YAMATO_GUN); break;
    case MT_ZERGLING: upgrades = SC_BIT(27)|SC_BIT(28); techs = SC_BIT(SC_TECH_BURROWING); break;
    case MT_HYDRALISK: upgrades = SC_BIT(29)|SC_BIT(30); techs = SC_BIT(SC_TECH_BURROWING); break;
    case MT_DRONE: case MT_INFESTED_TERRAN: techs = SC_BIT(SC_TECH_BURROWING); break;
    case MT_OVERLORD: upgrades = SC_BIT(24)|SC_BIT(25)|SC_BIT(26); break;
    case MT_QUEEN: upgrades = SC_BIT(31); techs = SC_BIT(SC_TECH_INFESTATION)|SC_BIT(SC_TECH_PARASITE)|SC_BIT(SC_TECH_SPAWN_BROODLING)|SC_BIT(SC_TECH_ENSNARE); break;
    case MT_DEFILER: upgrades = SC_BIT(32); techs = SC_BIT(SC_TECH_BURROWING)|SC_BIT(SC_TECH_DARK_SWARM)|SC_BIT(SC_TECH_PLAGUE)|SC_BIT(SC_TECH_CONSUME); break;
    case MT_ZEALOT: upgrades = SC_BIT(34); break;
    case MT_DRAGOON: upgrades = SC_BIT(33); break;
    case MT_HIGH_TEMPLAR: upgrades = SC_BIT(40); techs = SC_BIT(SC_TECH_ARCHON_WARP)|SC_BIT(SC_TECH_PSIONIC_STORM)|SC_BIT(SC_TECH_HALLUCINATION); break;
    case MT_SHUTTLE: upgrades = SC_BIT(37); break;
    case MT_SCOUT: upgrades = SC_BIT(41)|SC_BIT(42); break;
    case MT_ARBITER: upgrades = SC_BIT(44); techs = SC_BIT(SC_TECH_RECALL)|SC_BIT(SC_TECH_STASIS_FIELD); break;
    case MT_CARRIER: upgrades = SC_BIT(43); break;
    case MT_REAVER: upgrades = SC_BIT(35)|SC_BIT(36); break;
    case MT_OBSERVER: upgrades = SC_BIT(38)|SC_BIT(39); break;
    default: break;
    }
    for (int i = 0; i < SC_UPGRADES; i++) if (upgrades & SC_BIT(i)) {
        int from = sc_upgrade_level(mo->owner, i);
        if (from > sc_upgrade_level(owner, i)) level.upgrades[i][owner].weapon = from;
    }
    for (int i = 0; i < SC_TECHS; i++) if (techs & SC_BIT(i))
        level.upgrades[SC_UPGRADES + i][owner].weapon = 1;
}
#undef SC_BIT

/* OpenBW trigger_give_unit_to transfers cargo, launched fighters and attached
 * structures along with their owner. This representation stores them as mobjs. */
static void give_one(mobj_t *mo, uint8_t owner) {
    if (!mo || mo->owner == owner) return;
    give_abilities(mo, owner);
    if (mo->production && !mo->production->placed &&
        (((sc_unit(mo)->flags & SC_UNIT_BUILDING) && !(mo->traits & MF_FLY)) ||
         sc_hangar_type(mo->type_id) == MT_INTERCEPTOR || sc_hangar_type(mo->type_id) == MT_SCARAB))
        sc_cancel_production(mo, -1);
    P_MobjSetSelected(mo, false);
    if (!(mo->sc.flags & SC_LOADED) && mo->type_id != MT_INTERCEPTOR && mo->type_id != MT_SCARAB) {
        P_ClearMove(mo);
        mo->movement.goal = fixed3_xy(mo->core.position);
        mo->movement.order_id = 0;
        mo->movement.order_arrived = true;
        mo->core.momentum = fixed3_zero();
        mo->move_only = false;
        mo->attack.target = NULL;
        mo->harvest.target = -1;
        mo->harvest.base = NULL;
        mo->harvest.phase = mo->harvest.timer_ms = 0;
        mo->sc.order = (sc_order_t){0};
        mo->sc.queue = (sc_order_queue_t){0};
        mo->sc.flags &= (uint8_t)~SC_HOLD;
        mo->waypoints = (waypoints_t){0};
    } else mo->sc.queue.owner = owner;
    mo->owner = mo->team = owner;
    mo->allegiance = sc_allegiance_for(owner);
    if (!(mo->sc.flags & SC_LOADED) && mo->type_id != MT_INTERCEPTOR && mo->type_id != MT_SCARAB &&
        !mo->production && gameinfo && mo->type_id < gameinfo->mobj_type_count) {
        mo->core.state_id = 0;
        P_InitMobj(gameinfo, mo); /* Spawn initialization does not execute an action. */
    }
}

static bool give_unit(mobj_t *mo, uint8_t owner) {
    if (!mo || mo->owner == owner || (mo->sc.flags & (SC_HALLUCINATION | SC_LOADED)) ||
        (mo->type_id >= MT_MINERAL_FIELD1 && mo->type_id <= MT_MINERAL_FIELD3) ||
        mo->type_id == MT_VESPENE_GEYSER || mo->type_id == MT_INTERCEPTOR ||
        mo->type_id == MT_SCARAB || sc_addon_parent(mo->type_id)) return false;
    uint32_t id = mo->id, addon = mo->sc.addon;
    uint32_t nydus = mo->type_id == MT_NYDUS_CANAL ? mo->sc.parent : 0;
    /* Our gas worker stays outside, but MINING on this vent corresponds to
     * OpenBW's hidden HarvestGas worker inside the refinery. */
    if ((!mo->production || !mo->production->placed) &&
        (mo->type_id == MT_REFINERY || mo->type_id == MT_EXTRACTOR || mo->type_id == MT_ASSIMILATOR)) {
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            mobj_t *worker = (mobj_t *)th;
            int vent = worker->harvest.target;
            if (th->function != P_MobjThinker || worker->remove || worker->hp <= 0 ||
                worker->owner != mo->owner || !(worker->traits & MF_HARVESTER) ||
                worker->harvest.phase != HARVEST_PHASE_MINING || vent < 0 || vent >= level.resource_vent_count ||
                level.resource_vents[vent].source_id != id) continue;
            if (level.resource_vents[vent].occupant == worker->id)
                level.resource_vents[vent].occupant = 0;
            give_one(worker, owner);
            break;
        }
    }
    give_one(mo, owner);
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *child = (mobj_t *)th;
        if (th->function != P_MobjThinker || child->remove || child->hp <= 0 || child == mo) continue;
        if (child->id == addon || child->id == nydus ||
            (child->sc.parent == id && ((child->sc.flags & SC_LOADED) ||
             child->type_id == MT_INTERCEPTOR || child->type_id == MT_SCARAB)))
            give_one(child, owner);
    }
    return true;
}

static void finish_trigger(sc_trig_t *st) {
    if (st->preserve) { st->cursor = 0; st->running = false; st->preserve = false; }
    else st->disabled = true;
}

/* OpenBW permits one outstanding Wait per trigger owner; other triggers
 * continue until they themselves reach a Wait. */
static bool player_waiting(const sc_mission_t *m, int owner) {
    for (int i = 0; i < m->trig_count; i++) {
        const sc_trig_t *other = &m->rt[i * 8 + owner];
        if (other->wait_ms <= 0 || other->cursor < 1 || other->cursor > 64) continue;
        const uint8_t *action = m->trig + (size_t)i * 2400 + 320 + (other->cursor - 1) * 32;
        if (action[26] == 4) return true;
    }
    return false;
}

static void run_actions(sc_mission_t *m, const uint8_t *trig, sc_trig_t *st, hudtext_t *hud) {
    while (st->cursor < 64 && !m->result) {
        const uint8_t *a = trig + 320 + st->cursor * 32;
        uint8_t id = a[26];
        if (!id) { finish_trigger(st); return; }
        st->cursor++;
        if (a[28] & 2) continue;
        unsigned player = read_u32_le(a + 16), number = read_u32_le(a + 20);
        unsigned loc = read_u32_le(a), unit = read_u16_le(a + 24), time = read_u32_le(a + 12);
        int players[8], n = collect_players(m, player, st->current, players);
        char text[256];
        /* Outcomes and messages are the local player's; another player's
         * copy of the trigger decides nothing on this screen. */
        bool local = st->current == consoleplayer;
        if (id == 1 || id == 2 || id == 56) {
            if (!local) continue;
            m->result = id == 1 ? 1 : id == 2 ? 2 : 3;
            G_EndLevel(id == 1 ? OUTCOME_VICTORY : id == 2 ? OUTCOME_DEFEAT : OUTCOME_DRAW);
            return;
        }
        if (!local && (id == 7 || id == 8 || id == 9 || id == 10 || id == 12 || id == 28)) continue;
        if (id == 3) { st->preserve = true; continue; }
        if (id == 4) {
            if (player_waiting(m, st->current)) { st->cursor--; return; }
            st->wait_ms = time > INT_MAX ? INT_MAX : time ? (int)time : 1;
            return;
        }
        if (id == 7 || id == 8) {
            char wav[512];
            str_text(m, read_u32_le(a + 8), wav, sizeof(wav));
            if (wav[0]) sc_brief_voice(level.map_path, wav);
            if (id == 8) continue;
        }
        if (id == 7 || id == 9 || id == 12) {
            str_text(m, read_u32_le(a + 4), text, sizeof(text));
            if (id == 12) {
                if (strcmp(m->objectives, text)) {
                    snprintf(m->objectives, sizeof(m->objectives), "%s", text);
                    say(hud, text, 12000);
                }
            } else say(hud, text, id == 7 ? (int)(time ? time : 5000) : 8000);
            if (id == 7 && time) { st->wait_ms = (int)time; return; }
            continue;
        }
        if (id == 10 || id == 28) {
            look_at(m, loc);
            if (id == 28) say(hud, "Ping", 2000);
            continue;
        }
        if (id == 11 || id == 44) { create_units(m, a, st->current); continue; }
        if (id == 13 && number < 256) {
            uint8_t bit = m->switches[number];
            if (a[27] == 4) bit = 1;
            else if (a[27] == 5) bit = 0;
            else if (a[27] == 6) bit = !bit;
            else if (a[27] == 11) bit = sc_random() & 1;
            m->switches[number] = bit;
            continue;
        }
        if (id == 14) {
            int64_t ms = (int64_t)time * 1000, value = m->countdown_ms;
            if (a[27] == 7) value = ms;
            else if (a[27] == 8) value += ms;
            else if (a[27] == 9) value -= ms;
            m->countdown_ms = value < 0 ? 0 : value > INT_MAX ? INT_MAX : (int)value;
            continue;
        }
        if (id == 15 || id == 16) {
            /* OpenBW +Vi and PyMS -Vi scripts change shared vision. */
            if (id == 15 && ((number & 0x00ffffffu) == 0x0069562bu ||
                             (number & 0x00ffffffu) == 0x0069562du) &&
                (number >> 24) >= '0' && (number >> 24) <= '7') {
                int source = (int)(number >> 24) - '0';
                uint32_t bit = UINT32_C(0x40000000) >> source;
                if ((number & 0xff) == '+') level.sight.allies[st->current] |= bit;
                else if (source != st->current) level.sight.allies[st->current] &= ~bit;
                P_UpdateSight();
                continue;
            }
            if (st->current >= 0 && st->current < 8) m->ai_on[st->current] = true;
            continue;
        }
        if (id == 26) {
            for (int i = 0; i < n; i++)
                alter_resource(players[i], (int)unit, a[27], number);
            continue;
        }
        if (id == 38 && loc && number) {
            mobj_t *found[1];
            int dest_players[8], dn = collect_players(m, player, st->current, dest_players);
            int src = (int)loc - 1, dst = (int)number - 1;
            if (src >= 0 && src < m->loc_count && dst >= 0 && dst < m->loc_count) {
                int dx = 0, dy = 0;
                if (gather(m, dest_players, dn, unit, loc, 1, found, 1)) {
                    fixed2_t at = fixed3_xy(found[0]->core.position);
                    dx = (at.x >> 11) - (m->loc_left[src] + m->loc_right[src]) / 2;
                    dy = (at.y >> 11) - (m->loc_top[src] + m->loc_bottom[src]) / 2;
                }
                int l = m->loc_left[dst] + dx, r = m->loc_right[dst] + dx;
                int t = m->loc_top[dst] + dy, b = m->loc_bottom[dst] + dy;
                if (l < 0) { r -= l; l = 0; }
                if (t < 0) { b -= t; t = 0; }
                if (r >= level.width * 32) { l += level.width * 32 - 1 - r; r = level.width * 32 - 1; }
                if (b >= level.height * 32) { t += level.height * 32 - 1 - b; b = level.height * 32 - 1; }
                m->loc_left[dst] = l; m->loc_right[dst] = r;
                m->loc_top[dst] = t; m->loc_bottom[dst] = b;
            }
            continue;
        }
        if (id == 41) {
            str_text(m, read_u32_le(a + 4), m->next_scenario, sizeof(m->next_scenario));
            continue;
        }
        if (id == 45 && unit < SC_TYPES) {
            for (int i = 0; i < n; i++) {
                uint32_t *row = &m->deaths[players[i]][unit];
                if (a[27] == 7) *row = number;
                else if (a[27] == 8) *row += number;
                else if (a[27] == 9) *row = *row > number ? *row - number : 0;
            }
            continue;
        }
        if (id == 54) { m->countdown_paused = true; continue; }
        if (id == 55) { m->countdown_paused = false; continue; }
        if (id == 57) {
            int self[8], ns = 0;
            if (st->current >= 0 && st->current < 8) self[ns++] = st->current;
            else ns = collect_players(m, (unsigned)st->current, st->current, self);
            int status = (int)unit;
            for (int s = 0; s < ns; s++) for (int i = 0; i < n; i++)
                set_alliance(self[s], players[i], status);
            continue;
        }
        if (id == 46) {
            order_units(m, players, n, unit, loc, number, a[27]);
            continue;
        }
        if (id == 52) {
            modify_deposits(m, player, st->current, loc, number, a[27]);
            continue;
        }
        if (id == 53) {
            modify_hangars(m, players, n, unit, loc, a[27], number);
            continue;
        }
        if (id == 49 || id == 50 || id == 51) {
            modify_vitals(m, players, n, unit, loc, a[27], number, id);
            continue;
        }
        if (id == 43 && thinkercap.next) {
            int l, t, r, b;
            bool anywhere = !loc || loc == 64;
            if (!anywhere && !loc_box(m, loc, &l, &t, &r, &b)) continue;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                mobj_t *mo = (mobj_t *)th;
                if (th->function != P_MobjThinker || mo->hp <= 0 || mo->remove || !kind_ok(mo, unit) ||
                    (!location_height_ok(m, mo, loc) || (!anywhere && !inside(mo, l, t, r, b)))) continue;
                for (int i = 0; i < n; i++) if (players[i] == mo->owner) {
                    if (a[27] == 4) mo->sc.flags |= SC_INVINCIBLE;
                    else if (a[27] == 5) mo->sc.flags &= (uint8_t)~SC_INVINCIBLE;
                    else if (a[27] == 6) mo->sc.flags ^= SC_INVINCIBLE;
                    break;
                }
            }
            continue;
        }
        if ((id == 22 || id == 23 || id == 24 || id == 25) && thinkercap.next) {
            int l, t, r, b;
            bool anywhere = (id != 23 && id != 25) || !loc || loc == 64;
            if (!anywhere && !loc_box(m, loc, &l, &t, &r, &b)) continue;
            unsigned removed = 0;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                mobj_t *mo = (mobj_t *)th;
                if (th->function != P_MobjThinker || mo->hp <= 0 || mo->remove || !kind_ok(mo, unit) ||
                    ((id == 23 || id == 25) && !location_height_ok(m, mo, loc)) ||
                    (!anywhere && !inside(mo, l, t, r, b))) continue;
                for (int i = 0; i < n; i++) if (players[i] == mo->owner) {
                    if (id == 22 || id == 23) P_DamageMobj(mo, NULL, mo->hp);
                    else P_RemoveMobj(mo);
                    removed++;
                    break;
                }
                if ((id == 23 || id == 25) && a[27] && removed >= a[27]) break;
            }
            continue;
        }
        if (id == 48 && (number < 8 || number == 13) && thinkercap.next) {
            int l, t, r, b;
            bool anywhere = !loc || loc == 64;
            if (!anywhere && !loc_box(m, loc, &l, &t, &r, &b)) continue;
            uint8_t owner = number == 13 ? (uint8_t)st->current : (uint8_t)number;
            unsigned given = 0;
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                mobj_t *mo = (mobj_t *)th;
                if (th->function != P_MobjThinker || mo->hp <= 0 || mo->remove || !kind_ok(mo, unit) ||
                    (!location_height_ok(m, mo, loc) || (!anywhere && !inside(mo, l, t, r, b)))) continue;
                for (int i = 0; i < n; i++) if (players[i] == mo->owner) {
                    if (give_unit(mo, owner)) given++;
                    break;
                }
                if (a[27] && given >= a[27]) break;
            }
            continue;
        }
        if (id == 39) {
            move_units(m, players, n, unit, loc, number, a[27]);
            continue;
        }
    }
    if (!m->result) finish_trigger(st);
}

/* Computer and human slots take part in elimination (not rescuable or
 * neutral ones). A map that declares its own Victory action decides both
 * outcomes through its triggers; a melee map without one is eliminated. */
bool G_PlayerSide(int owner) {
    sc_mission_t *m = mission(&level);
    if (!m || owner < 0 || owner >= 8) return false;
    uint8_t kind = m->owners[owner];
    return kind == 1 || kind == 2 || kind == 5 || kind == 6;
}

bool G_UnitHoldsSide(const mobj_t *unit) {
    return unit->thinker.function == P_MobjThinker && unit->type_id && unit->type_id <= SC_TYPES;
}

void G_ShowOutcome(outcome_t outcome) {
    sc_mission_t *m = mission(&level);
    int result = outcome == OUTCOME_VICTORY ? 1 : outcome == OUTCOME_DEFEAT ? 2 : 3;
    if (m) m->result = result;
    sc_show_result(result);
}

unsigned G_LevelObjectives(mobj_t *const *units, int count) {
    (void)units; (void)count;
    sc_mission_t *m = mission(&level);
    if (!m || m->result) return 0;
    return m->has_victory ? 0 : ELIMINATE_ALL;
}

void sc_mission_tick(level_t *map, hudtext_t *hud, int dt_ms) {
    sc_mission_t *m = mission(map);
    if (!m || m->result) return;
    int ms = dt_ms < 1 ? 1 : dt_ms;
    m->elapsed_ms += ms;
    /* Score: a rise in stock is income, a fall is spending. */
    for (int p = 0; p < 8; p++) for (int r = 0; r < 2; r++) {
        int now = map->player_resources[p][r], delta = now - m->stock[p][r];
        if (m->stock_seen && delta > 0) {
            int64_t total = (int64_t)m->gathered[p][r] + delta;
            m->gathered[p][r] = total > INT_MAX ? INT_MAX : (int)total;
        } else if (m->stock_seen && delta < 0) {
            int64_t total = (int64_t)m->spent[p] - delta;
            m->spent[p] = total > INT_MAX ? INT_MAX : (int)total;
        }
        m->stock[p][r] = now;
    }
    m->stock_seen = true;
    /* A melee map can omit TRIG. Campaign maps carry a Victory action, so
     * elimination applies only when the map never declares its own winner. */
    if (m->trig) {
        if (!m->countdown_paused && m->countdown_ms > 0) {
            m->countdown_ms -= ms;
            if (m->countdown_ms < 0) m->countdown_ms = 0;
        }
        /* All player wait timers advance before any trigger is revisited. */
        for (int i = 0; i < m->trig_count * 8; i++) {
            sc_trig_t *st = &m->rt[i];
            if (st->wait_ms > 0) {
                st->wait_ms -= ms;
                if (st->wait_ms <= 0) st->wait_ms = -1;
            }
        }
        for (int i = 0; i < m->trig_count && !m->result; i++) {
            const uint8_t *trig = m->trig + (size_t)i * 2400, *mask = trig + 2372;
            for (int p = 0; p < 8 && !m->result; p++) {
                if (!owns_trigger(m, mask, p)) continue;
                sc_trig_t *st = &m->rt[i * 8 + p];
                if (st->disabled) continue;
                st->current = p;
                if (st->wait_ms) {
                    if (st->wait_ms > 0) continue;
                    const uint8_t *last = st->cursor ? trig + 320 + (st->cursor - 1) * 32 : NULL;
                    if (last && last[26] == 4 && player_waiting(m, p)) continue;
                    st->wait_ms = 0;
                    run_actions(m, trig, st, hud);
                    continue;
                }
                if (st->running) { run_actions(m, trig, st, hud); continue; }
                if (!conditions_ok(m, trig, p)) continue;
                st->running = true;
                st->cursor = 0;
                run_actions(m, trig, st, hud);
            }
        }
    }
}

void sc_note_damage(mobj_t *mo) {
    if (!mo) return;
    if (mo->hp > 0) return;
    sc_mission_t *m = mission(&level);
    if (!m || mo->owner >= 8 || !mo->type_id || mo->type_id > SC_TYPES) return;
    unsigned type = mo->type_id - 1;
    if (m->deaths[mo->owner][type] < UINT32_MAX) m->deaths[mo->owner][type]++;
    if ((mo->sc.flags & SC_HIT) && mo->sc.attacker < 8 && m->kills[mo->sc.attacker][type] < 65535)
        m->kills[mo->sc.attacker][type]++;
}

bool sc_mission_bind(level_t *map) {
    sc_mission_t *m = mission(map);
    if (!m) return false;
    const uint8_t *bytes = m->file.bytes;
    size_t size = m->file.size;
    for (size_t at = 0; at + 8 <= size;) {
        const uint8_t *tag = bytes + at, *data = tag + 8;
        size_t n = read_u32_le(tag + 4);
        if (n > size - at - 8) return false;
        if (!memcmp(tag, "TRIG", 4)) { m->trig = data; m->trig_size = n; }
        else if (!memcmp(tag, "STR ", 4)) { m->str = data; m->str_size = n; }
        else if (!memcmp(tag, "UPRP", 4)) { m->uprp = data; m->uprp_size = n; }
        else if (!memcmp(tag, "OWNR", 4) && n >= 12) memcpy(m->owners, data, 12);
        else if (!memcmp(tag, "SIDE", 4) && n >= 12) memcpy(m->side, data, 12);
        else if (!memcmp(tag, "FORC", 4) && n >= 20) {
            memcpy(m->force, data, 8);
            memcpy(m->force_flags, data + 16, 4);
        } else if (!memcmp(tag, "MRGN", 4)) {
            m->loc_count = (int)(n / 20);
            if (m->loc_count > 64) m->loc_count = 64;
            for (int i = 0; i < m->loc_count; i++) {
                const uint8_t *loc = data + (size_t)i * 20;
                m->loc_left[i] = (int)read_u32_le(loc);
                m->loc_top[i] = (int)read_u32_le(loc + 4);
                m->loc_right[i] = (int)read_u32_le(loc + 8);
                m->loc_bottom[i] = (int)read_u32_le(loc + 12);
                m->loc_elevation[i] = read_u16_le(loc + 18);
            }
        }
        at += 8 + n;
    }
    if (m->trig_size) {
        if (m->trig_size % 2400) return false;
        m->trig_count = (int)(m->trig_size / 2400);
        m->rt = calloc((size_t)m->trig_count * 8, sizeof(*m->rt));
        if (!m->rt) return false;
        for (int i = 0; i < m->trig_count; i++) {
            const uint8_t *trig = m->trig + (size_t)i * 2400;
            for (int a = 0; a < 64 && !m->has_victory; a++)
                if (trig[320 + a * 32 + 26] == 1) m->has_victory = true;
        }
    }
    map->player_teams = true;
    for (int a = 0; a < 8; a++) {
        uint32_t mask = UINT32_C(0x40000000) >> a, vision = mask;
        int force = m->force[a];
        bool allied = force < 4 && (m->force_flags[force] & 2);
        bool shared = force < 4 && (m->force_flags[force] & 16);
        for (int b = 0; b < 8; b++) {
            if (allied && m->force[b] == force) mask |= UINT32_C(0x40000000) >> b;
            if (shared && m->force[b] == force) vision |= UINT32_C(0x40000000) >> b;
        }
        m->allies[a] = mask;
        map->sight.allies[a] = vision;
    }
    m->rng = 1;
    return true;
}

int sc_elapsed_ms(void) {
    sc_mission_t *m = mission(&level);
    return m ? m->elapsed_ms : 0;
}

void sc_player_stats(int owner, sc_stats_t *out) {
    memset(out, 0, sizeof(*out));
    sc_mission_t *m = mission(&level);
    if (!m || owner < 0 || owner >= 8) return;
    for (int t = 0; t < SC_TYPES; t++) {
        out->lost[t] = m->deaths[owner][t] > 65535 ? 65535 : (int)m->deaths[owner][t];
        out->killed[t] = m->kills[owner][t];
    }
    out->gathered[0] = m->gathered[owner][0];
    out->gathered[1] = m->gathered[owner][1];
    out->spent = m->spent[owner];
}
