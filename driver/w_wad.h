#ifndef __W_WAD__
#define __W_WAD__

#include <stdbool.h>
#include <stdint.h>

#define WAD_NAME_SIZE 16

/* WAD on-disk structures — based on DOOM (linuxdoom-1.10) / Hexen,
   extended to 16-char lump names for Dark Colony FIN frame names. */

typedef struct {
    char identification[4];     /* "IWAD" or "PWAD" */
    int  numlumps;
    int  infotableofs;
} wadinfo_t;

typedef struct {
    int  filepos;
    int  size;
    char name[WAD_NAME_SIZE];
} filelump_t;

/* Lump namespaces — GZDoom convention (resourcefile.h).
   Each corresponds to a pair of marker lumps in the WAD directory:
     S_START  / S_END    → ns_sprites
     F_START  / F_END    → ns_flats
     C_START  / C_END    → ns_colormaps
     A_START  / A_END    → ns_acslibrary
     TX_START / TX_END   → ns_newtextures
     V_START  / V_END    → ns_strifevoices
     HI_START / HI_END   → ns_hires
     VX_START / VX_END   → ns_voxels                       */
typedef enum {
    ns_hidden = -1,
    ns_global = 0,
    ns_sprites,
    ns_flats,
    ns_colormaps,
    ns_acslibrary,
    ns_newtextures,
    ns_strifevoices,
    ns_hires,
    ns_voxels,
} wad_namespace_t;

/* Runtime lump directory entry.
   handle == -1 means in-memory lump (data is an owned malloc'd block). */
typedef struct {
    char            name[WAD_NAME_SIZE];
    int             handle;
    int             position;
    int             size;
    void           *data;
    wad_namespace_t ns;
} lumpinfo_t;

extern lumpinfo_t  *lumpinfo;
extern int          numlumps;
extern void       **lumpcache;

void  W_Init(void);
void  W_AddFile(const char *filename);
int   W_AddLump(const char *name, void *data, int size, wad_namespace_t ns);
int   W_AddMarker(const char *name);

int   W_CheckNumForName(const char *name);
int   W_GetNumForName(const char *name);

int   W_LumpLength(int lump);
void  W_ReadLump(int lump, void *dest);

void *W_CacheLumpNum(int lump);
void *W_CacheLumpName(const char *name);

int   W_NumLumps(void);
void  W_Close(void);

#endif
