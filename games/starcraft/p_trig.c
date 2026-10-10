#include "sc_local.h"
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
        (level.sight.allies[self] & (UINT32_C(0x40000000) >> other));
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
    if (!*l && !*t && !*r && !*b) { *r = level.width * 32; *b = level.height * 32; }
    return true;
}

static bool inside(const mobj_t *mo, int l, int t, int r, int b) {
    fixed2_t at = fixed3_xy(mo->core.position);
    int x = at.x >> 11, y = at.y >> 11; /* 32 map pixels per cell */
    return x >= l && x <= r && y >= t && y <= b;
}

static bool kind_ok(const mobj_t *mo, unsigned id) {
    if (!mo->type_id || mo->type_id > SC_TYPES) return false;
    unsigned type = mo->type_id - 1;
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

static int count_units(const int *players, int nplayers, unsigned unit, unsigned loc,
                       const sc_mission_t *m) {
    int l, t, r, b;
    bool anywhere = !loc || loc == 64;
    if (!anywhere && !loc_box(m, loc, &l, &t, &r, &b)) return 0;
    int n = 0;
    if (!thinkercap.next) return 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function != P_MobjThinker || !alive_unit(mo) || !kind_ok(mo, unit)) continue;
        bool mine = false;
        for (int i = 0; i < nplayers; i++) if (players[i] == mo->owner) mine = true;
        if (!mine || (!anywhere && !inside(mo, l, t, r, b))) continue;
        n++;
    }
    return n;
}

static bool cmp_num(uint8_t op, int value, int qty) {
    if (op == 0) return value >= qty;
    if (op == 1) return value <= qty;
    if (op == 10) return value == qty;
    return false;
}

static bool owns_any(int owner) {
    if (!thinkercap.next) return false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        const mobj_t *mo = (const mobj_t *)th;
        if (th->function == P_MobjThinker && mo->owner == owner && mo->hp > 0 && !mo->remove &&
            mo->type_id && mo->type_id <= SC_TYPES) return true;
    }
    return false;
}

static bool condition_ok(sc_mission_t *m, const uint8_t *c, int current) {
    uint8_t type = c[15], op = c[14];
    unsigned group = read_u32_le(c + 4), qty = read_u32_le(c + 8);
    unsigned unit = read_u16_le(c + 12), loc = read_u32_le(c);
    int players[8], n = collect_players(m, group, current, players);
    if (type == 22) return true;
    if (type == 23) return false;
    if (type == 1) return cmp_num(op, m->countdown_ms / 1000, (int)qty);
    if (type == 12) return cmp_num(op, m->elapsed_ms / 1000, (int)qty);
    if (type == 11) {
        unsigned id = c[16];
        if (id > 255) return false;
        if (op == 2) return m->switches[id];
        if (op == 3) return !m->switches[id];
        return false;
    }
    if (type == 2 || type == 3) {
        unsigned where = type == 3 ? loc : 64;
        return cmp_num(op, count_units(players, n, unit, where, m), (int)qty);
    }
    if (type == 4) {
        int total = 0;
        for (int i = 0; i < n; i++) {
            if (c[16] == 0 || c[16] == 2) total += level.player_resources[players[i]][0];
            if (c[16] == 1 || c[16] == 2) total += level.player_resources[players[i]][1];
        }
        return cmp_num(op, total, (int)qty);
    }
    if (type == 5 || type == 15) {
        int total = 0;
        for (int i = 0; i < n; i++) {
            const uint16_t *row = type == 15 ? m->deaths[players[i]] : m->kills[players[i]];
            if (unit < SC_TYPES) total += row[unit];
            else if (unit == 229) for (int t = 0; t < SC_TYPES; t++) total += row[t];
        }
        return cmp_num(op, total, (int)qty);
    }
    if (type == 6 || type == 7 || type == 16 || type == 17) {
        unsigned where = (type == 7 || type == 17) ? loc : 64;
        int mine = count_units(players, n, unit, where, m), others = 0;
        bool most = type == 6 || type == 7;
        for (int p = 0; p < 8; p++) {
            bool in = false;
            for (int i = 0; i < n; i++) if (players[i] == p) in = true;
            if (in || !slot_active(m, p)) continue;
            int one = p;
            int count = count_units(&one, 1, unit, where, m);
            others++;
            if (most ? count >= mine : count <= mine) return false;
        }
        return others > 0 || mine > 0;
    }
    if (type == 14) {
        int foes = 0;
        for (int p = 0; p < 8; p++) {
            bool self = false;
            for (int i = 0; i < n; i++) if (players[i] == p) self = true;
            if (!self && in_group(m, 14, p, n == 1 ? players[0] : current) && owns_any(p)) foes++;
        }
        return cmp_num(op, foes, (int)qty);
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
        if (th->function != P_MobjThinker || !alive_unit(mo) || !kind_ok(mo, unit)) continue;
        bool mine = false;
        for (int i = 0; i < nplayers; i++) if (players[i] == mo->owner) mine = true;
        if (!mine || (!anywhere && !inside(mo, l, t, r, b))) continue;
        out[n++] = mo;
        if (limit && n >= limit) break;
    }
    return n;
}

