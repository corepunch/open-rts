/* The campaign's fifteen missions load, their keys parse, the player plays the campaign's
 * faction, every enemy and every ground to hold can be reached, tech limits hide the products
 * they leave out, and the front end's API lists the missions and keeps progress. */
#include "campaign_harness.h"

static bool listed(int owner, uint16_t type) {
    StaticProductDefinition list[64];
    int n = G_ModelGetProducts(NULL, owner, list, 64);
    for (int i = 0; i < n; ++i)
        if (list[i].product_type == type) return true;
    return false;
}

static int check_mission(int faction, int index) {
    rf_missioninfo_t info;
    CCHECK(RF_MissionInfo(NULL, faction, index, &info) || (snprintf(mission_name, sizeof(mission_name),
           "%s-%d", RF_CampaignSlug(faction), index + 1), 0), "RF_MissionInfo finds no title");
    RtsGameModel *model = load_mission(faction, index);
    if (!model) return 1;
    int f = -1, i = -1;
    CCHECK(RF_InMission(&f, &i) && f == faction && i == index, "the mission line does not match its file");
    CCHECK(RF_MissionLoadErrors() == 0, "has bad mission lines");
    CCHECK(info.title[0] && info.briefing[0] && strchr(info.briefing, '\n'), "needs a title and a briefing");
    CCHECK(!strcmp(info.title, RF_MissionTitle()), "the front end and the level disagree on the title");
    CCHECK(info.objective_count == RF_ObjectiveCount() && RF_ObjectiveCount() > 0, "objectives");
    for (int k = 0; k < RF_ObjectiveCount(); ++k) {
        rf_objective_t o;
        CCHECK(RF_ObjectiveDetail(k, &o) && !strcmp(info.objectives[k], RF_ObjectiveText(k)), "objective text");
        CCHECK((o.kind != RF_OBJ_DESTROY && o.kind != RF_OBJ_PROTECT) || live(find_id(o.tag_id)),
               "a destroy or protect objective names no thing on the map");
    }
    CCHECK(RF_OwnerFaction(0) == faction, "the player does not play the campaign's faction");
    int ux = 0, uy = 0;
    if (!reachable_from_player(&ux, &uy)) {
        fprintf(stderr, "FAIL (%s): the player cannot reach the thing at %d,%d\n", mission_name, ux, uy);
        return 1;
    }
    /* Mission 1 is a tutorial: no War Factory, so no tanks yet. */
    if (index == 0) {
        CCHECK(!listed(0, RF_Role(faction, RF_ROLE_FACTORY)) && listed(0, RF_Role(faction, RF_ROLE_POWER)),
               "the tutorial's tech limit");
        CCHECK(!G_ModelProductAvailable(NULL, 0, G_ModelProductByUIId(NULL, RF_Role(faction, RF_ROLE_FACTORY))),
               "a product left out of the tech list can still be built");
    }
    CCHECK(ticks(model, RTS_TICRATE * 2) && !G_LevelOutcome(), "ends at once");
    CCHECK(RF_LastMissionMessage()[0], "says nothing at the start");
    printf("%-34s %-22s %d objectives, %d s of briefing text\n", mission_name, info.title, info.objective_count,
           (int)strlen(info.briefing) / 15);
    rts_game_model_destroy(model);
    return 0;
}

static int check_mission_save(void) {
    RtsGameModel *model = load_mission(RF_CONCORD, 1);
    if (!model) return 1;
    CCHECK(ticks(model, RTS_TICRATE), "advance the mission");
    int elapsed = RF_MissionElapsedMs();
    app_t app = {0};
    hudtext_t hud = {0};
    AiContext *ai = rts_game_model_ai(model);
    void *save = NULL;
    size_t size = 0;
    CCHECK(elapsed > 0 && G_SaveGameBlob(&save, &size, "Shard Recovery", &app, ai, &hud), "save the mission");
    CCHECK(ticks(model, RTS_TICRATE) && RF_MissionElapsedMs() > elapsed, "advance beyond the save");
    bool loaded = G_LoadGameBlob(save, size, &app, ai, &hud);
    free(save);
    CCHECK(loaded && RF_MissionElapsedMs() == elapsed, "restore the campaign clock");
    rts_game_model_destroy(model);
    return 0;
}

static int check_progress(void) {
    snprintf(mission_name, sizeof(mission_name), "progress");
    CCHECK(RF_CampaignResetProgress(), "cannot write campaign.txt");
    for (int f = 0; f < RF_FACTION_COUNT; ++f) {
        CCHECK(RF_CampaignName(f) && RF_CampaignWon(f) == 0, "a fresh campaign");
        CCHECK(RF_MissionUnlocked(f, 0) && !RF_MissionUnlocked(f, 1), "only the first mission is open");
    }
    CCHECK(!RF_CampaignName(RF_FACTION_COUNT) && !RF_MissionUnlocked(0, 5) && !RF_MissionUnlocked(0, -1),
           "out of range");
    CCHECK(RF_CampaignRecordWin(RF_CLANS, 0) && RF_CampaignWon(RF_CLANS) == 1, "a win");
    CCHECK(RF_MissionUnlocked(RF_CLANS, 1) && !RF_MissionUnlocked(RF_CLANS, 2) && !RF_MissionUnlocked(RF_CONCORD, 1),
           "a win opens the next mission of its own campaign");
    CCHECK(RF_CampaignRecordWin(RF_CLANS, 3) && RF_CampaignWon(RF_CLANS) == 4, "a later win");
    CCHECK(RF_CampaignRecordWin(RF_CLANS, 1) && RF_CampaignWon(RF_CLANS) == 4, "replaying never loses progress");
    char path[1200];
    M_PathJoin(path, sizeof(path), D_UserDirectory(), "campaign.txt");
    FILE *file = fopen(path, "r");
    char text[256] = "";
    if (file) { size_t n = fread(text, 1, sizeof(text) - 1, file); text[n] = '\0'; fclose(file); }
    CCHECK(!strcmp(text, "directorate 0\nclans 4\nconcord 0\n"), "campaign.txt");
    CCHECK(RF_CampaignResetProgress() && RF_CampaignWon(RF_CLANS) == 0, "reset");
    char map[64];
    CCHECK(RF_MissionPath(RF_CONCORD, 4, map, sizeof(map)) && !strcmp(map, "maps/campaign/concord-5.txt") &&
           !RF_MissionPath(RF_CONCORD, 5, map, sizeof(map)), "mission paths");
    return 0;
}

/* A skirmish map is no mission: elimination decides it and nothing is limited. */
static int check_skirmish(void) {
    snprintf(mission_name, sizeof(mission_name), "skirmish");
    RtsGameModel *model = rts_game_model_create();
    RtsGameModelConfig config = {.data_root = g_game_default_root};
    CCHECK(rts_game_model_load(model, &config), "loads");
    CCHECK(!RF_InMission(NULL, NULL) && RF_ObjectiveCount() == 0 && RF_CampaignAllows(0, MT_MASTODON), "no mission");
    CCHECK(G_LevelObjectives(NULL, 0) == ELIMINATE_ALL, "elimination");
    rts_game_model_destroy(model);
    return 0;
}

int main(void) {
    campaign_user_dir();
    for (int f = 0; f < RF_FACTION_COUNT; ++f)
        for (int i = 0; i < RF_CAMPAIGN_MISSIONS; ++i) RTS_RUN(check_mission(f, i));
    RTS_RUN(check_mission_save());
    RTS_RUN(check_progress());
    RTS_RUN(check_skirmish());
    puts("PASS: fifteen missions load with their objectives, tech limits and briefings; progress is kept");
    return 0;
}
