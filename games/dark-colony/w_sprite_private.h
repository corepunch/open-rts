#ifndef __W_SPRITE_PRIVATE__
#define __W_SPRITE_PRIVATE__

#include "engine.h"

/* Temporary decoded SPR/FIN ownership belongs to the loader.
 * Destroy after conversion to engine source images and frame definitions. */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t dis_x;
    uint16_t dis_y;
    uint8_t used;
    uint32_t data_size;
    uint8_t *data;
} JuiceCell;

typedef struct {
    uint16_t flags;
    uint16_t cell_count;
    uint32_t payload_bytes;
    bool chunked;
    uint32_t palette[256];
    JuiceCell *cells;
} JuiceFile;

typedef struct {
    char name[9];
} AnimationDependency;

typedef struct {
    char name[17];
    uint16_t start;
    uint16_t end;
} AnimationLabel;

typedef struct {
    char sprite[9];
    int16_t frame;
    int16_t x;
    int16_t y;
    int16_t remap;
    int16_t intensity;
    int16_t layer;
    int16_t flags;
} AnimationCommand;

typedef struct {
    uint16_t frame_count;
    uint16_t aux_count;
    uint16_t label_count;
    uint16_t dependency_count;
    AnimationDependency *dependencies;
    AnimationLabel *labels;
    uint8_t *aux_records;
    AnimationCommand *commands;
    int command_count;
} AnimationFile;


void DC_DecodePalette(const uint8_t *spr, size_t size, uint32_t colors[256]);
void DC_FreeJuice(JuiceFile *juice);
bool DC_LoadJuice(const char *path, JuiceFile *out);
void DC_FreeAnimation(AnimationFile *animation);
bool DC_LoadAnimation(const char *path, AnimationFile *out);
const AnimationCommand *DC_FindAnimationCommand(
    const AnimationFile *animation, const char *label_name,
    const char *sprite_name, int frame, int layer);
const AnimationCommand *DC_AnimationFrameCommand(
    const AnimationFile *animation, int frame_index,
    const char *sprite_name, int layer);
const AnimationLabel *DC_FindAnimationLabel(const AnimationFile *animation,
                                                  const char *name);
bool DC_AnimationPath(char *out, size_t out_size,
                                                  const char *sprite_path);
bool DC_SpritePath(char *out, size_t out_size,
                                      const char *animation_path);
bool DC_DependencySpriteName(char *out, size_t out_size,
                                               const char *dependency);
void DC_SpriteStem(char *out, size_t out_size, const char *sprite_path);
bool DC_AssetExists(const char *path);
/* On success, animation_out receives ownership of decoded FIN storage;
 * the caller releases it with DC_FreeAnimation after loading dependencies.
 * The sheet contains only engine images and sprite definitions. */
bool DC_LoadSpriteWithAnimation(SDL_Renderer *renderer, const char *path,
                                             spritesheet_t *out,
                                             uint32_t palette_out[256],
                                             AnimationFile *animation_out);
#endif
