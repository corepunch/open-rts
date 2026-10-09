#include "t_local.h"
#include "starcraft.h"
#include <dirent.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"line %d: %s\n",__LINE__,#c);return 1;} } while(0)
static FILE *catalog;
static unsigned images, groups, movies, dialogs, controls, maximum, kinds[15];
static int atlas(spritesheet_t *sheet) {
    spritesheet_t palette={0};
    CHECK(W_LoadIndexedSheet("data/STARCRAFT/native/glue/palnl/backgnd.pcx",&palette));
    memcpy(sheet->palette,palette.palette,sizeof(sheet->palette));
    memcpy(sheet->source_palette,palette.palette,sizeof(sheet->source_palette));
    I_SetPalette(palette.palette);
    isize2_t cell={0};
    for(int i=0;i<sheet->numlumps;i++) {
        if(cell.w<sheet->cells[i].rect.w)cell.w=sheet->cells[i].rect.w;
        if(cell.h<sheet->cells[i].rect.h)cell.h=sheet->cells[i].rect.h;
    }
    cell.w+=8;cell.h+=16;isize2_t size={cell.w*10,cell.h*((sheet->numlumps+9)/10)};
    V_AllocScreen(size.w,size.h);V_BeginFrame(0xff202020);V_SetDrawScale(1);
    for(int i=0;i<sheet->numlumps;i++) {
        irect_t r=sheet->cells[i].rect;
        ivec2_t at={i%10*cell.w,i/10*cell.h};
        irect_t dst={at.x+4,at.y+12,r.w,r.h};
        R_DrawSprite(sheet,i,-1,&r,&dst,0,16);
        char label[8];snprintf(label,sizeof(label),"%d",i);
        V_DrawSmallText((irect_t){at.x+4,at.y+2,cell.w-8,7},label,0xffeeeeee,size);
    }
    SDL_Surface *out=SDL_CreateRGBSurfaceWithFormat(0,size.w,size.h,32,SDL_PIXELFORMAT_ARGB8888);CHECK(out);
    for(int y=0;y<size.h;y++)for(int x=0;x<size.w;x++)
        ((uint32_t*)((uint8_t*)out->pixels+y*out->pitch))[x]=vpalette[screens[0].pixels[y*size.w+x]];
    SDL_Surface *rgb=SDL_ConvertSurfaceFormat(out,SDL_PIXELFORMAT_RGB24,0);CHECK(rgb);
    CHECK(!SDL_SaveBMP(rgb,"/private/tmp/starcraft-glue-dlg.bmp"));
    SDL_FreeSurface(rgb);SDL_FreeSurface(out);R_FreeSprite(&palette);V_FreeScreen();return 0;
}
static int inspect(const char *path,const char *name) {
    const char *ext=strrchr(name,'.');
    spritesheet_t sheet={0};blob_t file={0};unsigned ms=0;uint32_t *palettes=NULL;
    if(!strcasecmp(ext,".bin")) {
        CHECK(W_ReadFile(path,&file));
        sc_dialog_t dialog;
        if(file.size<86||read_u32_le(file.bytes+34)){W_FreeFile(&file);return 0;}
        if(!sc_decode_dialog(&file,&dialog))fprintf(stderr,"rejected dialog: %s\n",name);
        CHECK(sc_decode_dialog(&file,&dialog));
        dialogs++;controls+=dialog.count;
        if(maximum<(unsigned)dialog.count)maximum=dialog.count;
        fprintf(catalog,"dialog\t%s\t%d,%d %dx%d\t%d\n",name,dialog.rect.x,dialog.rect.y,dialog.rect.w,dialog.rect.h,dialog.count);
        for(int i=0;i<dialog.count;i++) {
            const sc_control_t *c=&dialog.controls[i];kinds[c->type]++;
            fprintf(catalog,"control\t%s\t%d\t%d\t%08x\t%d,%d %dx%d\t%d,%d\t",
                    name,c->id,c->type,c->flags,c->rect.x,c->rect.y,c->rect.w,c->rect.h,c->text_offset.x,c->text_offset.y);
            for(const char *p=c->text;*p;p++) {
                if(*p=='\n')fputs("\\n",catalog);else fputc(*p,catalog);
            }
            fputc('\n',catalog);
        }
        W_FreeFile(&file);return 0;
    }
    if(!strcasecmp(ext,".pcx")) {
        CHECK(W_LoadIndexedSheet(path,&sheet));images++;
    } else if(!strcasecmp(ext,".grp")) {
        uint32_t palette[256]={0};CHECK(W_ReadFile(path,&file));
        CHECK(sc_decode_grp(&file,palette,false,&sheet));
        CHECK(sheet.numlumps==read_u16_le(file.bytes));
        CHECK(sheet.frame_size.w==read_u16_le(file.bytes+2)&&sheet.frame_size.h==read_u16_le(file.bytes+4));
        W_FreeFile(&file);groups++;
    } else if(!strcasecmp(ext,".smk")) {
        CHECK(sc_movie(path,&sheet,&ms,&palettes));CHECK(ms&&palettes);movies++;
    } else {fprintf(stderr,"unexamined glue resource: %s\n",name);return 1;}
    uint64_t hash=UINT64_C(14695981039346656037);
    for(int i=0;i<sheet.numlumps;i++) {
        const irect_t r=sheet.cells[i].rect;
        CHECK(sheet.lumps[i].indices&&r.w>0&&r.h>0);
        for(int p=0;p<r.w*r.h;p++){hash^=sheet.lumps[i].indices[p];hash*=UINT64_C(1099511628211);}
        if(!strcasecmp(ext,".grp"))
            fprintf(catalog,"frame\t%s\t%d\t%d,%d %dx%d\n",name,i,sheet.cells[i].displacement.x,sheet.cells[i].displacement.y,r.w,r.h);
    }
    fprintf(catalog,"%s\t%s\t%dx%d\t%d\t%016llx\t%u\n",ext+1,name,sheet.frame_size.w,sheet.frame_size.h,sheet.numlumps,(unsigned long long)hash,ms);
    if(!strcmp(name,"glue/palcs/arrow.smk"))
        CHECK(sheet.numlumps==5&&sheet.frame_size.w==32&&sheet.frame_size.h==32&&ms==100&&hash==UINT64_C(0x82fe4404e591d1f9));
    if(!strcmp(name,"glue/palnl/dlg.grp"))CHECK(!atlas(&sheet));
    R_FreeSprite(&sheet);free(palettes);return 0;
}
static int walk(const char *source,const char *relative,bool installed,bool rez) {
    char path[2048];snprintf(path,sizeof(path),"%s/%s",source,relative);
    DIR *dir=opendir(path);CHECK(dir);struct dirent *entry;
    while((entry=readdir(dir))) {
        if(entry->d_name[0]=='.')continue;
        char name[1024],child[2048];struct stat st;
        snprintf(name,sizeof(name),"%s/%s",relative,entry->d_name);
        snprintf(child,sizeof(child),"%s/%s",source,name);CHECK(!lstat(child,&st));
        if(S_ISDIR(st.st_mode)){CHECK(!walk(source,name,installed,rez));continue;}
        if(!S_ISREG(st.st_mode))continue;
        const char *ext=strrchr(name,'.');
        if(rez&&(!ext||strcasecmp(ext,".bin")))continue;
        if(installed) {
            char native[2048];snprintf(native,sizeof(native),"data/STARCRAFT/native/%s",name);
            if(!access(native,R_OK))continue;
        }
        CHECK(ext&&!inspect(child,name));
    }
    closedir(dir);return 0;
}
int main(void) {
    CHECK(!SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO));
    catalog=fopen("/private/tmp/starcraft-glue-assets.tsv","w");CHECK(catalog);
    CHECK(!walk("data/STARCRAFT/native","glue",false,false));
    CHECK(!walk("data/STARCRAFT/install","glue",true,false));
    CHECK(!walk("data/STARCRAFT/native","rez",false,true));
    CHECK(!walk("data/STARCRAFT/install","rez",true,true));
    CHECK(!fclose(catalog));
    CHECK(images==331&&groups==44&&movies==53&&dialogs==76&&controls==1049&&maximum==79);
    /* Rejection before video allocation used to assert in smk_close. */
    blob_t truncated={0};spritesheet_t sheet={0};uint32_t *palettes=NULL;unsigned ms=0;
    CHECK(W_ReadFile("data/STARCRAFT/install/glue/palcs/arrow.smk",&truncated));
    FILE *bad=fopen("/private/tmp/starcraft-truncated.smk","wb");CHECK(bad);
    CHECK(fwrite(truncated.bytes,1,104,bad)==104&&!fclose(bad));W_FreeFile(&truncated);
    CHECK(!sc_movie("/private/tmp/starcraft-truncated.smk",&sheet,&ms,&palettes));
    CHECK(!sheet.numlumps&&!palettes);unlink("/private/tmp/starcraft-truncated.smk");
    printf("glue: %u PCX, %u GRP, %u SMK; %u dialogs, %u controls, maximum %u\n",images,groups,movies,dialogs,controls,maximum);
    for(int i=1;i<15;i++)printf("type %d: %u\n",i,kinds[i]);
    SDL_Quit();return 0;
}
