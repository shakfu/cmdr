/* main for the fuzz targets without libFuzzer: run each file named, or each
 * file in each directory named, once. ctest replays the seed corpora. */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

static int replay(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return 1; }
    uint8_t *data = NULL;
    size_t used = 0, size = 0, n;
    do {
        if (used == size && !(data = realloc(data, size = size ? size * 2 : 4096))) abort();
        n = fread(data + used, 1, size - used, file);
        used += n;
    } while (n);
    int failed = ferror(file);
    fclose(file);
    if (failed) { fprintf(stderr, "%s: read failed\n", path); free(data); return 1; }
    LLVMFuzzerTestOneInput(data, used);
    free(data);
    return 0;
}

int main(int argc, char **argv)
{
    int status = 0, count = 0;
    for (int i = 1; i < argc; ++i) {
        struct stat st;
        if (stat(argv[i], &st)) { perror(argv[i]); status = 1; continue; }
        if (!S_ISDIR(st.st_mode)) { status |= replay(argv[i]); ++count; continue; }
        DIR *dir = opendir(argv[i]);
        if (!dir) { perror(argv[i]); status = 1; continue; }
        for (struct dirent *e; (e = readdir(dir));) {
            if (e->d_name[0] == '.') continue;
            char path[4096];
            snprintf(path, sizeof(path), "%s/%s", argv[i], e->d_name);
            if (!stat(path, &st) && S_ISREG(st.st_mode)) { status |= replay(path); ++count; }
        }
        closedir(dir);
    }
    printf("%d inputs\n", count);
    return status || !count;
}
