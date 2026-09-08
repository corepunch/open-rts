#include "w_wad.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

lumpinfo_t  *lumpinfo;
int          numlumps;
void       **lumpcache;

static void strupr(char *s) {
    while (*s) { *s = (char)toupper((unsigned char)*s); s++; }
}

static int filelength_fd(int handle) {
    struct stat info;
    if (fstat(handle, &info) == -1) return 0;
    return (int)info.st_size;
}

void W_Init(void) {
    numlumps = 0;
    lumpinfo = NULL;
    lumpcache = NULL;
}

static int grow_directory(int count) {
    int start = numlumps;
    numlumps += count;
    lumpinfo = realloc(lumpinfo, (size_t)numlumps * sizeof(*lumpinfo));
    free(lumpcache);
    lumpcache = calloc((size_t)numlumps, sizeof(*lumpcache));
    return start;
}

/* ── W_AddFile ─────────────────────────────────────────────────────────────
   Files with a .wad extension are WAD archives with multiple lumps.
   Other files are single lumps with the base filename for the lump name.
   Matches DOOM linuxdoom-1.10/w_wad.c behaviour. */
void W_AddFile(const char *filename) {
    wadinfo_t   header;
    int         handle;
    filelump_t *fileinfo = NULL;
    filelump_t  singleinfo;
    int         count;

    if ((handle = open(filename, O_RDONLY)) == -1) {
        fprintf(stderr, "W_AddFile: couldn't open %s\n", filename);
        return;
    }

    size_t flen = strlen(filename);
    if (flen < 4 || strcasecmp(filename + flen - 4, ".wad") != 0) {
        fileinfo = &singleinfo;
        singleinfo.filepos = 0;
        singleinfo.size = filelength_fd(handle);
        memset(singleinfo.name, 0, WAD_NAME_SIZE);
        const char *src = filename + flen;
        while (src != filename && *(src - 1) != '/' && *(src - 1) != '\\') src--;
        int n = 0;
        while (*src && *src != '.' && n < WAD_NAME_SIZE)
            singleinfo.name[n++] = (char)toupper((unsigned char)*src++);
        count = 1;
    } else {
        if (read(handle, &header, sizeof(header)) != (ssize_t)sizeof(header)) {
            close(handle);
            return;
        }
        if (strncmp(header.identification, "IWAD", 4) &&
            strncmp(header.identification, "PWAD", 4)) {
            fprintf(stderr, "W_AddFile: %s doesn't have IWAD or PWAD id\n", filename);
            close(handle);
            return;
        }
        count = header.numlumps;
        fileinfo = malloc((size_t)count * sizeof(*fileinfo));
        if (!fileinfo) { close(handle); return; }
        lseek(handle, header.infotableofs, SEEK_SET);
        read(handle, fileinfo, (size_t)count * sizeof(*fileinfo));
    }

    int start = grow_directory(count);
    lumpinfo_t *lp = &lumpinfo[start];

    for (int i = 0; i < count; i++, lp++, fileinfo++) {
        memset(lp, 0, sizeof(*lp));
        lp->handle   = handle;
        lp->position = fileinfo->filepos;
        lp->size     = fileinfo->size;
        lp->ns       = ns_global;
        lp->data     = NULL;
        strncpy(lp->name, fileinfo->name, WAD_NAME_SIZE);
    }

    if (flen >= 4 && strcasecmp(filename + flen - 4, ".wad") == 0)
        free(fileinfo - count);
}

/* ── W_AddLump ─────────────────────────────────────────────────────────────
   Add an in-memory lump.  The WAD takes ownership of `data`
   (will be freed by W_Close). */
int W_AddLump(const char *name, void *data, int size, wad_namespace_t ns) {
    int idx = grow_directory(1);
    lumpinfo_t *lp = &lumpinfo[idx];
    memset(lp, 0, sizeof(*lp));
    lp->handle = -1;
    lp->size   = size;
    lp->data   = data;
    lp->ns     = ns;
    strncpy(lp->name, name, WAD_NAME_SIZE);
    strupr(lp->name);
    return idx;
}

/* ── W_AddMarker ───────────────────────────────────────────────────────────
   Zero-length marker lump (S_START, S_END, F_START, …). */
int W_AddMarker(const char *name) {
    return W_AddLump(name, NULL, 0, ns_global);
}

/* ── W_CheckNumForName ─────────────────────────────────────────────────────
   Returns -1 if name not found.
   Scans backwards so a later lump overrides an earlier one (DOOM convention). */
int W_CheckNumForName(const char *name) {
    char upper[WAD_NAME_SIZE];
    memset(upper, 0, WAD_NAME_SIZE);
    strncpy(upper, name, WAD_NAME_SIZE);
    strupr(upper);

    lumpinfo_t *lp = lumpinfo + numlumps;
    while (lp-- != lumpinfo) {
        if (memcmp(lp->name, upper, WAD_NAME_SIZE) == 0)
            return (int)(lp - lumpinfo);
    }
    return -1;
}

/* ── W_GetNumForName ───────────────────────────────────────────────────────
   Calls W_CheckNumForName, but exits with error if not found. */
int W_GetNumForName(const char *name) {
    int i = W_CheckNumForName(name);
    if (i == -1) {
        fprintf(stderr, "W_GetNumForName: %s not found!\n", name);
        exit(1);
    }
    return i;
}

int W_LumpLength(int lump) {
    if (lump < 0 || lump >= numlumps) return 0;
    return lumpinfo[lump].size;
}

/* ── W_ReadLump ────────────────────────────────────────────────────────────
   Loads the lump into dest, which must be >= W_LumpLength() bytes. */
void W_ReadLump(int lump, void *dest) {
    if (lump < 0 || lump >= numlumps) return;
    lumpinfo_t *l = &lumpinfo[lump];

    if (l->data) {
        memcpy(dest, l->data, (size_t)l->size);
        return;
    }

    if (l->handle == -1) return;
    lseek(l->handle, l->position, SEEK_SET);
    ssize_t c = read(l->handle, dest, (size_t)l->size);
    if (c < l->size)
        fprintf(stderr, "W_ReadLump: only read %zd of %d on lump %d\n",
                c, l->size, lump);
}

/* ── W_CacheLumpNum ────────────────────────────────────────────────────────
   Returns a pointer to the lump data.
   In-memory lumps return their data directly.
   File-backed lumps are read into lumpcache on first access. */
void *W_CacheLumpNum(int lump) {
    if (lump < 0 || lump >= numlumps) return NULL;

    if (lumpinfo[lump].data)
        return lumpinfo[lump].data;

    if (!lumpcache[lump]) {
        lumpcache[lump] = malloc((size_t)W_LumpLength(lump));
        W_ReadLump(lump, lumpcache[lump]);
    }
    return lumpcache[lump];
}

void *W_CacheLumpName(const char *name) {
    return W_CacheLumpNum(W_GetNumForName(name));
}

int W_NumLumps(void) {
    return numlumps;
}

void W_Close(void) {
    for (int i = 0; i < numlumps; i++) {
        free(lumpinfo[i].data);
        if (lumpcache && lumpcache[i] && lumpcache[i] != lumpinfo[i].data)
            free(lumpcache[i]);
        if (lumpinfo[i].handle >= 0) {
            int h = lumpinfo[i].handle;
            lumpinfo[i].handle = -1;
            for (int j = i + 1; j < numlumps; j++)
                if (lumpinfo[j].handle == h) lumpinfo[j].handle = -1;
            close(h);
        }
    }
    free(lumpinfo);
    free(lumpcache);
    lumpinfo = NULL;
    lumpcache = NULL;
    numlumps = 0;
}
