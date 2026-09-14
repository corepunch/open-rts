#define _DEFAULT_SOURCE
#include "m_menu.h"
#include "game.h"
#include "w_spr.h"
#include "d_net.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* DC.EXE's screen scripts own layout; the M_* lifecycle is separate from SB_*.
 * Native control IDs and dispatch: docs/DC_EXE_FINDINGS.md, Main-menu screens. */
bool menuactive;
bool menuerror;
const char *menumap;
static char root[1024], leader[128], mapname[1024];
static int race, itemOn, scroll;
static bool initialized, training;
static uint64_t menutime;
static const char *notice;
static char mission_title[128], mission_region[128];
static char *prose;
static enum { MAIN, SETUP, STORY, BRIEFING } page;
static isize2_t screen;
static bitmapfont_t fonts[2];
static spritesheet_t background;
static SDL_Renderer *menu_renderer;
static spritecache_t images;

typedef struct {
    dc_fin_t fin;
} menuanimation_t;
static menuanimation_t animations[16];
static int numanimations;

/* The native screen object has 300 controls (0x34 bytes each at +0x88).
 * Only the presentation fields needed by these four screens are decoded. */
typedef struct {
    enum { NONE, PUSH, CHECK, LABEL, TEXT, GADGET } kind;
    irect_t rect;
    char text[128], animation[32];
    int message, font, remap, gadget, mode, frame, delay;
    int maxchars;
    bool centered, visible;
    const dc_fin_t *fin;
    const dc_fin_label_t *sequence;
} menucontrol_t;
static menucontrol_t controls[300];
static char messages[300][128];

static void free_screen(void) {
    for (int i = 0; i < 2; ++i) HU_FreeFont(&fonts[i]);
    R_FreeSprite(&background);
    R_FreeSpriteCache(&images);
    for (int i = 0; i < numanimations; ++i) DC_FreeFIN(&animations[i].fin);
    numanimations = 0;
    free(prose);
    prose = NULL;
    memset(controls, 0, sizeof(controls));
    memset(messages, 0, sizeof(messages));
    screen = (isize2_t){0};
}

void M_Shutdown(void) {
    free_screen();
    initialized = menuactive = false;
    menumap = NULL;
    SDL_StopTextInput();
}

static bool load_animations(const char *name) {
    char path[1024], entry[128];
    M_PathJoin(path, sizeof(path), root, name);
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool ok = true;
    while (fscanf(file, "%127s", entry) == 1) {
        if (numanimations == 16) { ok = false; break; }
        dc_fin_t *fin = &animations[numanimations++].fin;
        M_PathJoin(path, sizeof(path), root, M_va("ANIMATE/%s", M_Upper(entry)));
        if (!DC_LoadFIN(path, fin)) { ok = false; break; }
        for (int i = 0; i < fin->header->dependency_count; ++i) {
            char key[32];
            snprintf(key, sizeof(key), "SPRITES/%.8s.SPR", fin->dependencies[i].name);
            M_Upper(key);
            if (R_CacheLookup(&images, key)) continue;
            if (images.count == MAX_DECORATION_SPRITES) { ok = false; break; }
            cachedsprite_t *image = &images.entries[images.count];
            M_PathJoin(path, sizeof(path), root, key);
            if (!DC_LoadSpriteImage(path, &image->sprite)) { ok = false; break; }
            snprintf(image->name, sizeof(image->name), "%s", key);
            ++images.count;
        }
        if (!ok) break;
    }
    if (ferror(file)) ok = false;
    fclose(file);
    return ok;
}

static void animate(int id, int mode) {
    menucontrol_t *control = &controls[id];
    if (!control->sequence) return;
    control->mode = mode;
    control->frame = control->sequence->start;
    control->delay = 0;
}

static bool read_text(const char *name) {
    char path[1024];
    M_PathJoin(path, sizeof(path), root, name);
    blob_t file;
    if (!W_ReadFile(path, &file)) return false;
    prose = malloc(file.size + 1);
    if (!prose) { W_FreeFile(&file); return false; }
    /* ~digit is a native text colour command, not displayed text. */
    size_t n = 0;
    for (size_t i = 0; i < file.size; ++i) {
        if (file.bytes[i] == '~' && i + 1 < file.size && isdigit(file.bytes[i + 1])) { ++i; continue; }
        if (file.bytes[i] != '\r') prose[n++] = (char)file.bytes[i];
    }
    prose[n] = '\0';
    W_FreeFile(&file);
    return true;
}

