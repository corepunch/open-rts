/* Extract native MPQ members without converting or discarding file formats. */
#include "StormLib.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int path_for(char *out, size_t size, const char *root, const char *name) {
    if (strstr(name, "..") || name[0] == '/' || strchr(name, ':')) return 0;
    int n = snprintf(out, size, "%s/%s", root, name);
    if (n < 0 || (size_t)n >= size) return 0;
    for (char *p = out + strlen(root) + 1; *p; ++p) {
        if (*p == '\\') *p = '/';
        *p = (char)tolower((unsigned char)*p);
    }
    for (char *p = out + 1; *p; ++p) if (*p == '/') {
        *p = 0;
        if (mkdir(out, 0755) && errno != EEXIST) return 0;
        *p = '/';
    }
    return 1;
}
int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: %s archive output-directory listfile\n", argv[0]); return 2; }
    HANDLE archive = NULL;
    if (!SFileOpenArchive(argv[1], 0, MPQ_OPEN_READ_ONLY, &archive)) {
        fprintf(stderr, "Cannot open %s: %u\n", argv[1], (unsigned)GetLastError()); return 1;
    }
    if (mkdir(argv[2], 0755) && errno != EEXIST) { SFileCloseArchive(archive); return 1; }
    SFILE_FIND_DATA data;
    HANDLE search = SFileFindFirstFile(archive, "*", &data, argv[3]);
    char path[2048];
    int count = 0, failed = 0;
    if (search) do {
        if (!path_for(path, sizeof(path), argv[2], data.cFileName) ||
            !SFileExtractFile(archive, data.cFileName, path, SFILE_OPEN_FROM_MPQ)) {
            fprintf(stderr, "Cannot extract %s: %u\n", data.cFileName, (unsigned)GetLastError()); ++failed;
        } else ++count;
    } while (SFileFindNextFile(search, &data));
    if (search) SFileFindClose(search);
    SFileCloseArchive(archive);
    printf("%s: %d files extracted, %d failures -> %s\n", argv[1], count, failed, argv[2]);
    return failed || !count ? 1 : 0;
}