static void apply_property(mobj_t *mo, const sc_mission_t *m, unsigned index) {
    if (!m->uprp || index >= m->uprp_size / 20) return;
    const uint8_t *p = m->uprp + index * 20;
    unsigned special = read_u16_le(p), data = read_u16_le(p + 2), state = read_u16_le(p + 14);
    if ((data & 1) && p[4] < 8) {
        mo->owner = mo->team = p[4];
        mo->allegiance = sc_allegiance_for(p[4]);
    }
    if (data & 2) mo->hp = mo->max_hp * p[5] / 100;
    if ((special & 16) && (state & 16)) mo->sc.flags |= SC_INVINCIBLE;
    if ((special & 8) && (state & 8)) mo->sc.flags |= SC_HALLUCINATION;
    if (mo->hp < 1 && !(mo->sc.flags & SC_INVINCIBLE)) mo->hp = 1;
    mo->sc.guard_hp = mo->hp;
}

static void create_units(sc_mission_t *m, const uint8_t *a, int current) {
    unsigned player = read_u32_le(a + 16), loc = read_u32_le(a);
    unsigned unit = read_u16_le(a + 24), number = read_u32_le(a + 20);
    int players[8], n = collect_players(m, player, current, players);
    if (!n || unit >= SC_TYPES) return;
    int l, t, r, b;
    if (!loc_box(m, loc ? loc : 64, &l, &t, &r, &b)) return;
    bool props = (a[28] & 8) != 0;
    int count = props ? 1 : (number ? (int)number : 1);
    if (count > 64) count = 64;
    for (int i = 0; i < n; i++) for (int c = 0; c < count; c++) {
        int col = c % 8, row = c / 8;
        ivec2_t pixel = {(l + r) / 2 + col * 32, (t + b) / 2 + row * 32};
        if (pixel.x < 0) pixel.x = 0;
        if (pixel.y < 0) pixel.y = 0;
        mobj_t *mo = sc_spawn_actor(unit, pixel, (uint8_t)players[i]);
        if (mo && props) apply_property(mo, m, number);
    }
}

static void alter_resource(int player, int which, int mod, int amount) {
    for (int r = 0; r < 2; r++) {
        if (which == r || which == 2) {
            int *slot = &level.player_resources[player][r];
            if (mod == 7) *slot = amount;
            else if (mod == 8) *slot += amount;
            else if (mod == 9) *slot -= amount;
            if (*slot < 0) *slot = 0;
        }
    }
}

static void set_alliance(int a, int b, int status) {
    if (a < 0 || b < 0 || a >= 8 || b >= 8 || a == b) return;
    uint32_t ab = UINT32_C(0x40000000) >> b, ba = UINT32_C(0x40000000) >> a;
    if (status == 0) {
        level.sight.allies[a] &= ~ab;
        level.sight.allies[b] &= ~ba;
    } else {
        level.sight.allies[a] |= ab;
        level.sight.allies[b] |= ba;
    }
}

