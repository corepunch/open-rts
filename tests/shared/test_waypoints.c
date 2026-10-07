#include "engine.h"
#include "info.h"
#include "t_local.h"

#define CHECK(c) RTS_CHECK(c, "shared waypoints", #c)

static int follow(mobj_t *actor, waypointmode_t mode) {
    waypoints_t path = {.points = {{5,5},{8,5},{8,8}}, .count = 3, .mode = mode};
    actor->core.position = fixed3_from_fvec2((fvec2_t){5.5f,5.5f}, 0);
    CHECK(G_PathOrder(&actor, 1, &path));
    bool second = false, last = false, returned = false;
    for (int tic = 0; tic < 1200; ++tic) {
        P_Ticker();
        CHECK(!actor->remove && actor->hp > 0);
        if (actor->waypoints.current == 1) second = true;
        if (actor->waypoints.current == 2) last = true;
        if (last && actor->waypoints.current == (mode == WP_BACKTRACK ? 1 : 0)) returned = true;
        if (mode == WP_ONCE && !actor->waypoints.count) break;
        if (mode != WP_ONCE && returned) break;
    }
    CHECK(second && last);
    if (mode == WP_ONCE) {
        CHECK(!actor->waypoints.count && !P_HasMoveOrder(actor));
        CHECK(ivec2_equal(fvec2_cell(fixed3_xy_to_fvec2(actor->core.position)), path.points[2]));
    } else CHECK(returned && actor->waypoints.count == 3);
    return 0;
}

int main(void) {
    G_InitGame(); P_InitThinkers();
    level = (level_t){.width = 32, .height = 32};
    level.blocked = calloc(32*32,1);
    CHECK(level.blocked);
    int type = -1;
    for (int i = 0; i < num_actor_types; ++i)
        if ((actor_types[i].traits & (MF_MOBILE | MF_FLY)) == MF_MOBILE &&
            mobjinfo[actor_types[i].id].seestate) { type = actor_types[i].id; break; }
    CHECK(type >= 0);
    mobj_t *actor = P_SpawnMobj(fixed3_from_fvec2((fvec2_t){5.5f,5.5f},0),type);
    CHECK(actor);
    actor->traits = MF_MOBILE | MF_SELECTABLE | MF_RENDERABLE;
    actor->speed = 8;
    P_MobjSetSelected(actor,true);
    consoleplayer = 0;
    CHECK(follow(actor,WP_ONCE) == 0);
    actor->traits |= MF_FLY;
    for (int x = 0; x < level.width; ++x) level.blocked[L_Index(&level, x, 7)] = 1;
    CHECK(follow(actor,WP_ONCE) == 0); /* Flying routes cross an impassable row. */
    memset(level.blocked,0,(size_t)level.width*level.height);
    actor->traits &= ~MF_FLY;
    CHECK(follow(actor,WP_LOOP) == 0);
    CHECK(follow(actor,WP_BACKTRACK) == 0);
    uint32_t checksum = G_Consistency();
    actor->waypoints.mode = WP_ONCE;
    CHECK(checksum != G_Consistency());
    checksum = G_Consistency();
    actor->waypoints.points[1] = (ivec2_t){9,9};
    CHECK(checksum != G_Consistency());
    ticcmd_t stop = {.order = TC_STOP, .count = 1, .units = {actor->id}};
    G_RunTiccmd(1,&stop);
    CHECK(actor->waypoints.count == 3);
    G_RunTiccmd(0,&stop);
    CHECK(!actor->waypoints.count && !P_HasMoveOrder(actor));

    waypoints_t path = {.points = {{6,5},{9,5}},.count = 2,.mode = WP_ONCE};
    netactive = true;
    CHECK(G_PathOrder(&actor,1,&path));
    CHECK(!actor->waypoints.count);
    P_MobjSetSelected(actor,false);
    path.points[0] = (ivec2_t){31,31};
    ticcmd_t command;
    G_BuildTiccmd(&command);
    CHECK(command.order == TC_PATH && command.path.points[0].x == 6);
    G_RunTiccmd(0,&command);
    CHECK(actor->waypoints.count == 2 && actor->waypoints.points[0].x == 6);
    G_BuildTiccmd(&command);
    CHECK(command.order == TC_NONE);
    netactive = false;
    P_MobjSetSelected(actor,true);
    path.points[0] = (ivec2_t){-1,0};
    CHECK(!G_PathOrder(&actor,1,&path));
    CHECK(actor->waypoints.count == 2);
    P_MoveOrderAt(&level,&actor,1,(fvec2_t){5.5f,5.5f});
    CHECK(!actor->waypoints.count);

    pathbook_t book;
    HU_PathReset(&book);
    book.path = (waypoints_t){.points = {{4,4},{5,5}},.count = 2,.current = 1,.mode = WP_ONCE};
    CHECK(HU_PathSave(&book));
    CHECK(book.saved_count == 1 && book.saved[0].count == 2);
    CHECK(HU_PathDelete(&book));
    CHECK(book.path.count == 1 && book.path.current == 0 && book.saved[0].count == 2);
    HU_PathClear(&book);
    CHECK(!book.path.count && !HU_PathGo(&book));
    HU_PathSelect(&book, 0);
    CHECK(book.selection == 0 && book.path.count == 2 && !book.path.current);
    CHECK(HU_PathPoint(&book, (cell_t){4,4}) && book.path.count == 2 && book.path.current == 0);
    HU_PathSelect(&book, -1);
    CHECK(book.selection == -1 && !book.path.count);
    P_FreeLevel(&level);
    printf("PASS: %s shared route traversal, cancellation, ownership, checksums, atomic commands and editing\n",g_game_id);
    return 0;
}
