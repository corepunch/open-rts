#include "engine.h"
#include "info.h"
#include "rustfront.h"

/* gameinfo_t.draw_overlays: the marks over a unit after every sprite is drawn. It replaces the
 * engine's, so it draws the selection box and health bar too, then Rustfront's own: rank
 * chevrons, a wrench over a structure being repaired, and the selected producer's rally flag. */

static uint8_t health_tint(const mobj_t *u) {
    if (u->hp * 3 <= u->max_hp) return V_NearestIndex(0xffff4c36u);
    if (u->hp * 3 <= u->max_hp * 2) return V_NearestIndex(0xffffda3eu);
    return V_NearestIndex(0xff53f55cu);
}

/* One chevron: a small gold V, point down, 7 x 4. */
static void chevron(int x, int y, uint8_t ink, uint8_t edge) {
    for (int i = 0; i < 4; ++i) {
        V_FillRect((irect_t){x + i - 1, y + i - 1, 3, 3}, edge);
        V_FillRect((irect_t){x + 6 - i - 1, y + i - 1, 3, 3}, edge);
    }
    for (int i = 0; i < 4; ++i) {
        V_FillRect((irect_t){x + i, y + i, 1, 1}, ink);
        V_FillRect((irect_t){x + 6 - i, y + i, 1, 1}, ink);
    }
}

static void rally_flag(const app_t *app, const mobj_t *producer) {
    float sx, sy, tx, ty;
    R_MapPositionToScreen(app, &level, producer->core.position, &sx, &sy);
    R_MapPositionToScreen(app, &level, fixed3_from_fixed2(RF_Rally(producer), 0), &tx, &ty);
    int px = (int)sx, py = (int)sy, fx = (int)tx, fy = (int)ty;
    uint8_t line = V_NearestIndex(0xff8fd0ffu), pole = V_NearestIndex(0xffdce6dcu);
    V_DrawLine((ivec2_t){px, py}, (ivec2_t){fx, fy}, line);
    V_FillRect((irect_t){fx, fy - 14, 1, 14}, pole);
    V_FillRect((irect_t){fx + 1, fy - 14, 7, 5}, V_NearestIndex(0xff3c78ebu));
}

static void wrench(irect_t box) {
    int x = box.x + box.w / 2 - 4, y = box.y + box.h / 2 - 4;
    uint8_t ink = V_NearestIndex(0xffebc328u), edge = V_NearestIndex(0xff141412u);
    V_FillRect((irect_t){x - 1, y - 1, 10, 10}, edge);
    V_FillRect((irect_t){x, y, 8, 8}, V_NearestIndex(0xff2d3438u));
    for (int i = 1; i < 7; ++i) V_FillRect((irect_t){x + i, y + 7 - i, 1, 1}, ink);
    V_FillRect((irect_t){x + 5, y + 1, 2, 2}, ink);
}

void RF_DrawMarks(const unitoverlaycontext_t *ctx) {
    const mobj_t *u = ctx->unit;
    irect_t box = ctx->bounds;
    if (!u || u->hp <= 0 || u->type_id >= MT_FX_HIT) return;
    bool selected = P_MobjIsSelected(u) && (u->traits & MF_SELECTABLE);
    if (selected) V_DrawRectOutline(box, V_NearestIndex(0xff53f55cu));
    int by = box.y - 6;
    if (u->max_hp > 0 && (selected || u->hp < u->max_hp)) {
        int hp = u->hp < u->max_hp ? u->hp : u->max_hp;
        V_FillRect((irect_t){box.x - 1, by - 1, box.w + 2, 4}, V_NearestIndex(0xff141412u));
        V_FillRect((irect_t){box.x, by, (int)((int64_t)box.w * hp / u->max_hp), 2}, health_tint(u));
    }
    int rank = RF_Rank(u);
    for (int i = 0; i < rank; ++i)
        chevron(box.x + box.w - 8, box.y + 2 + i * 4, V_NearestIndex(0xffe6d750u), V_NearestIndex(0xff141412u));
    if (u->rf_repairing && (leveltime / (RTS_TICRATE / 2)) % 2 == 0) wrench(box);
    if (selected && u->owner == consoleplayer && RF_HasRally(u)) rally_flag(ctx->app, u);
}
