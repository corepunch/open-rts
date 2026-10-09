/* Extract native MPQ members without converting or discarding file formats. */
#include "StormLib.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

/* SCM/SCX files are nested MPQs. Keep them and extract their native CHK
 * under a matching folder so the runtime needs no archive dependency. */
static int extract_maps(const char *source, const char *root, const char *rel) {
    char path[2048];
    int n = snprintf(path, sizeof(path), "%s/%s", source, rel);
    if (n < 0 || (size_t)n >= sizeof(path)) return -1;
    DIR *dir = opendir(path);
    if (!dir) { fprintf(stderr, "Cannot list %s: %s\n", path, strerror(errno)); return -1; }
    struct dirent *entry;
    int count = 0, failed = 0;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        char child[1024];
        n = snprintf(child, sizeof(child), "%s%s%s", rel, rel[0] ? "/" : "", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(child)) { failed = 1; continue; }
        n = snprintf(path, sizeof(path), "%s/%s", source, child);
        if (n < 0 || (size_t)n >= sizeof(path)) { failed = 1; continue; }
        struct stat st;
        if (lstat(path, &st)) { failed = 1; continue; }
        if (S_ISDIR(st.st_mode)) {
            int maps = extract_maps(source, root, child);
            if (maps < 0) failed = 1;
            else count += maps;
            continue;
        }
        if (!S_ISREG(st.st_mode)) continue;
        char *ext = strrchr(child, '.');
        if (!ext || (strcasecmp(ext, ".scm") && strcasecmp(ext, ".scx"))) continue;
        HANDLE archive = NULL;
        if (!SFileOpenArchive(path, 0, MPQ_OPEN_READ_ONLY, &archive)) {
            fprintf(stderr, "Cannot open %s: %u\n", path, (unsigned)GetLastError());
            failed = 1; continue;
        }
        char member[2048], output[2048];
        n = snprintf(member, sizeof(member), "%s/staredit/scenario.chk", child);
        if (n < 0 || (size_t)n >= sizeof(member) || !path_for(output, sizeof(output), root, member) ||
            !SFileExtractFile(archive, "staredit\\scenario.chk", output, SFILE_OPEN_FROM_MPQ)) {
            fprintf(stderr, "Cannot extract scenario.chk from %s: %u\n", path, (unsigned)GetLastError());
            failed = 1;
        } else ++count;
        SFileCloseArchive(archive);
    }
    closedir(dir);
    return failed ? -1 : count;
}

int main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[1], "--maps")) {
        int count = extract_maps(argv[2], argv[3], "");
        if (count < 0) return 1;
        printf("%s: %d native map CHKs extracted -> %s\n", argv[2], count, argv[3]);
        return count ? 0 : 1;
    }
    if (argc != 4) {
        fprintf(stderr, "usage: %s archive output-directory listfile\n"
                        "       %s --maps map-directory output-directory\n", argv[0], argv[0]);
        return 2;
    }
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