static bool load_screen(int next) {
    static const char *const scripts[] = {"INTROE", "NEWGAMEE", "STORYE", "SHUMANE"};
    static const char *const lists[] = {"INTRO.DAT", "CHOO.DAT", "LOADG.DAT", "SHUMAN.DAT"};
    free_screen();
    page = next;
    itemOn = page == STORY ? 5 : page == BRIEFING ? 2 : 0;
    scroll = 0;
    notice = NULL;
    char path[1024], line[512], name[128];
    if (!load_animations(M_va("INTRFACE/%s", lists[page]))) return false;
    M_PathJoin(path, sizeof(path), root, M_va("INTRFACE/%s", scripts[page]));
    FILE *file = fopen(path, "r");
    if (!file) return false;
    bool ok = true;
    while (fgets(line, sizeof(line), file)) {
        char kind[32];
        int id, desc;
        irect_t rect;
        if (sscanf(line, "size %d %d", &screen.w, &screen.h) == 2) continue;
        if (sscanf(line, "background %127s", name) == 1) {
            M_PathJoin(path, sizeof(path), root, M_va("%s.GIF", M_Upper(name)));
            if (!W_LoadGIFTexture(menu_renderer, path, &background)) ok = false;
        } else if (sscanf(line, "font %d %127s", &id, name) == 2) {
            if (id < 0 || id >= 2 || !DC_LoadFont(root,
                    M_va("%s.SPR", M_Upper(name)), &fonts[id])) ok = false;
        } else if (sscanf(line, "textmsg %d %127[^\r\n]", &id, name) == 2) {
            if (id < 0 || id >= 300) { ok = false; break; }
            strcpy(messages[id], name);
        } else if (sscanf(line, "%31s %d %d %d %d %d %d", kind, &id, &desc,
                           &rect.x, &rect.y, &rect.w, &rect.h) == 7 &&
                   (!strcmp(kind, "pushb") || !strcmp(kind, "checkb") ||
                    !strcmp(kind, "label") || !strcmp(kind, "in_text") || !strcmp(kind, "gadget"))) {
            if (id < 0 || id >= 300 || rect.w <= 0 || rect.h <= 0) { ok = false; break; }
            menucontrol_t *control = &controls[id];
            *control = (menucontrol_t){ .rect = rect, .gadget = -1, .visible = true };
            control->kind = !strcmp(kind, "pushb") ? PUSH : !strcmp(kind, "checkb") ? CHECK :
                !strcmp(kind, "label") ? LABEL : !strcmp(kind, "in_text") ? TEXT : GADGET;
            char *label = strstr(line, "label centre ");
            if (label) {
                sscanf(label, "label centre %d %d", &control->message, &control->font);
                control->centered = true;
            } else if ((label = strstr(line, " label "))) sscanf(label, " label %d", &control->message);
            if ((label = strstr(line, "remap "))) sscanf(label, "remap %d", &control->remap);
            if ((label = strstr(line, " font "))) sscanf(label, " font %d", &control->font);
            if (control->kind == TEXT) {
                control->maxchars = rect.w;
                sscanf(line, "%*s %*d %*d %*d %*d %*d %*d %d", &control->font);
            }
            if (control->message < 0 || control->message >= 300 || control->font < 0 || control->font >= 2) { ok = false; break; }
            if (control->kind == GADGET) {
                sscanf(line, "%*s %*d %*d %*d %*d %*d %*d %31s", control->animation);
                for (int i = 0; i < numanimations && !control->sequence; ++i) {
                    control->fin = &animations[i].fin;
                    control->sequence = DC_FINLabel(control->fin, control->animation);
                }
                if (!control->sequence) { ok = false; break; }
                animate(id, strstr(line, "anim_oneoff") ? 1 : strstr(line, "anim_loop") ? 0 : 2);
            }
        } else if (!strncmp(line, "banim", 5)) {
            int values[300], count = 0;
            char *cursor = line + 5, *end;
            while (count < 300) {
                long value = strtol(cursor, &end, 10);
                if (end == cursor) break;
                if (value < 0 || value >= 300) { ok = false; break; }
                values[count++] = (int)value;
                cursor = end;
            }
            if (count < 4 || values[2] <= 0 || values[2] > values[3] || count != 4 + values[2] + values[3]) { ok = false; break; }
            for (int i = 0; i < values[3]; ++i) {
                menucontrol_t *button = &controls[values[4 + values[2] + i]];
                for (int j = 0; j < values[2]; ++j) {
                    int gadget = values[4 + j];
                    if (button->rect.x == controls[gadget].rect.x && button->rect.y == controls[gadget].rect.y)
                        button->gadget = gadget;
                }
            }
        }
    }
    if (ferror(file)) ok = false;
    fclose(file);
    for (int i = 0; i < 300; ++i) {
        menucontrol_t *control = &controls[i];
        if (control->message) strcpy(control->text, messages[control->message]);
    }
    if (page == MAIN) animate(14, 1); /* 0x404b8c..0x404b9c. */
    if (page == SETUP) {
        controls[training ? 3 : 2].visible = false;
        controls[19].visible = controls[20].visible = false;
        for (int i = 21; i <= 26; ++i) controls[i].visible = false;
        controls[race ? 26 : 23].visible = true;
        animate(race ? 26 : 23, 0);
        for (int i = 13; i <= 16; ++i) animate(i, 0);
        for (int i = 29; i <= 35; ++i) animate(i, 0);
        snprintf(controls[5].text, sizeof(controls[5].text), "%s", leader);
        SDL_StartTextInput();
    } else SDL_StopTextInput();
    if (page == STORY && !read_text(race ? "INTRFACE/ASTORY.TXT" : "INTRFACE/HSTORY.TXT")) ok = false;
    if (page == BRIEFING) {
        controls[10].visible = false;
        controls[race ? 36 : 35].visible = false;
        for (int i = 15; i <= 24; ++i) animate(i, 0);
        animate(39, 0);
        animate(race ? 35 : 36, 0);
        snprintf(controls[5].text, sizeof(controls[5].text), "%s", leader);
        snprintf(controls[6].text, sizeof(controls[6].text), "%s", mission_title);
        snprintf(controls[7].text, sizeof(controls[7].text), "%s", mission_region);
        if (!read_text(M_va("%.*s.TXT", (int)strlen(mapname) - 4, mapname))) ok = false;
    }
    menutime = SDL_GetTicks64();
    return ok && screen.w > 0 && screen.h > 0 && background.numlumps && fonts[0].sprite.numlumps;
}

