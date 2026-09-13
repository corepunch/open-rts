#include "engine.h"
#include "game.h"
#include "g_game.h"
#include "info.h"
#include "dc_types.h"
#include "d_net.h"
#include "w_spr.h"
#include "rts_model_test.h"
#include <assert.h>

static mobj_t *find(int type) {
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        mobj_t *u = (mobj_t *)th;
        if (!u->remove && u->owner == consoleplayer && u->type_id == type) return u;
    }
    return NULL;
}

static void tick(void *ui) {
    P_Ticker();
    mobjlist_t objects = P_ListMobjs();
    G_UpdateProduction(ui, &level, objects.items, &objects.count, FIXED_DT);
    P_FreeMobjList(&objects);
}

static irect_t native_button(const StaticProductDefinition *product) {
    FILE *file = fopen("data/DCOLONY/INTRFACE/MAINE", "r");
    assert(file);
    char line[512];
    irect_t rect = {0};
    while (fgets(line, sizeof(line), file)) {
        int id, desc, icon, pressed;
        irect_t value;
        if (sscanf(line, "count %d %d %d %d %d %d %d %d", &id, &desc,
                   &value.x, &value.y, &value.w, &value.h, &icon, &pressed) == 8 && id == product->ui_id) {
            assert(icon == product->icon_frame);
            rect = value;
            break;
        }
    }
    fclose(file);
    assert(rect.w && rect.h);
    return rect;
}

static void check_dependency(const StaticProductDefinition *product) {
    FILE *file = fopen("data/DCOLONY/GAMESTAT/DEPEND.TXT", "r");
    assert(file);
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        int row, cost, ui, kind, type, consumed;
        if (sscanf(line, "%d %d %d %d %d%n", &row, &cost, &ui, &kind, &type, &consumed) != 5 ||
            row != product->row_id) continue;
        assert(cost == product->cost && ui == product->ui_id);
        assert(kind + 1 == (int)product->product_class);
        char *p = line + consumed, *end;
        if (kind == 0) {
            int upgrade = (int)strtol(p, &end, 10); p = end;
            int race = (int)strtol(p, &end, 10); p = end;
            static const int city[2][2][5] = {
                {{16,17,18,20,22}, {16,17,19,21,22}},
                {{28,29,30,32,34}, {28,29,31,33,34}}
            };
            assert(race == product->faction && type >= 0 && type < 5 && upgrade >= 0 && upgrade < 2);
            assert(product->product_type == city[race][upgrade][type]);
        } else assert(type == product->product_type);
        for (int i = 0; i < product->prerequisite_count; ++i) {
            int required = (int)strtol(p, &end, 10); assert(end != p); p = end;
            assert(required == product->prerequisites[i]);
        }
        assert(strtol(p, &end, 10) == -1 && end != p);
        found = true;
        break;
    }
    fclose(file);
    assert(found);
}

static void click(void *ui, app_t *app, const StaticProductDefinition *product) {
    irect_t rect = native_button(product);
    SDL_Event event = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT,
                                  .x = rect.x + rect.w / 2, .y = rect.y + rect.h / 2}};
    mobjlist_t objects = P_ListMobjs();
    assert(G_CustomUIResponder(ui, app, &level, objects.items, objects.count, &event));
    P_FreeMobjList(&objects);
}

