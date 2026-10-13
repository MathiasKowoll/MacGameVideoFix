/* See shaderfix.h. A C port of the original Python scripts; the app's tests check
 * that it still produces the verified, byte-identical shaders. */
#include "sm2-shaderfix.h"

#include <ctype.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint64_t sf_shader_hash(const void *data, size_t size)
{
    const uint8_t *p = data;
    uint64_t h = 0xcbf29ce484222325ull;
    size_t i;

    for (i = 0; i < size; i++)
        h = (h * 0x100000001b3ull) ^ p[i];
    return h;
}

int sf_find_container(const uint8_t *data, size_t size, uint64_t hash, size_t *offset, size_t *found_size)
{
    size_t i;

    for (i = 0; i + 32 <= size; i++)
    {
        uint32_t total;

        if (data[i] != 'D' || memcmp(data + i, "DXBC", 4))
            continue;
        /* "DXBC", 16-byte digest, version 1.0, total size, part count */
        if (data[i + 20] != 1 || data[i + 21] || data[i + 22] || data[i + 23])
            continue;
        memcpy(&total, data + i + 24, 4);
        if (total < 32 || total > size - i)
            continue;
        if (sf_shader_hash(data + i, total) == hash)
        {
            *offset = i;
            *found_size = total;
            return 1;
        }
    }
    return 0;
}

/* ---- small helpers: a growable line list and a string builder */

struct lines { char **v; size_t n, cap; };

static int lines_push(struct lines *l, char *s)
{
    if (!s)
        return 0;
    if (l->n == l->cap)
    {
        size_t cap = l->cap ? l->cap * 2 : 1024;
        char **v = realloc(l->v, cap * sizeof(*v));
        if (!v)
            return 0;
        l->v = v;
        l->cap = cap;
    }
    l->v[l->n++] = s;
    return 1;
}

static void lines_free(struct lines *l)
{
    size_t i;

    for (i = 0; i < l->n; i++)
        free(l->v[i]);
    free(l->v);
    memset(l, 0, sizeof(*l));
}