static void finish_trigger(sc_trig_t *st) {
    if (st->preserve) { st->cursor = 0; st->running = false; st->preserve = false; }
    else st->disabled = true;
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
        if (id == 1 || id == 2 || id == 58) {
            if (!local) continue;
            m->result = id == 1 ? 1 : id == 2 ? 2 : 3;
            sc_show_result(m->result);
            return;
        }
        if (!local && (id == 7 || id == 9 || id == 10 || id == 12 || id == 28)) continue;
        if (id == 3) { st->preserve = true; continue; }
        if (id == 4) { st->wait_ms = (int)time; return; }
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
            else if (a[27] == 11) { m->rng = m->rng * 1664525u + 1013904223u; bit = (m->rng >> 16) & 1; }
            m->switches[number] = bit;
            continue;
        }
        if (id == 14) {
            int ms = (int)time * 1000, mod = a[27];
            if (mod == 7) m->countdown_ms = ms;
            else if (mod == 8) m->countdown_ms += ms;
            else if (mod == 9) m->countdown_ms -= ms;
            if (m->countdown_ms < 0) m->countdown_ms = 0;
            continue;
        }
        if (id == 15 || id == 16) {
            for (int i = 0; i < n; i++) m->ai_on[players[i]] = true;
            continue;
        }
        if (id == 26) {
            for (int i = 0; i < n; i++)
                alter_resource(players[i], (int)unit, a[27], (int)number);
            continue;
        }
        if (id == 38 && loc && number) {
            mobj_t *found[1];
            int dest_players[8], dn = collect_players(m, player, st->current, dest_players);
            if (gather(m, dest_players, dn, unit, number, 1, found, 1)) {
                fixed2_t at = fixed3_xy(found[0]->core.position);
                int i = (int)loc - 1;
                if (i >= 0 && i < m->loc_count) {
                    int w = m->loc_right[i] - m->loc_left[i], h = m->loc_bottom[i] - m->loc_top[i];
                    int cx = at.x >> 11, cy = at.y >> 11;
                    m->loc_left[i] = cx - w / 2; m->loc_right[i] = m->loc_left[i] + w;
                    m->loc_top[i] = cy - h / 2; m->loc_bottom[i] = m->loc_top[i] + h;
                }
            }
            continue;
        }
        if (id == 41) {
            str_text(m, read_u32_le(a + 4), m->next_scenario, sizeof(m->next_scenario));
            continue;
        }
        if (id == 45 && unit < SC_TYPES) {
            for (int i = 0; i < n; i++) {
                uint16_t *row = &m->deaths[players[i]][unit];
                unsigned value = number > 65535 ? 65535 : number;
                if (a[27] == 7) *row = (uint16_t)value;
                else if (a[27] == 8) {
                    unsigned sum = *row + value;
                    *row = (uint16_t)(sum > 65535 ? 65535 : sum);
                } else if (a[27] == 9) *row = *row > value ? (uint16_t)(*row - value) : 0;
            }
            continue;
        }
        if (id == 55) { m->countdown_paused = true; continue; }
        if (id == 56) { m->countdown_paused = false; continue; }
        if (id == 57) {
            int self[8], ns = 0;
            if (st->current >= 0 && st->current < 8) self[ns++] = st->current;
            else ns = collect_players(m, (unsigned)st->current, st->current, self);
            int status = (int)unit;
            for (int s = 0; s < ns; s++) for (int i = 0; i < n; i++)
                set_alliance(self[s], players[i], status);
            continue;
        }
        if (id == 22 || id == 23 || id == 24 || id == 25 || id == 39 || id == 43 ||
            id == 47 || id == 48 || id == 49) {
            unsigned where = (id == 22 || id == 24 || id == 43 || id == 47) ? (id == 43 || id == 47 ? loc : 64) : loc;
            int limit = (id == 23 || id == 25 || id == 39 || id == 48 || id == 49) ? a[27] : 0;
            mobj_t *found[256];
            int count = gather(m, players, n, unit, where, limit, found, 256);
            fixed2_t dest = loc_center(m, number ? number : loc);
            for (int i = 0; i < count; i++) {
                mobj_t *mo = found[i];
                if (id == 22 || id == 23) P_DamageMobj(mo, NULL, mo->hp > 0 ? mo->hp : 1);
                else if (id == 24 || id == 25) P_RemoveMobj(mo);
                else if (id == 39 || id == 47) {
                    mobj_t *enemy = NULL;
                    if (id == 47 && a[27] == 2 && thinkercap.next) {
                        int l, t, r, b;
                        if (loc_box(m, number, &l, &t, &r, &b))
                            for (thinker_t *th = thinkercap.next; th != &thinkercap && !enemy; th = th->next) {
                                mobj_t *other = (mobj_t *)th;
                                if (th->function == P_MobjThinker && alive_unit(other) &&
                                    inside(other, l, t, r, b) && !P_IsAlly(mo, other)) enemy = other;
                            }
                    }
                    P_MoveUnitTo(&level, mo, dest);
                    if (enemy) mo->attack.target = enemy;
                } else if (id == 43) {
                    if (a[27] == 4) mo->sc.flags |= SC_INVINCIBLE;
                    else if (a[27] == 5) mo->sc.flags &= (uint8_t)~SC_INVINCIBLE;
                    else if (a[27] == 6) mo->sc.flags ^= SC_INVINCIBLE;
                    mo->sc.guard_hp = mo->hp;
                } else if (id == 48 && number < 8) {
                    mo->owner = mo->team = (uint8_t)number;
                    mo->allegiance = sc_allegiance_for((uint8_t)number);
                } else if (id == 49) {
                    unsigned pct = read_u32_le(a + 8);
                    if (!pct) P_DamageMobj(mo, NULL, mo->hp > 0 ? mo->hp : 1);
                    else { mo->hp = mo->max_hp * (int)pct / 100; if (mo->hp < 1) mo->hp = 1; mo->sc.guard_hp = mo->hp; }
                }
            }
            continue;
        }
    }
    if (!m->result) finish_trigger(st);
}