static void check_player(int player) {
    netgame = true;
    doomcom->numplayers = 2;
    consoleplayer = player;
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/MPLAYER/D2PLAY01.MAP"};
    assert(model && rts_game_model_load(model, &config));
    assert(DC_PlayerRace(0) == 0 && DC_PlayerRace(1) == 1);
    assert(find(player ? MT_ALIEN_MINDHIVE : MT_EXCOPOD));
    if (level.destroy_mission) level.destroy_mission(level.mission);
    level.mission = NULL; level.destroy_mission = NULL;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        P_MobjSetSelected((mobj_t *)th, false);
    StaticProductDefinition products[32];
    int count = G_ModelGetProducts(NULL, player, products, 32);
    assert(count == 16);
    for (int i = 0; i < count; ++i) {
        assert(products[i].faction == player);
        check_dependency(&products[i]);
        native_button(&products[i]);
        assert(G_ModelActorIdForProduct(&products[i]));
    }
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    r_renderer = SDL_CreateSoftwareRenderer(surface);
    assert(r_renderer);
    app_t app = {.renderer = r_renderer, .win = {640,480}, .cell = {32,32}, .mouse = {-1,-1}};
    void *ui = G_InitCustomUI(&app, "data/DCOLONY");
    assert(ui);
    tileset_t tiles = {0}; spritesheet_t sprite = {0};
    assert(W_LoadAssets(r_renderer, "data/DCOLONY", &level, "SPRITES/GRAY.SPR", &tiles, &sprite));
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(cache && R_InitSprites(r_renderer, "data/DCOLONY", &level, NULL, 0, cache));
    const int modules[2][6] = {{80, 81, 82, 85, 86, 83}, {41, 42, 43, 97, 98, 44}};
    level.player_resources[player][0] = 100000;
    for (size_t i = 0; i < sizeof(modules[player])/sizeof(*modules[player]); ++i) {
        const StaticProductDefinition *product = G_ModelProductByUIId(NULL, modules[player][i]);
        assert(product && G_ModelProductAvailable(NULL, player, product));
        int before = level.player_resources[player][0];
        click(ui, &app, product);
        mobj_t *building = find(G_ModelActorIdForProduct(product));
        assert(building && building->owner == player && building->team == player);
        assert(level.player_resources[player][0] == before - product->cost);
        assert(states[building->core.state_id].group == 6);
        click(ui, &app, product);
        assert(level.player_resources[player][0] == before - product->cost);
        for (int t = 0; t < 600 && states[building->core.state_id].group == 6; ++t) tick(ui);
        assert(states[building->core.state_id].group == 1);
    }
    assert(!find(player ? MT_ALIEN_BRDRHIVE : MT_ROBOPOD));
    assert(!find(player ? MT_ALIEN_MINDHIVE2 : MT_SCNCPOD));
    assert(DC_ProductActorMatches(MT_ALIEN_BRDRHIVE2, MT_ALIEN_BRDRHIVE));
    assert(DC_ProductActorMatches(MT_ALIEN_MINDHIVE3, MT_ALIEN_MINDHIVE2));
    hudtext_t hud = {0};
    mobjlist_t objects = P_ListMobjs();
    G_CustomUIDrawer(ui, &app, &level, objects.items, objects.count, cache, &hud);
    SDL_RenderPresent(r_renderer);
    assert(!SDL_SaveBMP(surface, player ? "/private/tmp/dc-alien-production.bmp" :
                                       "/private/tmp/dc-human-production.bmp"));
    P_FreeMobjList(&objects);
    for (int i = 0; i < count; ++i) {
        const StaticProductDefinition *product = &products[i];
        if (product->product_class != RTS_PRODUCT_UNIT) continue;
        uint32_t first_id = level.next_mobj_id;
        int before = level.player_resources[player][0];
        assert(G_FindProducer(player, product));
        click(ui, &app, product);
        assert(level.player_resources[player][0] == before - product->cost);
        mobj_t *born = NULL;
        for (int t = 0; t < 1200 && !born; ++t) {
            tick(ui);
            for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
                mobj_t *u = (mobj_t *)th;
                if (!u->remove && u->id >= first_id && u->owner == player &&
                    u->type_id == G_ModelActorIdForProduct(product)) born = u;
            }
        }
        assert(born && born->team == player && born->hp > 0);
        assert(born->core.state_id > 0 && states[born->core.state_id].group == 1);
        const spritesheet_t *sheet = R_StateSprite(cache, gameinfo, born->core.sprite_id, NULL);
        assert(sheet && born->core.frame >= sheet->numlumps && born->core.frame < sheet->spritedef.numframes);
        P_RemoveMobj(born);
        P_RunThinkers();
    }
    G_ShutdownCustomUI(ui); R_FreeSpriteCache(cache); free(cache);
    R_FreeSprite(&sprite); R_FreeTileset(&tiles);
    SDL_DestroyRenderer(r_renderer); r_renderer = NULL;
    SDL_FreeSurface(surface); SDL_Quit();
    rts_game_model_destroy(model); netgame = false; consoleplayer = 0;
}

int main(void) {
    check_player(0);
    check_player(1);
    puts("PASS: 32 native faction products, all sidebar purchases, module upgrades, 18 trained unit types");
    return 0;
}