static char *dup_range(const char *s, size_t n)
{
    char *d = malloc(n + 1);

    if (d)
    {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

/* Splits on '\n' exactly like Python's str.split('\n'). */
static int split_lines(const char *text, struct lines *out)
{
    const char *start = text, *nl;

    while ((nl = strchr(start, '\n')))
    {
        if (!lines_push(out, dup_range(start, (size_t)(nl - start))))
            return 0;
        start = nl + 1;
    }
    return lines_push(out, dup_range(start, strlen(start)));
}

static char *join_lines(const struct lines *l)
{
    size_t i, len = 0, at = 0;
    char *s;

    for (i = 0; i < l->n; i++)
        len += strlen(l->v[i]) + 1;
    if (!(s = malloc(len + 1)))
        return NULL;
    for (i = 0; i < l->n; i++)
    {
        size_t n = strlen(l->v[i]);
        memcpy(s + at, l->v[i], n);
        at += n;
        if (i + 1 < l->n)
            s[at++] = '\n';
    }
    s[at] = 0;
    return s;
}

static char *format(const char *fmt, ...)
{
    va_list ap;
    int n;
    char *s;

    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0 || !(s = malloc((size_t)n + 1)))
        return NULL;
    va_start(ap, fmt);
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    va_end(ap);
    return s;
}

static char *group(const char *s, const regmatch_t *m)
{
    return dup_range(s + m->rm_so, (size_t)(m->rm_eo - m->rm_so));
}

static int in_set(char **set, size_t n, const char *s)
{
    size_t i;

    for (i = 0; i < n; i++)
        if (!strcmp(set[i], s))
            return 1;
    return 0;
}

static int starts_with(const char *s, const char *prefix)
{
    return !strncmp(s, prefix, strlen(prefix));
}

static void set_error(char *error, size_t size, const char *msg)
{
    if (error && size)
        snprintf(error, size, "%s", msg);
}

/* Python's \w and \s, in POSIX ERE */
#define ID "%[A-Za-z0-9_.]+"
#define IDF "%%[A-Za-z0-9_.]+"   /* the same, inside format() */
#define SP "[[:space:]]*"

/* Inserts `count` new lines after line `at` (takes over the strings; frees them on failure). */
static int insert_after(struct lines *l, size_t at, char **add, size_t count)
{
    size_t k;

    for (k = 0; k < count; k++)
        if (!add[k])
            goto fail;
    if (l->n + count > l->cap)
    {
        size_t cap = (l->n + count) * 2;
        char **v = realloc(l->v, cap * sizeof(*v));

        if (!v)
            goto fail;
        l->v = v;
        l->cap = cap;
    }
    memmove(l->v + at + 1 + count, l->v + at + 1, (l->n - at - 1) * sizeof(*l->v));
    memcpy(l->v + at + 1, add, count * sizeof(*add));
    l->n += count;
    return 1;
fail:
    for (k = 0; k < count; k++)
        free(add[k]);
    return 0;
}

/* Drops the line starting with `decl` (and an attribute comment right before it) when no line
 * contains `call`: the validator rejects declarations of unused operations. */
static void drop_unused_decl(struct lines *l, const char *decl, const char *call)
{
    size_t i, w = 0;

    for (i = 0; i < l->n; i++)
        if (strstr(l->v[i], call))
            return;
    for (i = 0; i < l->n; i++)
    {
        if (starts_with(l->v[i], decl))
        {
            free(l->v[i]);
            if (w > 0 && starts_with(l->v[w - 1], "; Function Attrs:"))
                free(l->v[--w]);
            continue;
        }
        l->v[w++] = l->v[i];
    }
    l->n = w;
}

/* The entry point's properties get shader flag 0x10 (EnableRawAndStructuredBuffers). */
static int set_raw_buffer_flag(struct lines *l, char *error, size_t error_size)
{
    char ep[32], props[32];
    const char *last, *p;
    size_t i;
    unsigned long long flags;

    for (i = 0; i < l->n && strncmp(l->v[i], "!dx.entryPoints = !{!", 21); i++)
        ;
    if (i == l->n || sscanf(l->v[i] + 21, "%30[0-9]", ep + 1) != 1)
        goto bad;
    ep[0] = '!';
    for (i = 0; i < l->n; i++)
        if (starts_with(l->v[i], ep) && !strncmp(l->v[i] + strlen(ep), " = !{", 5))
            break;
    if (i == l->n || !(last = strrchr(l->v[i], ',')) || strncmp(last, ", !", 3) || sscanf(last + 2, "%30[!0-9]", props) != 1
            || strcmp(last + 2 + strlen(props), "}"))
        goto bad;
    for (i = 0; i < l->n; i++)
        if (starts_with(l->v[i], props) && !strncmp(l->v[i] + strlen(props), " = !{", 5))
            break;
    if (i == l->n || l->v[i][strlen(l->v[i]) - 1] != '}')
        goto bad;
    {
        char *line = l->v[i], *body = line + strlen(props) + 5, *fixed;

        if ((p = strstr(body, "i32 0, i64 ")) && sscanf(p + 11, "%llu", &flags) == 1)
        {
            const char *after = p + 11 + strspn(p + 11, "0123456789");
            fixed = format("%.*si32 0, i64 %llu%s", (int)(p - line), line, flags | 16, after);
        }
        else
            fixed = format("%.*si32 0, i64 16, %s", (int)(body - line), line, body);
        if (!fixed)
            return 0;
        free(line);
        l->v[i] = fixed;
    }
    return 1;
bad:
    set_error(error, error_size, "entry point properties not found");
    return 0;
}

static char *typed_to_raw(const char *ir, int space, int reg, int uav, int *handles_out, int *loads_out, int *stores_out,
        char *error, size_t error_size)
{
    struct lines lines = {0}, out = {0};
    char *created[64], *annotated[64], *result = NULL, *pattern = NULL, *attr = NULL;
    size_t ncreated = 0, nannotated = 0, i;
    regex_t meta_re, bind_re, annot_re, load_re, store_re, decl_re;
    int have_meta = 0, have_bind = 0, have_annot = 0, have_load = 0, have_store = 0, have_decl = 0, found_meta = 0;
    int loads = 0, stores = 0;
    regmatch_t m[8];

    if (!split_lines(ir, &lines))
        goto oom;

    /* 1. the resource's metadata entry: TypedBuffer (10) -> RawBuffer (11); a UAV's entry also
     *    loses its element type tag */
    if (uav)
        pattern = format("^(![0-9]+ = !\\{i32 ([0-9]+), (%%[^*]+)\\* undef, !\"\", i32 %d, i32 %d, i32 1, )i32 10, "
                "(i1 [A-Za-z0-9_]+, i1 [A-Za-z0-9_]+, i1 [A-Za-z0-9_]+), ![0-9]+\\}$", space, reg);
    else
        pattern = format("^(![0-9]+ = !\\{i32 ([0-9]+), (%%[^*]+)\\* undef, !\"\", i32 %d, i32 %d, i32 1, )i32 10, i32 0, ![0-9]+\\}$",
                space, reg);
    if (!pattern || regcomp(&meta_re, pattern, REG_EXTENDED))
        goto bad_regex;
    have_meta = 1;
    for (i = 0; i < lines.n; i++)
    {
        if (!regexec(&meta_re, lines.v[i], 5, m, 0))
        {
            char *head = group(lines.v[i], &m[1]), *bits = uav ? group(lines.v[i], &m[4]) : NULL, *line = NULL;

            if (head && (!uav || bits))
                line = uav ? format("%si32 11, %s, null}", head, bits) : format("%si32 11, i32 0, null}", head);
            free(head);
            free(bits);
            if (!line)
                goto oom;
            free(lines.v[i]);
            lines.v[i] = line;
            found_meta = 1;
            break;
        }
    }
    if (!found_meta)
    {
        char msg[96];
        snprintf(msg, sizeof(msg), "typed %s space %d register %d not found in the resource metadata", uav ? "UAV" : "SRV",
                space, reg);
        set_error(error, error_size, msg);
        goto done;
    }

    /* 2. handles created for that binding (class 0 SRV, 1 UAV) */
    free(pattern);
    if (reg == 0 && space == 0 && !uav)
        pattern = format("^" SP "(" IDF ") = call %%dx\\.types\\.Handle @dx\\.op\\.createHandleFromBinding\\(i32 217, "
                "%%dx\\.types\\.ResBind (zeroinitializer|\\{ i32 0, i32 0, i32 0, i8 0 \\})");
    else
        pattern = format("^" SP "(" IDF ") = call %%dx\\.types\\.Handle @dx\\.op\\.createHandleFromBinding\\(i32 217, "
                "%%dx\\.types\\.ResBind \\{ i32 %d, i32 %d, i32 %d, i8 %d \\}", reg, reg, space, uav);
    if (!pattern || regcomp(&bind_re, pattern, REG_EXTENDED))
        goto bad_regex;
    have_bind = 1;
    for (i = 0; i < lines.n; i++)
        if (!regexec(&bind_re, lines.v[i], 2, m, 0) && ncreated < 64)
            if (!(created[ncreated++] = group(lines.v[i], &m[1])))
                goto oom;
    if (!ncreated)
    {
        set_error(error, error_size, "no handle created for the binding");
        goto done;
    }

    /* 3. their annotations: {10, 261} (typed u32) -> {11, 0} (raw); for a UAV {4106, 261} -> {4107, 0} */
    free(pattern);
    pattern = format("^(" SP "(" IDF ") = call %%dx\\.types\\.Handle @dx\\.op\\.annotateHandle\\(i32 216, "
            "%%dx\\.types\\.Handle (" IDF "), %%dx\\.types\\.ResourceProperties )\\{ i32 %d, i32 261 \\}(\\).*)$",
            uav ? 4106 : 10);
    if (!pattern || regcomp(&annot_re, pattern, REG_EXTENDED))
        goto bad_regex;
    have_annot = 1;
    for (i = 0; i < lines.n; i++)
    {
        char *source, *head, *tail, *t, *line;

        if (regexec(&annot_re, lines.v[i], 5, m, 0))
            continue;
        if (!(source = group(lines.v[i], &m[3])))
            goto oom;
        if (!in_set(created, ncreated, source))
        {
            free(source);
            continue;
        }
        free(source);
        head = group(lines.v[i], &m[1]);
        tail = group(lines.v[i], &m[4]);
        if (nannotated < 64)
            annotated[nannotated++] = group(lines.v[i], &m[2]);
        if (!head || !tail || !annotated[nannotated - 1])
            goto oom;
        /* the comment names the resource type: TypedBuffer<U32> -> ByteAddressBuffer (all occurrences) */
        while ((t = strstr(tail, "TypedBuffer<U32>")))
        {
            char *fixed = format("%.*sByteAddressBuffer%s", (int)(t - tail), tail, t + strlen("TypedBuffer<U32>"));
            free(tail);
            if (!(tail = fixed))
                goto oom;
        }
        line = format("%s{ i32 %d, i32 0 }%s", head, uav ? 4107 : 11, tail);
        free(head);
        free(tail);
        if (!line)
            goto oom;
        free(lines.v[i]);
        lines.v[i] = line;
    }

    /* 4. loads: bufferLoad(index) -> rawBufferLoad(index << 2); for a UAV also
     *    bufferStore(index, v, v, v, v) -> rawBufferStore(index << 2, v) */
    if (regcomp(&load_re, "^(" SP ")(" ID ") = call %dx\\.types\\.ResRet\\.i32 @dx\\.op\\.bufferLoad\\.i32\\(i32 68, "
            "%dx\\.types\\.Handle (" ID "), i32 ([^,]+), i32 undef\\)(.*)$", REG_EXTENDED))
        goto bad_regex;
    have_load = 1;
    if (regcomp(&store_re, "^(" SP ")call void @dx\\.op\\.bufferStore\\.i32\\(i32 69, %dx\\.types\\.Handle (" ID "), "
            "i32 ([^,]+), i32 undef, i32 ([^,]+), i32 [^,]+, i32 [^,]+, i32 [^,]+, i8 15\\)(.*)$", REG_EXTENDED))
        goto bad_regex;
    have_store = 1;
    for (i = 0; i < lines.n; i++)
    {
        char *ind, *val, *h, *idx;

        if (uav && !regexec(&store_re, lines.v[i], 6, m, 0))
        {
            if (!(h = group(lines.v[i], &m[2])))
                goto oom;
            if (in_set(annotated, nannotated, h))
            {
                int n = loads + stores + 1;

                ind = group(lines.v[i], &m[1]);
                idx = group(lines.v[i], &m[3]);
                val = group(lines.v[i], &m[4]);
                stores++;
                if (!ind || !val || !idx
                        || !lines_push(&out, format("%s%%t2r%d = shl i32 %s, 2", ind, n, idx))
                        || !lines_push(&out, format("%scall void @dx.op.rawBufferStore.i32(i32 140, %%dx.types.Handle %s, "
                                "i32 %%t2r%d, i32 undef, i32 %s, i32 undef, i32 undef, i32 undef, i8 1, i32 4)  ; "
                                "RawBufferStore(uav,index,elementOffset,value0,value1,value2,value3,mask,alignment)",
                                ind, h, n, val)))
                {
                    free(ind); free(val); free(idx); free(h);
                    goto oom;
                }
                free(ind); free(val); free(idx); free(h);
                continue;
            }
            free(h);
        }
        if (!regexec(&load_re, lines.v[i], 6, m, 0))
        {
            if (!(h = group(lines.v[i], &m[3])))
                goto oom;
            if (in_set(annotated, nannotated, h))
            {
                ind = group(lines.v[i], &m[1]);
                val = group(lines.v[i], &m[2]);
                idx = group(lines.v[i], &m[4]);
                loads++;
                if (!ind || !val || !idx
                        || !lines_push(&out, format("%s%%t2r%d = shl i32 %s, 2", ind, loads + stores, idx))
                        || !lines_push(&out, format("%s%s = call %%dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32 139, "
                                "%%dx.types.Handle %s, i32 %%t2r%d, i32 undef, i8 1, i32 4)  ; "
                                "RawBufferLoad(srv,index,elementOffset,mask,alignment)", ind, val, h, loads + stores)))
                {
                    free(ind); free(val); free(idx); free(h);
                    goto oom;
                }
                free(ind); free(val); free(idx); free(h);
                continue;
            }
            free(h);
        }
        if (!lines_push(&out, lines.v[i]))
            goto oom;
        lines.v[i] = NULL;
    }
    if (!loads && !stores)
    {
        set_error(error, error_size, "no typed loads found on the handle");
        goto done;
    }

    if (uav)
    {
        /* 5a. declare rawBufferStore after bufferStore (same attributes), mark the shader as
         *     using raw buffers, and drop bufferStore's declaration once nothing calls it */
        int has_raw_store = 0;

        for (i = 0; i < out.n; i++)
            if (starts_with(out.v[i], "declare void @dx.op.rawBufferStore.i32("))
                has_raw_store = 1;
        if (stores && !has_raw_store)
        {
            for (i = 0; i < out.n; i++)
            {
                const char *close = strrchr(out.v[i], ')');

                if (starts_with(out.v[i], "declare void @dx.op.bufferStore.i32(") && close && close[1] == ' '
                        && close[2] == '#')
                {
                    char *add[3] = { dup_range("", 0), format("; Function Attrs: nounwind"),
                            format("declare void @dx.op.rawBufferStore.i32(i32, %%dx.types.Handle, i32, i32, i32, i32, i32, "
                                   "i32, i8, i32) %s", close + 2) };

                    if (!insert_after(&out, i, add, 3))
                        goto oom;
                    break;
                }
            }
        }
        if (!set_raw_buffer_flag(&out, error, error_size))
            goto done;
        drop_unused_decl(&out, "declare void @dx.op.bufferStore.i32(", "call void @dx.op.bufferStore.i32(");
    }

    /* 5. declare rawBufferLoad (after bufferLoad's declaration, with its attributes) */
    if (loads)
    {
        int has_raw = 0;

        for (i = 0; i < out.n; i++)
        {
            if (starts_with(out.v[i], "declare %dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32("))
                has_raw = 1;
        }
        if (regcomp(&decl_re, "^declare %dx\\.types\\.ResRet\\.i32 @dx\\.op\\.bufferLoad\\.i32\\(i32, %dx\\.types\\.Handle, i32, i32\\) (#[0-9]+)$",
                REG_EXTENDED))
            goto bad_regex;
        have_decl = 1;
        if (!has_raw)
        {
            for (i = 0; i < out.n; i++)
            {
                if (!regexec(&decl_re, out.v[i], 2, m, 0))
                {
                    char *raw;
                    size_t k;

                    if (!(attr = group(out.v[i], &m[1])))
                        goto oom;
                    raw = format("declare %%dx.types.ResRet.i32 @dx.op.rawBufferLoad.i32(i32, %%dx.types.Handle, i32, i32, i8, i32) %s", attr);
                    /* insert "", "; Function Attrs: nounwind readonly", raw after line i */
                    if (!raw || !lines_push(&out, dup_range("", 0)) || !lines_push(&out, dup_range("", 0)) || !lines_push(&out, dup_range("", 0)))
                    {
                        free(raw);
                        goto oom;
                    }
                    free(out.v[out.n - 1]);
                    free(out.v[out.n - 2]);
                    free(out.v[out.n - 3]);
                    for (k = out.n - 1; k > i + 3; k--)
                        out.v[k] = out.v[k - 3];
                    out.v[i + 1] = dup_range("", 0);
                    out.v[i + 2] = format("; Function Attrs: nounwind readonly");
                    out.v[i + 3] = raw;
                    if (!out.v[i + 1] || !out.v[i + 2])
                        goto oom;
                    break;
                }
            }
        }

    }

    /* 6. drop bufferLoad's declaration (and its attribute comment) once nothing calls it */
    drop_unused_decl(&out, "declare %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(", "call %dx.types.ResRet.i32 @dx.op.bufferLoad.i32(");

    if (!(result = join_lines(&out)))
        goto oom;
    if (handles_out)
        *handles_out = (int)nannotated;
    if (loads_out)
        *loads_out = loads;
    if (stores_out)
        *stores_out = stores;
    goto done;

bad_regex:
    set_error(error, error_size, "internal error: bad pattern");
    goto done;
oom:
    set_error(error, error_size, "out of memory");
    free(result);
    result = NULL;
done:
    if (have_meta) regfree(&meta_re);
    if (have_bind) regfree(&bind_re);
    if (have_annot) regfree(&annot_re);
    if (have_load) regfree(&load_re);
    if (have_store) regfree(&store_re);
    if (have_decl) regfree(&decl_re);
    for (i = 0; i < ncreated; i++) free(created[i]);
    for (i = 0; i < nannotated; i++) free(annotated[i]);
    free(pattern);
    free(attr);
    lines_free(&lines);
    lines_free(&out);
    return result;
}

char *sf_typed_to_raw(const char *ir, int space, int reg, int *handles, int *loads, char *error, size_t error_size)
{
    return typed_to_raw(ir, space, reg, 0, handles, loads, NULL, error, error_size);
}

char *sf_typed_to_raw_uav(const char *ir, int space, int reg, int *handles, int *loads, int *stores,
        char *error, size_t error_size)
{
    return typed_to_raw(ir, space, reg, 1, handles, loads, stores, error, error_size);
}

/* %N -> %vN where N is followed by a non-word character (Python's %(\d+)\b) */
static char *rename_locals(const char *s)
{
    size_t len = strlen(s), cap = len * 2 + 16, at = 0, i = 0;
    char *d = malloc(cap);

    if (!d)
        return NULL;
    while (i < len)
    {
        if (s[i] == '%' && isdigit((unsigned char)s[i + 1]))
        {
            size_t j = i + 1;

            while (isdigit((unsigned char)s[j]))
                j++;
            if (!(isalnum((unsigned char)s[j]) || s[j] == '_'))
            {
                if (at + (j - i) + 2 >= cap)
                {
                    char *nd = realloc(d, cap = cap * 2 + (j - i) + 16);
                    if (!nd) { free(d); return NULL; }
                    d = nd;
                }
                d[at++] = '%';
                d[at++] = 'v';
                memcpy(d + at, s + i + 1, j - i - 1);
                at += j - i - 1;
                i = j;
                continue;
            }
        }
        if (at + 2 >= cap)
        {
            char *nd = realloc(d, cap *= 2);
            if (!nd) { free(d); return NULL; }
            d = nd;
        }
        d[at++] = s[i++];
    }
    d[at] = 0;
    return d;
}

char *sf_name_values(const char *ir)
{
    struct lines lines = {0}, out = {0};
    char *result = NULL;
    int inside = 0;
    size_t i;

    if (!split_lines(ir, &lines))
        goto done;
    for (i = 0; i < lines.n; i++)
    {
        const char *line = lines.v[i];

        if (starts_with(line, "define "))
        {
            inside = 1;
            if (!lines_push(&out, dup_range(line, strlen(line))) || !lines_push(&out, dup_range("v0:", 3)))
                goto done;
            continue;
        }
        if (inside && !strcmp(line, "}"))
            inside = 0;
        if (inside)
        {
            if (starts_with(line, "; <label>:") && isdigit((unsigned char)line[10]))
            {
                const char *p = line + 10;
                size_t n = 0;
                char *rest;

                while (isdigit((unsigned char)p[n]))
                    n++;
                if (!(rest = rename_locals(p + n)) || !lines_push(&out, format("v%.*s:%s", (int)n, p, rest)))
                {
                    free(rest);
                    goto done;
                }
                free(rest);
                continue;
            }
            if (!lines_push(&out, rename_locals(line)))
                goto done;
        }
        else if (!lines_push(&out, dup_range(line, strlen(line))))
            goto done;
    }
    result = join_lines(&out);
done:
    lines_free(&lines);
    lines_free(&out);
    return result;
}
