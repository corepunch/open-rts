#include "engine.h"

gamesettings_t gamesettings = {10, 10, 2};

const char *D_UserDirectory(void) {
    const char *override = getenv("OPEN_RTS_USER_DIR");
    if (override && *override) return override;
    static char path[1024];
    if (!path[0]) {
        char *directory = SDL_GetPrefPath("open-rts", g_game_id);
        if (directory) {
            snprintf(path, sizeof(path), "%s", directory);
            SDL_free(directory);
        }
    }
    return path;
}

void D_LoadSettings(void) {
    char path[1200];
    M_PathJoin(path, sizeof(path), D_UserDirectory(), "settings.cfg");
    FILE *file = fopen(path, "r");
    if (!file) return;
    int speed;
    gamesettings_t settings;
    if (fscanf(file, "%d %d %d %d", &speed, &settings.sound, &settings.music, &settings.detail) == 4 &&
        speed >= 10 && speed <= 200 && settings.sound >= 0 && settings.sound <= 10 &&
        settings.music >= 0 && settings.music <= 10 && settings.detail >= 0 && settings.detail <= 2) {
        gamesettings = settings;
        D_SetGameSpeed(speed);
        S_SetVolume(settings.sound * 10);
    }
    fclose(file);
}

bool D_SaveSettings(int speed) {
    char path[1200], temporary[1200];
    if (!D_UserDirectory()[0]) return false;
    M_PathJoin(path, sizeof(path), D_UserDirectory(), "settings.cfg");
    M_PathJoin(temporary, sizeof(temporary), D_UserDirectory(), "settings.tmp");
    FILE *file = fopen(temporary, "w");
    if (!file) return false;
    bool ok = fprintf(file, "%d %d %d %d\n", speed, gamesettings.sound,
                      gamesettings.music, gamesettings.detail) > 0;
    if (fclose(file)) ok = false;
    if (ok && !rename(temporary, path)) return true;
    remove(temporary);
    return false;
}