bool M_Init(app_t *app, const char *data_root) {
    menu_renderer = app->renderer;
    menuerror = false;
    if (strlen(data_root) >= sizeof(root)) return false;
    strcpy(root, data_root);
    initialized = load_screen(MAIN);
    if (!initialized) M_Shutdown();
    return initialized;
}

void M_StartControlPanel(app_t *app) {
    if (!initialized || menuactive) return;
    if (page != MAIN && !load_screen(MAIN)) {
        fprintf(stderr, "Could not load Dark Colony main menu\n");
        menuerror = true;
        app->running = false;
        return;
    }
    menuactive = true;
    itemOn = 0;
    notice = NULL;
    app->dragging_select = false;
    app->selection_rect = (irect_t){0};
}

static bool first_mission(void) {
    char path[1024], line[256];
    M_PathJoin(path, sizeof(path), root, M_va("GAMESTAT/%sSCENE.TXT", race ? (training ? "GT" : "G") : (training ? "HT" : "H")));
    FILE *file = fopen(path, "r");
    if (!file) return false;
    /* Eight faction-name lines, count, then scenario/title/region/map prefix. */
    bool ok = true;
    for (int i = 0; i < 10; ++i) if (!fgets(line, sizeof(line), file)) ok = false;
    if (!fgets(mission_title, sizeof(mission_title), file) ||
        !fgets(mission_region, sizeof(mission_region), file) || !fgets(line, sizeof(line), file)) ok = false;
    fclose(file);
    if (!ok) return false;
    mission_title[strcspn(mission_title, "\r\n")] = '\0';
    mission_region[strcspn(mission_region, "\r\n")] = '\0';
    line[strcspn(line, "\r\n")] = '\0';
    snprintf(mapname, sizeof(mapname), "%s.MAP", M_Upper(line));
    return true;
}