static bool opponent_slot(const sc_mission_t *m, int p) {
    if (p < 0 || p >= 8 || p == consoleplayer) return false;
    uint8_t kind = m->owners[p];
    if (kind != 1 && kind != 2 && kind != 5 && kind != 6) return false;
    return consoleplayer < 0 || consoleplayer >= 8 || !allied_to(consoleplayer, p);
}

static void check_elimination(sc_mission_t *m) {
    if (m->has_victory || m->result || consoleplayer < 0 || consoleplayer >= 8) return;
    if (!owns_any(consoleplayer)) { m->result = 2; sc_show_result(2); return; }
    int slots = 0, living = 0;
    for (int p = 0; p < 8; p++) if (opponent_slot(m, p)) {
        slots++;
        living += owns_any(p);
    }
    if (slots && !living) { m->result = 1; sc_show_result(1); }
}

void sc_mission_tick(level_t *map, hudtext_t *hud, int dt_ms) {
    sc_mission_t *m = mission(map);
    if (!m || m->result) return;
    int ms = dt_ms < 1 ? 1 : dt_ms;
    m->elapsed_ms += ms;
    /* Score: a rise in stock is income, a fall is spending. */
    for (int p = 0; p < 8; p++) for (int r = 0; r < 2; r++) {
        int now = map->player_resources[p][r], delta = now - m->stock[p][r];
        if (m->stock_seen && delta > 0) m->gathered[p][r] += delta;
        else if (m->stock_seen && delta < 0) m->spent[p] -= delta;
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
        for (int i = 0; i < m->trig_count && !m->result; i++) {
            const uint8_t *trig = m->trig + (size_t)i * 2400, *mask = trig + 2372;
            for (int p = 0; p < 8 && !m->result; p++) {
                if (!owns_trigger(m, mask, p)) continue;
                sc_trig_t *st = &m->rt[i * 8 + p];
                if (st->disabled) continue;
                st->current = p;
                if (st->wait_ms > 0) {
                    st->wait_ms -= ms;
                    if (st->wait_ms > 0) continue;
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
    if (!m->result) check_elimination(m);
    if (thinkercap.next) for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *mo = (mobj_t *)th;
        if (th->function == P_MobjThinker && mo->hp > 0 && !mo->remove) mo->sc.guard_hp = mo->hp;
    }
}

void sc_note_damage(mobj_t *mo) {
    if (!mo) return;
    if (mo->sc.flags & SC_INVINCIBLE) {
        if (mo->sc.guard_hp > 0) mo->hp = mo->sc.guard_hp;
        else if (mo->hp < 1) mo->hp = 1;
        return;
    }
    if (mo->hp > 0) return;
    sc_mission_t *m = mission(&level);
    if (!m || mo->owner >= 8 || !mo->type_id || mo->type_id > SC_TYPES) return;
    unsigned type = mo->type_id - 1;
    if (m->deaths[mo->owner][type] < 65535) m->deaths[mo->owner][type]++;
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
        uint32_t mask = UINT32_C(0x40000000) >> a;
        int force = m->force[a];
        bool allied = force < 4 && (m->force_flags[force] & 2);
        for (int b = 0; b < 8; b++)
            if (a == b || (allied && m->force[b] == force)) mask |= UINT32_C(0x40000000) >> b;
        map->sight.allies[a] = mask;
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
    for (int t = 0; t < SC_TYPES; t++) { out->lost[t] = m->deaths[owner][t]; out->killed[t] = m->kills[owner][t]; }
    out->gathered[0] = m->gathered[owner][0];
    out->gathered[1] = m->gathered[owner][1];
    out->spent = m->spent[owner];
}
