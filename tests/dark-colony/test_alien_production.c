#include "engine.h"
#include "info.h"
#include "dark-colony.h"
#include "t_local.h"
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
        } else if (kind == 2) {
            int category = (int)strtol(p, &end, 10); p = end;
            int tier = (int)strtol(p, &end, 10); p = end;
            assert(product->product_type == row);
            assert(type >= 0 && type < 106);
            assert(category >= 0 && category <= 1 && tier >= 1 && tier <= 2);
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
    event.button.x = 540; event.button.y = 435;
    assert(G_CustomUIResponder(ui, app, &level, objects.items, objects.count, &event));
    P_FreeMobjList(&objects);
}

static void native_upgrade(const StaticProductDefinition *product, int *type, int *category, int *tier) {
    FILE *file = fopen("data/DCOLONY/GAMESTAT/DEPEND.TXT", "r");
    assert(file);
    char line[512];
    bool found = false;
    while (fgets(line, sizeof(line), file)) {
        int row,cost,ui,kind;
        if (sscanf(line,"%d %d %d %d %d %d %d", &row,&cost,&ui,&kind,type,category,tier) == 7 &&
            row == product->row_id) { found = true; break; }
    }
    fclose(file);
    assert(found);
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
    StaticProductDefinition products[64];
    int count = G_ModelGetProducts(NULL, player, products, 64);
    assert(count == 40);
    for (int i = 0; i < count; ++i) {
        assert(products[i].faction == player);
        check_dependency(&products[i]);
        native_button(&products[i]);
        assert(G_ModelActorIdForProduct(&products[i]));
    }
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 640, 480, 32, SDL_PIXELFORMAT_ARGB8888);
    assert(surface);
    V_AllocScreen(surface->w, surface->h);
    assert(screens[0].pixels);
    app_t app = {.win = {640,480}, .cell = {32,32}, .mouse = {-1,-1}};
    void *ui = G_InitCustomUI(&app, "data/DCOLONY");
    assert(ui);
    tileset_t tiles = {0}; spritesheet_t sprite = {0};
    assert(W_LoadAssets("data/DCOLONY", &level, "SPRITES/GRAY.SPR", &tiles, &sprite));
    spritecache_t *cache = calloc(1, sizeof(*cache));
    assert(cache && R_InitSprites("data/DCOLONY", &level, NULL, 0, cache));
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
    V_ReadPixels(surface->pixels, surface->pitch);
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
    mobj_t *science = find(player ? MT_ALIEN_MINDHIVE3 : MT_SCNCPOD2);
    assert(science);
    P_MobjSetSelected(science, true);
    objects = P_ListMobjs();
    SDL_Event tab = {.button = {.type = SDL_MOUSEBUTTONDOWN, .button = SDL_BUTTON_LEFT,
                               .x = 570, .y = 100}};
    assert(G_CustomUIResponder(ui, &app, &level, objects.items, objects.count, &tab));
    G_CustomUIDrawer(ui, &app, &level, objects.items, objects.count, cache, &hud);
    V_ReadPixels(surface->pixels, surface->pitch);
    assert(!SDL_SaveBMP(surface, player ? "/private/tmp/dc-alien-research.bmp" :
                                       "/private/tmp/dc-human-research.bmp"));
    P_FreeMobjList(&objects);
    for (int i = 0; i < count; ++i) {
        const StaticProductDefinition *product = &products[i];
        if (product->product_class != RTS_PRODUCT_UPGRADE) continue;
        int type, category, tier;
        native_upgrade(product, &type, &category, &tier);
        uint8_t *value = category == 0 ? &level.upgrades[type][player].weapon :
                                    &level.upgrades[type][player].armor;
        if (tier == 1) {
            const StaticProductDefinition *next = G_ModelProductByClassType(NULL, RTS_PRODUCT_UPGRADE, product->row_id + 1);
            assert(next && !G_ModelProductAvailable(NULL, player, next));
            assert(!G_PlayerBuildProduct(science, next));
        }
        assert(G_ModelProductAvailable(NULL, player, product));
        int before = level.player_resources[player][0];
        level.player_resources[player][0] = product->cost - 1;
        assert(!G_PlayerBuildProduct(science, product) && *value == tier - 1);
        level.player_resources[player][0] = before;
        click(ui, &app, product);
        assert(*value == tier && level.player_resources[player][0] == before - product->cost);
        assert(!G_PlayerBuildProduct(science, product));
        assert(level.player_resources[player][0] == before - product->cost);
        assert(!level.upgrades[type][1-player].weapon && !level.upgrades[type][1-player].armor);
        assert(!science->production || !science->production->queue_count);
    }
    G_ShutdownCustomUI(ui); R_FreeSpriteCache(cache); free(cache);
    R_FreeSprite(&sprite); R_FreeTileset(&tiles);
    V_FreeScreen();
    SDL_FreeSurface(surface); SDL_Quit();
    rts_game_model_destroy(model); netgame = false; consoleplayer = 0;
}

int main(void) {
    check_player(0);
    check_player(1);
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = "data/DCOLONY", .map_path = "SCENARIO/HUMAN/HUMAN03.MAP"};
    assert(model && rts_game_model_load(model, &config));
    const int human_types[] = {0,2,3,6,43,5,1,4};
    for (unsigned i = 0; i < sizeof(human_types)/sizeof(*human_types); ++i) {
        assert(level.upgrades[human_types[i]][1].weapon == 1);
        assert(level.upgrades[human_types[i]][1].armor == 1);
        assert(level.upgrades[human_types[i]][0].weapon == 0);
    }
    assert(level.upgrades[13][2].weapon == 1 && level.upgrades[13][2].armor == 0);
    rts_game_model_destroy(model);
    puts("PASS: 80 native faction products, sidebar purchases, research costs/tiers/owner isolation, 18 trained unit types");
    return 0;
}
