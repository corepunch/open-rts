#include "kknd.h"
#include <string.h>

/* OpenKrush PNG sheets carry FrameSize and FrameAmount in checked tEXt chunks. */
bool W_LoadKkndPNG(SDL_Renderer *renderer, const char *name, spritesheet_t *out) {
    char path[1024];
    M_PathJoin(path, sizeof(path), "games/kknd", name);
    blob_t file;
    if (!W_ReadFile(path, &file)) return false;
    isize2_t size = {0};
    int frames = 0;
    for (size_t pos = 8; pos + 12 <= file.size;) {
        const uint8_t *p = file.bytes + pos;
        uint32_t n = (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
        if (n > file.size-pos-12) break;
        if (memcmp(p+4, "tEXt", 4) == 0 && n < 64) {
            char text[64] = {0};
            memcpy(text, p+8, n);
            size_t key = strlen(text);
            if (key < n && strcmp(text,"FrameSize") == 0)
                sscanf(text+key+1,"%d,%d",&size.w,&size.h);
            if (key < n && strcmp(text,"FrameAmount") == 0) frames = atoi(text+key+1);
        }
        pos += (size_t)n+12;
    }
    W_FreeFile(&file);
    char sequence_path[1024];
    if (snprintf(sequence_path,sizeof(sequence_path),"%.*s.yaml",(int)strlen(path)-4,path) >= (int)sizeof(sequence_path)) return false;
    FILE *sequence = fopen(sequence_path,"r");
    if (!sequence) return false;
    char line[256];
    ivec2_t offset = {0};
    bool idle = false;
    while (fgets(line,sizeof(line),sequence)) {
        if (line[0] == '\t' && line[1] != '\t') {
            if (idle) break;
            idle = strncmp(line,"\tidle:",6) == 0;
        }
        if (idle && strncmp(line,"\t\tOffset:",9) == 0)
            sscanf(line+9,"%d,%d",&offset.x,&offset.y);
    }
    fclose(sequence);
    if (!idle) return false;
    SDL_Surface *surface = W_LoadPNG(path);
    if (!surface) return false;
    bool ok = false;
    if (!frames && size.w > 0 && size.h > 0)
        frames = (surface->w/size.w)*(surface->h/size.h);
    if (size.w <= 0 || size.h <= 0 || size.w > surface->w || size.h > surface->h ||
        frames <= 0 || frames > (surface->w/size.w)*(surface->h/size.h) ||
        !W_SetPNGPalette(surface,"games/kknd/ui/palette.png") ||
        !R_AllocSpriteCells(out,frames) || !R_InitSpriteDef(out,frames,1)) goto done;
    out->frame_size = size;
    for (int i = 0; i < frames; ++i) {
        SDL_Surface *frame = SDL_CreateRGBSurfaceWithFormat(0,size.w,size.h,32,SDL_PIXELFORMAT_RGBA32);
        if (!frame) goto done;
        SDL_Rect source = {i%(surface->w/size.w)*size.w,i/(surface->w/size.w)*size.h,size.w,size.h};
        SDL_BlitSurface(surface,&source,frame,NULL);
        out->lumps[i].texture = SDL_CreateTextureFromSurface(renderer,frame);
        SDL_FreeSurface(frame);
        if (!out->lumps[i].texture) goto done;
        out->cells[i].rect = out->cells[i].bounds = (irect_t){0,0,size.w,size.h};
        /* These sheets' frame sequences share the idle sequence's placement. */
        out->cells[i].displacement = offset;
        if (!R_InstallSpriteLump(out,i,0,i,false)) goto done;
    }
    ok = true;
done:
    SDL_FreeSurface(surface);
    if (!ok) R_FreeSprite(out);
    return ok;
}
