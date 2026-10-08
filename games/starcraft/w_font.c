#include "sc_local.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
bool sc_font(const char *root,const char *name,bitmapfont_t *font) {
    char path[2048]; snprintf(path,sizeof(path),"%s/install/files/font/%s.fnt",root,name);
    blob_t b={0}; memset(font,0,sizeof(*font));
    if(!W_ReadFile(path,&b)) return false;
    bool ok=false;
    if(b.size<8||memcmp(b.bytes,"FONT",4)) goto done;
    int low=b.bytes[4],high=b.bytes[5],w=b.bytes[6],h=b.bytes[7],n=high-low+1;
    if(n<1||!w||!h||b.size<8u+n*4u||!R_AllocSpriteCells(&font->sprite,n)) goto done;
    spritesheet_t *s=&font->sprite; s->indexed=true; s->frame_size=(isize2_t){w,h};
    font->glyph_size=(isize2_t){w,h}; font->line_h=h; font->draw_divisor=1; font->own_palette=true;
    font->glyph_limit=256;
    for(int i=0;i<256;i++) { font->glyph_index[i]=-1; font->glyph_width[i]=w/2; }
    for(int i=0;i<n;i++) {
        unsigned off=read_u32_le(b.bytes+8+i*4);
        uint8_t *pixels=calloc((size_t)w*h,1); if(!pixels) goto done;
        s->lumps[i].indices=pixels; s->cells[i].rect=s->cells[i].bounds=(irect_t){0,0,w,h};
        font->glyph_index[low+i]=i;
        if(!off) continue;
        if(off+4>b.size) goto done;
        int gw=b.bytes[off],gh=b.bytes[off+1],ox=b.bytes[off+2],oy=b.bytes[off+3];
        if(ox+gw>w||oy+gh>h) goto done;
        font->glyph_width[low+i]=ox+gw+1;
        size_t at=off+4; int p=0;
        while(p<gw*gh) {
            if(at>=b.size) goto done;
            unsigned code=b.bytes[at++]; p+=(int)(code>>3);
            if(p>=gw*gh) break;
            pixels[(oy+p/gw)*w+ox+p%gw]=(code&7) ? (code&7)+1 : 0; ++p;
        }
    }
    ok=sc_font_colors(root,"glue/palmm/tfont.pcx",font);
done:
    W_FreeFile(&b); if(!ok) R_FreeSprite(&font->sprite); return ok;
}
bool HU_LoadFont(const char *root,bitmapfont_t *font) { return sc_font(root,"font10",font); }
/* Five authored font ramps: normal, button, hotkey, disabled, highlighted. */
bool sc_font_colors(const char *root,const char *name,bitmapfont_t *font) {
    char path[2048]; snprintf(path,sizeof(path),"%s/native/%s",root,name);
    spritesheet_t ramp={0}; if(!W_LoadIndexedSheet(path,&ramp)) return false;
    if(ramp.frame_size.w*ramp.frame_size.h<40){R_FreeSprite(&ramp);return false;}
    spritesheet_t *s=&font->sprite;
    free(s->palette_maps); s->palette_maps=calloc(5,sizeof(*s->palette_maps));
    if(!s->palette_maps){R_FreeSprite(&ramp);return false;}
    s->palette_map_count=5;
    for(int k=0;k<5;k++) for(int i=0;i<8;i++) {
        s->palette[k*8+i+1]=ramp.palette[ramp.lumps[0].indices[k*8+i]];
        if(s->palette_maps) { s->palette_maps[k].id=k; s->palette_maps[k].indices[i+1]=k*8+i+1; }
    }
    memcpy(s->source_palette,s->palette,sizeof(s->palette)); R_FreeSprite(&ramp);return true;
}
