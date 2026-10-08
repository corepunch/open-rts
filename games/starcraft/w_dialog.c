#include "sc_local.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Original (pre-Remastered) dialog records are 86 bytes. Offsets are
 * file-relative, including the root's child pointer at +66. */
bool sc_decode_dialog(const blob_t *b,sc_dialog_t *out) {
    memset(out,0,sizeof(*out));
    if(!b||b->size<86||read_u32_le(b->bytes+34)) return false;
    const uint8_t *root=b->bytes;
    out->rect=(irect_t){read_u16_le(root+4),read_u16_le(root+6),read_u16_le(root+12),read_u16_le(root+14)};
    unsigned at=read_u32_le(root+66);
    while(at) {
        if(at>b->size||b->size-at<86||out->count==SC_DIALOG_CONTROLS) return false;
        sc_control_t *c=&out->controls[out->count++]; const uint8_t *p=b->bytes+at;
        c->id=read_u16_le(p+32); c->type=read_u32_le(p+34); c->flags=read_u32_le(p+24);
        if(!c->type||c->type>14) return false;
        c->rect=(irect_t){out->rect.x+read_u16_le(p+4),out->rect.y+read_u16_le(p+6),read_u16_le(p+12),read_u16_le(p+14)};
        if(c->rect.w<1||c->rect.h<1)return false;
        c->text_offset=(ivec2_t){read_u16_le(p+70),read_u16_le(p+72)};
        if(c->flags&0x10) c->hitbox=(irect_t){read_u16_le(p+54),read_u16_le(p+56),read_u16_le(p+58),read_u16_le(p+60)};
        unsigned str=read_u32_le(p+20);
        if(str) {
            if(str>=b->size||!memchr(b->bytes+str,0,b->size-str)) return false;
            const char *text=(char*)b->bytes+str;
            if(c->flags&0x300) c->hotkey=(unsigned char)*text++;
            int n=0;
            for(;*text&&n<127;text++) {
                if((unsigned char)*text==4) c->mark_at=n;
                else if((unsigned char)*text==1) c->mark_len=n-c->mark_at;
                else if((unsigned char)*text>=32||*text=='\n') c->text[n++]=*text;
            }
        }
        unsigned smk=read_u32_le(p+66);
        while(smk) {
            if(smk>b->size||b->size-smk<30||c->movie_count==SC_CONTROL_MOVIES) return false;
            sc_movie_ref_t *m=&c->movies[c->movie_count++]; p=b->bytes+smk;
            m->flags=read_u16_le(p+4); m->offset=(ivec2_t){read_u16_le(p+18),read_u16_le(p+20)};
            str=read_u32_le(p+10);
            if(!str||str>=b->size||!memchr(b->bytes+str,0,b->size-str)) return false;
            snprintf(m->path,sizeof(m->path),"%s",(char*)b->bytes+str);
            smk=read_u32_le(p);
        }
        at=read_u32_le(b->bytes+at);
    }
    return true;
}
bool sc_dialog(const char *root,const char *path,sc_dialog_t *out) {
    blob_t b={0}; if(!sc_read(root,path,&b)) return false;
    bool ok=sc_decode_dialog(&b,out); W_FreeFile(&b); return ok;
}
