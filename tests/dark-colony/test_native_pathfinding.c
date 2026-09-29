#define _POSIX_C_SOURCE 200809L
#include "mobj_test.h"
#include "p_local.h"
#include "p_path.h"
#include <unistd.h>

static void reset(int width, int height) {
    P_FreeLevel(&level);
    gameinfo = NULL;
    level = (level_t){.width = width, .height = height};
    level.blocked = calloc((size_t)width * height, 1);
    assert(level.blocked);
}

static void block(int x, int y) { level.blocked[L_Index(&level,x,y)] = 1; }

static void check_search(void) {
    reset(10,10);
    ivec2_t route[64];
    const ivec2_t expected[] = {{2,1},{3,1},{4,1},{5,2},{6,3}};
    int count = DC_FindPath(&level, (ivec2_t){1,1}, (ivec2_t){6,3}, NULL, false, route, 64);
    assert(count == 5 && !memcmp(route, expected, sizeof(expected)));
    /* Native diagonals require either side, not both (0x43fcbf). */
    block(2,1);
    assert(DC_FindPath(&level, (ivec2_t){1,1}, (ivec2_t){2,2}, NULL, false, route, 64) == 1);
    block(1,2); block(0,1); block(1,0);
    assert(!DC_FindPath(&level, (ivec2_t){1,1}, (ivec2_t){2,2}, NULL, false, route, 64));
    reset(80,8);
    count = DC_FindPath(&level, (ivec2_t){1,3}, (ivec2_t){70,3}, NULL, false, route, 32);
    assert(count == 32 && ivec2_equal(route[0], (ivec2_t){2,3}) &&
           ivec2_equal(route[31], (ivec2_t){33,3}));
    /* Both (4,4) and (4,2) offer cost 13 to (3,3). LIFO expands (4,4)
     * first; the equal-cost replacement from (4,2) wins (0x43fd0d). */
    reset(9,7);
    block(4,3);
    count = DC_FindPath(&level, (ivec2_t){1,3}, (ivec2_t){7,3}, NULL, false, route, 64);
    const ivec2_t around[] = {{2,3},{3,3},{4,2},{5,3},{6,3},{7,3}};
    assert(count == 6 && !memcmp(route, around, sizeof(around)));
}

static void check_regions(void) {
    reset(7,5);
    size_t size = 0x10000 + 35;
    uint8_t *pth = calloc(size,1);
    assert(pth);
    /* Reverse search follows 3 -> 2 -> 1; fourth family is a tempting shortcut. */
    pth[3*256+1] = 2;
    pth[2*256+1] = 1;
    for (int x=1;x<=5;++x) pth[0x10000+2*7+x] = x < 3 ? 1 : x==3 ? 4 : 3;
    for (int x=2;x<=4;++x) pth[0x10000+3*7+x] = 2;
    char path[] = "/private/tmp/dc-path-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0);
    FILE *file = fdopen(fd,"wb");
    assert(file && fwrite(pth,1,size,file)==size && !fclose(file));
    assert(DC_LoadPaths(&level,path));
    ivec2_t route[32];
    int count = DC_FindPath(&level,(ivec2_t){1,2},(ivec2_t){5,2},NULL,false,route,32);
    assert(count > 0 && ivec2_equal(route[count-1],(ivec2_t){5,2}));
    for (int i=0;i<count;++i) assert(!ivec2_equal(route[i],(ivec2_t){3,2}));
    assert(!DC_FindPath(&level,(ivec2_t){5,2},(ivec2_t){1,2},NULL,false,route,32));
    /* Broken chains and truncated files fail without looping or reading beyond EOF. */
    pth[2*256+1] = 3;
    file = fopen(path,"wb");
    assert(file && fwrite(pth,1,size,file)==size && !fclose(file));
    assert(DC_LoadPaths(&level,path));
    assert(!DC_FindPath(&level,(ivec2_t){1,2},(ivec2_t){5,2},NULL,false,route,32));
    assert(!truncate(path,17));
    assert(!DC_LoadPaths(&level,path));
    unlink(path);
    free(pth);
}

static mobj_t *unit_at(fvec2_t position, bool flying) {
    return spawn_mobj_fixture((mobj_t){.hp=10, .speed=5, .radius=0.25f,
        .traits=MF_MOBILE | (flying ? MF_FLY : 0),
        .core={.position=fixed3_from_fvec2(position, fixed_from_float(2))},
        .harvest={.target=-1}});
}

static void run_to_goal(mobj_t *unit, int tics) {
    int translated = 0;
    for (int i=0;i<tics && !unit->movement.order_arrived;++i) {
        fixed3_t before = unit->core.position;
        P_Ticker();
        fixed3_t delta = fixed3_planar_displacement(before,unit->core.position);
        assert(!delta.x || !delta.y || abs(delta.x)==abs(delta.y));
        assert(unit->core.position.z==before.z);
        translated += delta.x || delta.y;
    }
    assert(translated && unit->movement.order_arrived);
    assert(fvec2_near(fixed3_xy_to_fvec2(unit->core.position),unit->movement.goal,1.0f/FIXED_ONE));
}