static void activate(app_t *app, int id, bool inlevel) {
    bool ok = true;
    notice = NULL;
    if (page == MAIN) {
        if (id == 12) app->running = false;
        else if ((id == 0 || id == 1) && !netgame) {
            training = id == 1;
            race = 0;
            ok = load_screen(SETUP);
        } else notice = inlevel ? "Escape resumes the current game" : "This menu is not implemented yet";
    } else if (page == SETUP) {
        if (id == 0 || id == 1) {
            race = id;
            controls[23].visible = race == 0;
            controls[26].visible = race == 1;
            animate(race ? 26 : 23, 0);
        } else if (id == 4) ok = load_screen(MAIN);
        else if (id == 2 || id == 3) {
            if (!leader[0]) { itemOn = 5; notice = "Type a name for your leader"; }
            else ok = first_mission() && load_screen(training ? BRIEFING : STORY);
        }
    } else if (page == STORY) {
        if (id == 4) ok = load_screen(SETUP);
        else if (id == 5) ok = load_screen(BRIEFING);
        else if (id == 2) { if (scroll > 0) --scroll; }
        else if (id == 3) ++scroll;
    } else if (page == BRIEFING) {
        if (id == 0) ok = load_screen(training ? SETUP : STORY);
        else if (id == 2) { menumap = mapname; menuactive = false; }
        else if (id == 3) ++scroll;
        else if (id == 4) { if (scroll > 0) --scroll; }
        else if (id == 1) notice = "Encyclopedia is not implemented yet";
    }
    if (!ok) {
        fprintf(stderr, "Could not load Dark Colony menu screen %d\n", page);
        menuerror = true;
        app->running = false;
    }
}

static bool selectable(int id) {
    const menucontrol_t *control = &controls[id];
    return control->visible && (control->kind == PUSH || control->kind == CHECK ||
                                (page == SETUP && id == 5));
}

bool M_Responder(app_t *app, const SDL_Event *event, bool inlevel) {
    if (!initialized) return false;
    if (event->type == SDL_QUIT) { app->running = false; return true; }
    if (event->type == SDL_WINDOWEVENT) return false;
    if (!menuactive) {
        if (event->type != SDL_KEYDOWN || event->key.keysym.sym != SDLK_ESCAPE) return false;
        if (!event->key.repeat) M_StartControlPanel(app);
        return true;
    }
    int olditem = itemOn;
    if (event->type == SDL_KEYDOWN) {
        switch (event->key.keysym.sym) {
        case SDLK_ESCAPE:
            if (event->key.repeat) break;
            if (page != MAIN) activate(app, page == SETUP || page == STORY ? 4 : 0, inlevel);
            else if (inlevel) menuactive = false;
            break;
        case SDLK_UP: case SDLK_DOWN: case SDLK_TAB:
            do { itemOn = (itemOn + (event->key.keysym.sym == SDLK_UP ? 299 : 1)) % 300; } while (!selectable(itemOn));
            break;
        case SDLK_RETURN: case SDLK_KP_ENTER:
            if (!event->key.repeat) activate(app, page == SETUP && itemOn == 5 ? (training ? 2 : 3) : itemOn, inlevel);
            break;
        case SDLK_BACKSPACE:
            if (page == SETUP && itemOn == 5 && leader[0]) {
                leader[strlen(leader) - 1] = '\0';
                strcpy(controls[5].text, leader);
            }
            break;
        default: break;
        }
    } else if (event->type == SDL_TEXTINPUT && page == SETUP && itemOn == 5) {
        size_t length = strlen(leader);
        for (const unsigned char *p = (const unsigned char *)event->text.text; *p; ++p) {
            if (*p < 32 || *p >= 127 || length >= sizeof(leader) - 1 ||
                length >= (size_t)controls[5].maxchars) continue;
            leader[length++] = (char)*p;
        }
        leader[length] = '\0';
        strcpy(controls[5].text, leader);
        notice = NULL;
    } else if (event->type == SDL_MOUSEMOTION || (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT)) {
        ivec2_t point;
        int x = event->type == SDL_MOUSEMOTION ? event->motion.x : event->button.x;
        int y = event->type == SDL_MOUSEMOTION ? event->motion.y : event->button.y;
        R_WindowToRenderPt(app, x, y, &point.x, &point.y);
        point = (ivec2_t){ point.x * screen.w / app->win.w, point.y * screen.h / app->win.h };
        for (int i = 0; i < 300; ++i) {
            irect_t rect = controls[i].rect;
            if (controls[i].kind == TEXT) rect = (irect_t){rect.x, rect.y, rect.w * fonts[0].glyph_size.w, fonts[0].line_h};
            if (!selectable(i) || !irect_contains(rect, point)) continue;
            itemOn = i;
            if (event->type == SDL_MOUSEBUTTONDOWN) activate(app, i, inlevel);
            break;
        }
    } else if (event->type == SDL_MOUSEWHEEL && (page == STORY || page == BRIEFING)) {
        scroll -= event->wheel.y;
        if (scroll < 0) scroll = 0;
    }
    if (itemOn != olditem && controls[itemOn].gadget >= 0) animate(controls[itemOn].gadget, 1);
    return true;
}

