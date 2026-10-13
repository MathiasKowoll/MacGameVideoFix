/*
 * sm2-shadertool.c -- the Mac-side half of the Spider-Man 2 T-pose fix: finds the
 * two shaders in the user's own Spider-Man2.exe and rewrites their typed pool
 * reads as raw ones (sm2-shaderfix.c, by macprotips, public domain). The same
 * steps as their app's ShaderBuilder.swift; install-sm2-fix.sh runs it between
 * Microsoft's dxc -dumpbin and dxa/dxv.
 *
 *   sm2-shadertool hash <file>                         FNV-1 hash of a container
 *   sm2-shadertool extract <exe> <hash> <out.dxil>     find a DXBC container by hash
 *   sm2-shadertool rewrite <in.ll> <space> <reg> <out.ll>   typed SRV -> raw, values named
 *   sm2-shadertool rewrite-uav <in.ll> <space> <reg> <out.ll>   typed RWBuffer UAV -> raw
 *
 * Built for Apple Silicon only (M5 Macs do not have the bug):
 *   clang -O2 -arch arm64 -mmacosx-version-min=13.0 -o runtime/sm2-shadertool \
 *       runtime/sm2-shadertool.c runtime/sm2-shaderfix.c
 *
 * SPDX-License-Identifier: GPL-3.0-or-later (the rewriter itself is public domain)
 */
#include "sm2-shaderfix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void *map_file(const char *path, size_t *size)
{
    int fd = open(path, O_RDONLY);
    struct stat st;
    void *p;
    if (fd < 0 || fstat(fd, &st) || !st.st_size) { if (fd >= 0) close(fd); return NULL; }
    p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return NULL;
    *size = st.st_size;
    return p;
}

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *s;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    s = malloc(n + 1);
    if (s && fread(s, 1, n, f) == (size_t)n) s[n] = 0; else { free(s); s = NULL; }
    fclose(f);
    return s;
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "hash")) {
        size_t n; void *p = map_file(argv[2], &n);
        if (!p) return 2;
        printf("%016llx\n", (unsigned long long)sf_shader_hash(p, n));
        return 0;
    }
    if (argc == 5 && !strcmp(argv[1], "extract")) {
        size_t n, off, sz; const unsigned char *p = map_file(argv[2], &n);
        unsigned long long h = strtoull(argv[3], NULL, 16);
        FILE *o;
        if (!p) return 2;
        if (!sf_find_container(p, n, h, &off, &sz)) { fprintf(stderr, "not found\n"); return 3; }
        if (!(o = fopen(argv[4], "wb")) || fwrite(p + off, 1, sz, o) != sz) return 4;
        fclose(o);
        printf("%zu bytes at 0x%zx\n", sz, off);
        return 0;
    }
    if (argc == 6 && (!strcmp(argv[1], "rewrite") || !strcmp(argv[1], "rewrite-uav"))) {
        char err[256] = "", *ir = read_text(argv[2]), *raw, *named;
        int handles = 0, loads = 0, stores = 0;
        FILE *o;
        if (!ir) return 2;
        raw = !strcmp(argv[1], "rewrite-uav")
            ? sf_typed_to_raw_uav(ir, atoi(argv[3]), atoi(argv[4]), &handles, &loads, &stores, err, sizeof(err))
            : sf_typed_to_raw(ir, atoi(argv[3]), atoi(argv[4]), &handles, &loads, err, sizeof(err));
        if (!raw) { fprintf(stderr, "rewrite failed: %s\n", err); return 3; }
        named = sf_name_values(raw);
        if (!named || !(o = fopen(argv[5], "w"))) return 4;
        fputs(named, o);
        fclose(o);
        printf("%d handles, %d loads, %d stores changed\n", handles, loads, stores);
        return 0;
    }
    fprintf(stderr, "usage: hash|extract|rewrite\n");
    return 1;
}
