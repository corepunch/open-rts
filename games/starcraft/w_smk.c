#include "sc_local.h"
#include "../../reference/libsmacker/smacker.h"
#include <stdlib.h>
#include <string.h>
bool sc_movie(const char *path,spritesheet_t *out,unsigned *frame_ms, uint32_t **palettes) {
    memset(out,0,sizeof(*out)); *palettes=NULL;
    smk movie=smk_open_file(path,SMK_MODE_MEMORY); if(!movie) return false;
    unsigned long count=0,w=0,h=0; double us=0; bool ok=false;
    if(smk_info_all(movie,NULL,&count,&us)<0||smk_info_video(movie,&w,&h,NULL)<0||
       !count||count>1024||!w||!h||w>640||h>480||!R_AllocSpriteCells(out,(int)count)) goto done;
    *palettes=calloc(count,256*sizeof(uint32_t)); if(!*palettes) goto done;
    out->indexed=true; out->frame_size=(isize2_t){(int)w,(int)h};
    *frame_ms=(unsigned)(us/1000); if(!*frame_ms) *frame_ms=1;
    smk_enable_video(movie,1);
    if(smk_first(movie)<0) goto done;
    for(unsigned long i=0;i<count;i++) {
        const unsigned char *p=smk_get_palette(movie);
        for(int c=1;c<256;c++) (*palettes)[i*256+c]=0xff000000u|p[c*3]<<16|p[c*3+1]<<8|p[c*3+2];
        out->lumps[i].indices=malloc(w*h); if(!out->lumps[i].indices) goto done;
        memcpy(out->lumps[i].indices,smk_get_video(movie),w*h);
        out->cells[i].rect=out->cells[i].bounds=(irect_t){0,0,(int)w,(int)h};
        if(i+1<count && smk_next(movie)<0) goto done;
    }
    /* StarCraft menu movies use index 0 as their transparent background. */
    memcpy(out->palette,*palettes,sizeof(out->palette));
    memcpy(out->source_palette,out->palette,sizeof(out->palette));
    ok=true;
done:
    smk_close(movie); if(!ok) { R_FreeSprite(out); free(*palettes); *palettes=NULL; } return ok;
}