void M_Ticker(void) {
    if (!menuactive) return;
    uint64_t now = SDL_GetTicks64();
    if (now - menutime <= 16) return; /* DC.EXE 0x421ebd: menu cadence. */
    menutime = now;
    for (int i = 0; i < 300; ++i) {
        menucontrol_t *c = &controls[i];
        if (!c->visible || !c->sequence || c->mode == 2) continue;
        if (!c->delay) {
            if (++c->frame > c->sequence->end) {
                /* Gadget ticker 0x422828 holds the last one-off pose;
                 * the world animation ticker 0x423dd0 resets to zero. */
                if (c->mode == 1) {
                    c->frame = c->sequence->end;
                    c->mode = 2;
                    continue;
                }
                c->frame = c->sequence->start;
            }
            int raw = c->fin->frames[c->frame].ticks;
            c->delay = ((raw ? raw : 15) + 3) * 15 / 100;
        }
        if (c->delay) --c->delay;
    }
}

static void draw_gadget(SDL_Renderer *renderer, const menucontrol_t *c) {
    const dc_fin_frame_t *frame = &c->fin->frames[c->frame];
    const dc_fin_command_t *parts = c->fin->frame_commands[c->frame];
    for (int i = 0; i < frame->part_count; ++i) {
        const dc_fin_command_t *part = &parts[i];
        char key[32];
        snprintf(key, sizeof(key), "SPRITES/%.8s.SPR", part->sprite);
        const spritesheet_t *sprite = R_CacheLookup(&images, M_Upper(key));
        if (!sprite || part->cell < 0 || part->cell >= sprite->numlumps) continue;
        const spritecell_t *cell = &sprite->cells[part->cell];
        bool flipped = (part->flags & RTS_FRAME_FLIP_X) != 0;
        irect_t dst = {c->rect.x + part->offset.x + (flipped ? 0 : cell->displacement.x),
                        c->rect.y + part->offset.y - cell->rect.h, cell->rect.w, cell->rect.h};
        R_DrawSprite(renderer, sprite, part->cell, -1, &cell->rect, &dst,
                      flipped ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE,
                      (SDL_Color){255, 255, 255, 255}, SDL_BLENDMODE_BLEND);
    }
}

void M_Drawer(const app_t *app) {
    if (!menuactive) return;
    float sx, sy;
    SDL_Rect oldclip;
    bool clipped = SDL_RenderIsClipEnabled(app->renderer);
    SDL_RenderGetClipRect(app->renderer, &oldclip);
    SDL_RenderSetClipRect(app->renderer, NULL);
    SDL_RenderGetScale(app->renderer, &sx, &sy);
    SDL_RenderSetScale(app->renderer, (float)app->win.w / screen.w, (float)app->win.h / screen.h);
    irect_t dst = {0, 0, screen.w, screen.h};
    R_DrawSprite(app->renderer, &background, 0, -1, NULL, &dst,
                  SDL_FLIP_NONE, (SDL_Color){255, 255, 255, 255}, SDL_BLENDMODE_NONE);
    for (int i = 0; i < 300; ++i) {
        const menucontrol_t *c = &controls[i];
        if (c->visible && c->sequence) draw_gadget(app->renderer, c);
    }
    for (int i = 0; i < 300; ++i) {
        const menucontrol_t *c = &controls[i];
        if (!c->visible || !c->text[0]) continue;
        const bitmapfont_t *font = &fonts[c->font];
        ivec2_t at = {c->rect.x, c->rect.y};
        if (c->centered) at = ivec2_add(at, (ivec2_t){(c->rect.w - HU_TextWidth(font, c->text, 1)) / 2, (c->rect.h - font->glyph_size.h) / 2});
        HU_DrawTextRemapped(app->renderer, font, at.x, at.y, c->text,
                            (SDL_Color){255, 255, 255, 255}, 1, c->remap);
    }
    if (prose) {
        /* Text viewport arguments at 0x4023f8/0x403030, separate from widgets. */
        irect_t clip = page == STORY ? (irect_t){10, 13, 579, 420} : (irect_t){310, 212, 294, 225};
        SDL_RenderSetClipRect(app->renderer, &clip);
        HU_DrawTextWrapped(app->renderer, &fonts[0], clip.x, clip.y - scroll * fonts[0].line_h,
                           clip.w, prose, (SDL_Color){255, 255, 255, 255}, 1);
        SDL_RenderSetClipRect(app->renderer, NULL);
    }
    SDL_SetWindowTitle(app->window, notice ? notice : "Dark Colony");
    SDL_RenderSetScale(app->renderer, sx, sy);
    SDL_RenderSetClipRect(app->renderer, clipped ? &oldclip : NULL);
}