static void check_movement(void) {
    static const fvec2_t goals[] = {
        {17.75f,13.25f},{13.25f,17.75f},{6.25f,17.75f},{2.25f,13.75f},
        {2.25f,6.75f},{6.75f,2.25f},{13.75f,2.25f},{17.25f,6.25f}
    };
    for (int flying=0;flying<2;++flying) {
        for (int i=0;i<8;++i) {
            reset(20,20);
            mobj_t *unit=unit_at((fvec2_t){10.25f,10.75f},flying);
            assert(P_MoveUnitTo(&level,unit,goals[i]));
            run_to_goal(unit,900);
            assert(!level.flow_fields);
        }
    }
    reset(80,12);
    mobj_t *unit=unit_at((fvec2_t){1.5f,5.5f},false);
    for (int y=0;y<10;++y) block(30,y);
    assert(P_MoveUnitTo(&level,unit,(fvec2_t){70.5f,5.5f}));
    run_to_goal(unit,2500);
    /* A newly occupied route triggers a local detour without crossing the unit. */
    reset(14,9);
    unit=unit_at((fvec2_t){1.5f,4.5f},false);
    assert(P_MoveUnitTo(&level,unit,(fvec2_t){11.5f,4.5f}));
    mobj_t *blocker=unit_at((fvec2_t){5.5f,4.5f},false);
    fixed3_t stationary=blocker->core.position;
    run_to_goal(unit,900);
    assert(!memcmp(&stationary,&blocker->core.position,sizeof(stationary)));
    P_ClearMove(unit);
    assert(!P_HasMoveOrder(unit) && !unit->route.count);
}

static ivec2_t claimed_cell(const mobj_t *unit) {
    return unit->route.traveling ? unit->route.cells[unit->route.current] :
           fvec2_cell(fixed3_xy_to_fvec2(unit->core.position));
}

static void check_groups(void) {
    for (int flying = 0; flying < 2; ++flying) {
        reset(24,24);
        mobj_t *units[16];
        for (int i = 0; i < 16; ++i)
            units[i] = unit_at((fvec2_t){2.5f + i % 4, 2.5f + i / 4}, flying);
        P_MoveUnitsAt(&level, units, 16, (fvec2_t){17.1f,17.9f});
        for (int i = 0; i < 16; ++i)
            assert(fvec2_near(units[i]->movement.goal, (fvec2_t){17.5f,17.5f}, 1.0f/FIXED_ONE));
        int arrived = 0;
        for (int tic = 0; tic < 3000 && arrived < 16; ++tic) {
            fixed3_t before[16];
            for (int i = 0; i < 16; ++i) before[i] = units[i]->core.position;
            P_Ticker();
            arrived = 0;
            for (int i = 0; i < 16; ++i) {
                fixed3_t delta = fixed3_planar_displacement(before[i], units[i]->core.position);
                assert(!delta.x || !delta.y || abs(delta.x) == abs(delta.y));
                arrived += units[i]->movement.order_arrived;
                for (int j = i + 1; j < 16; ++j)
                    assert(!ivec2_equal(claimed_cell(units[i]), claimed_cell(units[j])));
            }
        }
        assert(arrived == 16);
    }
    reset(8,8);
    mobj_t *unit = unit_at((fvec2_t){2.5f,3.5f}, false);
    unit_at((fvec2_t){3.5f,3.5f}, false);
    assert(P_MoveUnitTo(&level, unit, (fvec2_t){3.5f,3.5f}));
    fvec2_t target;
    bool final;
    assert(DC_MoveTarget(&level, unit, &target, &final));
    /* 5758 % 3 - 1 = 0; 10113 % 3 - 1 = -1 (retail RNG entries 1/2). */
    assert(level.random_index == 2 && !final);
    assert(fvec2_near(unit->movement.goal, (fvec2_t){3.5f,2.5f}, 1.0f/FIXED_ONE));

    reset(8,8);
    unit = unit_at((fvec2_t){2.5f,3.5f}, false);
    assert(P_MoveUnitTo(&level, unit, (fvec2_t){5.5f,3.5f}));
    assert(DC_MoveTarget(&level, unit, &target, &final));
    mobj_t *follower = unit_at((fvec2_t){1.5f,3.5f}, false);
    /* Claim (3,3) before translating, while (2,3) is already free. */
    assert(!DC_CheckStep(&level, follower, (fvec2_t){2.5f,3.5f}, (fvec2_t){3.5f,3.5f}));
    assert(DC_CheckStep(&level, follower, (fvec2_t){1.5f,3.5f}, (fvec2_t){2.5f,3.5f}));
}

int main(void) {
    check_search();
    check_regions();
    check_movement();
    check_groups();
    P_FreeLevel(&level);
    puts("PASS: native search costs, ties, regions, corners, route chunks, occupancy and eight-direction movement");
    return 0;
}
