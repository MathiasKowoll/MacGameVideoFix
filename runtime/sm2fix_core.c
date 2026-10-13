#include "sm2fix_core.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

uint64_t sm2_shader_hash(const void *code, size_t size)
{
    /* Matches vkd3d-proton's hash_fnv1_init / hash_fnv1_iterate_u8 exactly
     * (multiply, then xor), so hashes from its quirk tables can be reused as-is. */
    const uint8_t *p = code;
    uint64_t h = 0xcbf29ce484222325ull;
    size_t i;

    for (i = 0; i < size; i++)
        h = (h * 0x100000001b3ull) ^ p[i];
    return h;
}

/* D3D12_PIPELINE_STATE_SUBOBJECT_TYPE values we care about. */
enum
{
    SUB_ROOT_SIGNATURE = 0,
    SUB_CS = 6,
    SUB_NODE_MASK = 18,
    SUB_CACHED_PSO = 19,
    SUB_FLAGS = 20,
};

static int is_graphics_subobject(uint32_t type)
{
    /* VS..GS, stream output .. sample desc, depth-stencil1, view instancing,
     * AS/MS, depth-stencil2, rasterizer1/2. Any of these means "not compute". */
    return (type >= 1 && type <= 5) || (type >= 7 && type <= 17) || type == 21 || type == 22
            || (type >= 24 && type <= 28);
}

/* Each stream subobject is { TYPE (4 bytes); inner; } aligned to sizeof(void *),
 * as laid out by d3dx12's CD3DX12_PIPELINE_STATE_STREAM_SUBOBJECT on 64-bit. */
static size_t subobject_size(uint32_t type)
{
    switch (type)
    {
        case SUB_ROOT_SIGNATURE: return 16; /* 4 + pad + pointer */
        case SUB_CS:             return 24; /* 4 + pad + D3D12_SHADER_BYTECODE (16) */
        case SUB_NODE_MASK:      return 8;
        case SUB_CACHED_PSO:     return 24; /* 4 + pad + D3D12_CACHED_PIPELINE_STATE (16) */
        case SUB_FLAGS:          return 8;
        default:                 return 0;
    }
}

enum sm2_stream_result sm2_stream_find_cs(const void *stream, size_t size,
        const void **cs_code, size_t *cs_size)
{
    const uint8_t *base = stream;
    size_t offset = 0;
    int found = 0;

    if (!stream)
        return SM2_STREAM_MALFORMED;

    while (offset < size)
    {
        uint32_t type;
        size_t sub_size;

        if (size - offset < sizeof(uint32_t))
            return SM2_STREAM_MALFORMED;
        memcpy(&type, base + offset, sizeof(type));

        if (is_graphics_subobject(type))
            return SM2_STREAM_GRAPHICS;
        if (!(sub_size = subobject_size(type)))
            return found ? SM2_STREAM_COMPUTE : SM2_STREAM_UNKNOWN;
        if (size - offset < sub_size)
            return SM2_STREAM_MALFORMED;

        if (type == SUB_CS)
        {
            const void *code;
            uint64_t length;

            memcpy(&code, base + offset + 8, sizeof(code));
            memcpy(&length, base + offset + 16, sizeof(length));
            *cs_code = code;
            *cs_size = (size_t)length;
            found = 1;
        }
        offset += sub_size;
    }

    return found ? SM2_STREAM_COMPUTE : SM2_STREAM_NO_CS;
}

long sm2_stream_cs_offset(const void *stream, size_t size)
{
    const uint8_t *base = stream;
    size_t offset = 0;

    if (!stream)
        return -1;
    while (offset < size)
    {
        uint32_t type;
        size_t sub_size;

        if (size - offset < sizeof(uint32_t))
            return -1;
        memcpy(&type, base + offset, sizeof(type));
        if (is_graphics_subobject(type) || !(sub_size = subobject_size(type)) || size - offset < sub_size)
            return -1;
        if (type == SUB_CS)
            return (long)offset;
        offset += sub_size;
    }
    return -1;
}

/* Inner payload size/alignment of every subobject type (64-bit), for walking full
 * graphics streams. Sizes of the types mingw's d3d12.h knows are static-asserted
 * in sm2fix.c; the newer ones (26-28) are from the Agility SDK headers. */
static int inner_layout(uint32_t type, size_t *size, size_t *align)
{
    static const struct { uint16_t size, align; } table[] =
    {
        [0]  = {8, 8},    /* root signature pointer */
        [1]  = {16, 8}, [2] = {16, 8}, [3] = {16, 8}, [4] = {16, 8}, [5] = {16, 8}, [6] = {16, 8},
        [7]  = {32, 8},   /* D3D12_STREAM_OUTPUT_DESC */
        [8]  = {328, 4},  /* D3D12_BLEND_DESC */
        [9]  = {4, 4},    /* sample mask */
        [10] = {44, 4},   /* D3D12_RASTERIZER_DESC */
        [11] = {52, 4},   /* D3D12_DEPTH_STENCIL_DESC */
        [12] = {16, 8},   /* D3D12_INPUT_LAYOUT_DESC */
        [13] = {4, 4}, [14] = {4, 4},
        [15] = {36, 4},   /* D3D12_RT_FORMAT_ARRAY */
        [16] = {4, 4},
        [17] = {8, 4},    /* DXGI_SAMPLE_DESC */
        [18] = {4, 4},
        [19] = {16, 8},   /* D3D12_CACHED_PIPELINE_STATE */
        [20] = {4, 4},
        [21] = {56, 4},   /* D3D12_DEPTH_STENCIL_DESC1 */
        [22] = {24, 8},   /* D3D12_VIEW_INSTANCING_DESC */
        [24] = {16, 8}, [25] = {16, 8},  /* AS, MS */
        [26] = {60, 4},   /* D3D12_DEPTH_STENCIL_DESC2 */
        [27] = {44, 4},   /* D3D12_RASTERIZER_DESC1 */
        [28] = {40, 4},   /* D3D12_RASTERIZER_DESC2 */
    };

    if (type >= sizeof(table) / sizeof(table[0]) || !table[type].size)
        return 0;
    *size = table[type].size;
    *align = table[type].align;
    return 1;
}

size_t sm2_stream_subobject_size(uint32_t type)
{
    size_t size, align, offset;

    if (!inner_layout(type, &size, &align))
        return 0;
    offset = (4 + align - 1) & ~(align - 1);
    return (offset + size + 7) & ~(size_t)7;
}

int sm2_stream_for_each_shader(const void *stream, size_t size,
        void (*fn)(void *ctx, uint32_t type, const void *code, size_t len), void *ctx)
{
    const uint8_t *base = stream;
    size_t offset = 0;
    int count = 0;

    if (!stream)
        return -1;
    while (offset + sizeof(uint32_t) <= size)
    {
        uint32_t type;
        size_t sub_size;

        memcpy(&type, base + offset, sizeof(type));
        if (!(sub_size = sm2_stream_subobject_size(type)) || size - offset < sub_size)
            return -1;
        if ((type >= 1 && type <= 6) || type == 24 || type == 25)
        {
            const void *code;
            uint64_t length;

            memcpy(&code, base + offset + 8, sizeof(code));
            memcpy(&length, base + offset + 16, sizeof(length));
            fn(ctx, type, code, (size_t)length);
        }
        offset += sub_size;
        count++;
    }
    return count;
}

void sm2_config_defaults(struct sm2_config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->mode = SM2_MODE_HASH;
    cfg->log_shaders = 0;
    cfg->hash_count = 1;
    cfg->hashes[0] = SM2_DEFAULT_HASH;
    cfg->probe_skin_rva = 0x2b42560;
    cfg->probe_jobsys_rva = 0xc2ca170;
    cfg->skinning_hash = SM2_DEFAULT_SKINNING_HASH;
    cfg->snapshot_ms = 10000;
    cfg->force_anim_delay_ms = 20000;
    cfg->manager_rva = 0xbf60300;
    cfg->anim_vtbl_rva[0] = 0x5307598;
    cfg->anim_vtbl_rva[1] = 0x84f7370;
    cfg->anim_vtbl_rva[2] = 0x84f72f8;
    cfg->probe_sb_rva = 0x2b087c0;
    cfg->probe_dispatch_rva = 0x2b08f00;
    cfg->ziva_level_rva = 0x295e6a0;
    cfg->ziva_impl_rva = 0xbca9df0;
    cfg->ziva_vtbl_rva[0] = 0x5d34d50;
    cfg->ziva_vtbl_rva[1] = 0x5d34e30;
    cfg->ziva_vtbl_rva[2] = 0x5d34dc0;
    cfg->capture_after_ms = 60000;
    cfg->capture_max = 600;
    cfg->capture_budget_mb = 600;
    cfg->capture_view_mb = 32;
    cfg->capture_total_mb = 96;
    cfg->pool_fix_min = 1u << 28;
    cfg->capture_hash_count = 6;
    cfg->capture_hashes[0] = 0xbd8cf9af848f2800ull;   /* CS_ZivaTensorSkinning */
    cfg->capture_hashes[1] = 0x9485836ba3df2694ull;   /* CS_ZivaKernelFunctions */
    cfg->capture_hashes[2] = 0xd853ba193c3f8593ull;   /* CS_ZivaScatterAdd */
    cfg->capture_hashes[3] = 0x5e3c76b17b779a4eull;   /* CS_ZivaRBFInterpolate */
    cfg->capture_hashes[4] = 0x28a308ad4d452d2eull;   /* CS_ZivaEigenSkinning */
    cfg->capture_hashes[5] = 0x66f5573a563fc86full;   /* CS_AnimVertZivaApply */
}

static int key_equals(const char *a, const char *b)
{
    for (; *a && *b; a++, b++)
    {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
    }
    return !*a && !*b;
}

static void parse_hash_list(struct sm2_config *cfg, const char *value)
{
    const char *p = value;

    cfg->hash_count = 0;
    while (*p && cfg->hash_count < SM2_MAX_HASHES)
    {
        char *end;
        uint64_t h;

        while (*p == ' ' || *p == ',' || *p == '\t')
            p++;
        if (!*p)
            break;
        h = strtoull(p, &end, 16);
        if (end == p)
            break;
        cfg->hashes[cfg->hash_count++] = h;
        p = end;
    }
}

int sm2_config_set(struct sm2_config *cfg, const char *key, const char *value)
{
    if (key_equals(key, "mode"))
    {
        if (key_equals(value, "off"))
            cfg->mode = SM2_MODE_OFF;
        else if (key_equals(value, "all"))
            cfg->mode = SM2_MODE_ALL;
        else if (key_equals(value, "max"))
            cfg->mode = SM2_MODE_MAX;
        else
            cfg->mode = SM2_MODE_HASH;
        return 0;
    }
    if (key_equals(key, "log_shaders"))
    {
        cfg->log_shaders = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "hashes"))
    {
        parse_hash_list(cfg, value);
        return 0;
    }
    if (key_equals(key, "spoof"))
    {
        static const struct { const char *name; unsigned int bit; } names[] =
        {
            {"sm65", SM2_SPOOF_SM65}, {"no_int64", SM2_SPOOF_NO_INT64}, {"no_fp16", SM2_SPOOF_NO_FP16},
            {"uma", SM2_SPOOF_UMA}, {"no_rt", SM2_SPOOF_NO_RT},
        };
        const char *p = value;

        cfg->spoof = 0;
        while (*p)
        {
            char word[32];
            size_t n = 0, i;

            while (*p == ' ' || *p == ',' || *p == '\t')
                p++;
            while (*p && *p != ' ' && *p != ',' && *p != '\t' && n < sizeof(word) - 1)
                word[n++] = *p++;
            word[n] = 0;
            for (i = 0; n && i < sizeof(names) / sizeof(names[0]); i++)
            {
                if (key_equals(word, names[i].name))
                    cfg->spoof |= names[i].bit;
            }
            while (*p && *p != ' ' && *p != ',' && *p != '\t')
                p++;
        }
        return 0;
    }
    if (key_equals(key, "probe_skin"))
    {
        cfg->probe_skin = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "probe_skin_rva"))
    {
        cfg->probe_skin_rva = (uint32_t)strtoul(value, NULL, 16);
        return 0;
    }
    if (key_equals(key, "probe_jobsys_rva"))
    {
        cfg->probe_jobsys_rva = (uint32_t)strtoul(value, NULL, 16);
        return 0;
    }
    if (key_equals(key, "probe_sb_rva"))
    {
        cfg->probe_sb_rva = (uint32_t)strtoul(value, NULL, 16);
        return 0;
    }
    if (key_equals(key, "probe_dispatch_rva"))
    {
        cfg->probe_dispatch_rva = (uint32_t)strtoul(value, NULL, 16);
        return 0;
    }
    if (key_equals(key, "trap_writer"))
    {
        cfg->trap_writer = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "manager_rva"))
    {
        cfg->manager_rva = (uint32_t)strtoul(value, NULL, 16);
        return 0;
    }
    if (key_equals(key, "force_anim_delay_ms"))
    {
        cfg->force_anim_delay_ms = (unsigned int)strtoul(value, NULL, 10);
        return 0;
    }
    if (key_equals(key, "anim_vtbl_rvas"))
    {
        /* three hex RVAs separated by commas */
        char *end;
        int k;

        for (k = 0; k < 3 && *value; k++)
        {
            cfg->anim_vtbl_rva[k] = (uint32_t)strtoul(value, &end, 16);
            value = *end == ',' ? end + 1 : end;
        }
        return 0;
    }
    if (key_equals(key, "force_anim"))
    {
        cfg->force_anim = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "snapshot_ms"))
    {
        cfg->snapshot_ms = (unsigned int)strtoul(value, NULL, 10);
        return 0;
    }
    if (key_equals(key, "ziva_cpu"))
    {
        if (key_equals(value, "reference") || !strcmp(value, "1"))
            cfg->ziva_cpu = SM2_ZIVA_REFERENCE;
        else if (key_equals(value, "sse41") || !strcmp(value, "2"))
            cfg->ziva_cpu = SM2_ZIVA_SSE41;
        else if (key_equals(value, "avx2") || !strcmp(value, "3"))
            cfg->ziva_cpu = SM2_ZIVA_AVX2;
        else
            cfg->ziva_cpu = SM2_ZIVA_AUTO;
        return 0;
    }
    if (key_equals(key, "ziva_rvas"))
    {
        /* level function, impl global, Reference/SSE4.1/AVX2 vtables: five hex RVAs */
        uint32_t v[5];
        char *end;
        int k;

        for (k = 0; k < 5 && *value; k++)
        {
            v[k] = (uint32_t)strtoul(value, &end, 16);
            value = *end == ',' ? end + 1 : end;
        }
        if (k == 5)
        {
            cfg->ziva_level_rva = v[0];
            cfg->ziva_impl_rva = v[1];
            cfg->ziva_vtbl_rva[0] = v[2];
            cfg->ziva_vtbl_rva[1] = v[3];
            cfg->ziva_vtbl_rva[2] = v[4];
        }
        return 0;
    }
    if (key_equals(key, "capture"))
    {
        cfg->capture = (unsigned int)strtoul(value, NULL, 10);
        return 0;
    }
    if (key_equals(key, "capture_all"))
    {
        cfg->capture_all = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "capture_max"))
    {
        cfg->capture_max = (unsigned int)strtoul(value, NULL, 10);
        return 0;
    }
    if (key_equals(key, "capture_budget_mb"))
    {
        cfg->capture_budget_mb = (unsigned int)strtoul(value, NULL, 10);
        return 0;
    }
    if (key_equals(key, "capture_view_mb"))
    {
        cfg->capture_view_mb = (unsigned int)strtoul(value, NULL, 10);
        return 0;
    }
    if (key_equals(key, "capture_total_mb"))
    {
        cfg->capture_total_mb = (unsigned int)strtoul(value, NULL, 10);
        return 0;
    }
    if (key_equals(key, "capture_after_ms"))
    {
        cfg->capture_after_ms = (unsigned int)strtoul(value, NULL, 10);
        return 0;
    }
    if (key_equals(key, "capture_hashes"))
    {
        /* hex hashes separated by commas */
        char *end;

        cfg->capture_hash_count = 0;
        while (*value && cfg->capture_hash_count < 8)
        {
            uint64_t h = strtoull(value, &end, 16);

            if (end == value)
                break;
            cfg->capture_hashes[cfg->capture_hash_count++] = h;
            value = *end == ',' ? end + 1 : end;
            while (*value == ' ')
                value++;
        }
        return 0;
    }
    if (key_equals(key, "skinning_hash"))
    {
        cfg->skinning_hash = strtoull(value, NULL, 16);
        return 0;
    }
    if (key_equals(key, "pool_fix"))
    {
        cfg->pool_fix = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "pool_fix_min"))
    {
        cfg->pool_fix_min = (unsigned int)strtoul(value, NULL, 0);
        return 0;
    }
    if (key_equals(key, "replace_skinning"))
    {
        cfg->replace_skinning = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "skip_skinning"))
    {
        cfg->skip_skinning = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "skin_budget"))
    {
        cfg->skin_budget = (unsigned int)strtoul(value, NULL, 10);
        return 0;
    }
    if (key_equals(key, "fake_timestamps"))
    {
        cfg->fake_timestamps = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "trace_queries"))
    {
        cfg->trace_queries = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "pso_delay_ms"))
    {
        int n = atoi(value);
        cfg->pso_delay_ms = n > 0 && n <= 1000 ? (unsigned int)n : 0;
        return 0;
    }
    if (key_equals(key, "cpu_count"))
    {
        int n = atoi(value);
        cfg->cpu_count = n > 0 && n <= 64 ? (unsigned int)n : 0;
        return 0;
    }
    if (key_equals(key, "profile"))
    {
        cfg->profile = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "dump_graphics"))
    {
        cfg->dump_graphics = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "minmax"))
    {
        /* values match enum sm2_minmax_rewrite in sm2fix_dxbc.h */
        cfg->minmax = key_equals(value, "point") ? 2 : key_equals(value, "linear") ? 1 : 0;
        return 0;
    }
    if (key_equals(key, "dump_compute"))
    {
        cfg->dump_compute = atoi(value) != 0;
        return 0;
    }
    if (key_equals(key, "real_d3d12"))
    {
        strncpy(cfg->real_d3d12, value, sizeof(cfg->real_d3d12) - 1);
        cfg->real_d3d12[sizeof(cfg->real_d3d12) - 1] = 0;
        return 0;
    }
    return -1;
}

static void trim(char *s)
{
    size_t len = strlen(s);
    size_t start = 0;

    while (len && isspace((unsigned char)s[len - 1]))
        s[--len] = 0;
    while (s[start] && isspace((unsigned char)s[start]))
        start++;
    if (start)
        memmove(s, s + start, len - start + 1);
}

void sm2_config_parse_text(struct sm2_config *cfg, const char *text)
{
    const char *line = text;

    while (line && *line)
    {
        const char *next = strchr(line, '\n');
        size_t len = next ? (size_t)(next - line) : strlen(line);
        char buf[1024], *eq;

        if (len >= sizeof(buf))
            len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = 0;
        trim(buf);

        if (buf[0] && buf[0] != '#' && buf[0] != ';' && buf[0] != '[' && (eq = strchr(buf, '=')))
        {
            *eq = 0;
            trim(buf);
            trim(eq + 1);
            sm2_config_set(cfg, buf, eq + 1);
        }
        line = next ? next + 1 : NULL;
    }
}

int sm2_config_has_hash(const struct sm2_config *cfg, uint64_t hash)
{
    unsigned int i;

    for (i = 0; i < cfg->hash_count; i++)
    {
        if (cfg->hashes[i] == hash)
            return 1;
    }
    return 0;
}

static uint32_t rd32(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int sm2_rootsig_parse(const void *blob, size_t size, struct sm2_rootsig *rs)
{
    const uint8_t *p = blob;
    uint32_t i, j, params_off;

    memset(rs, 0, sizeof(*rs));
    if (!p || size < 24)
        return -1;
    if (!memcmp(p, "DXBC", 4))
    {
        uint32_t parts, found = 0;

        if (size < 32 || (parts = rd32(p + 28)) > 64 || 32 + (size_t)parts * 4 > size)
            return -1;
        for (i = 0; i < parts; i++)
        {
            uint32_t off = rd32(p + 32 + i * 4);

            if ((size_t)off + 8 <= size && !memcmp(p + off, "RTS0", 4) && (size_t)off + 8 + rd32(p + off + 4) <= size)
            {
                size = rd32(p + off + 4);
                p += off + 8;
                found = 1;
                break;
            }
        }
        if (!found || size < 24)
            return -1;
    }
    rs->version = rd32(p);
    if (rs->version < 1 || rs->version > 3)    /* 1.0, 1.1, 1.2 */
        return -1;
    rs->num_params = rd32(p + 4);
    params_off = rd32(p + 8);
    if (rs->num_params > SM2_RS_MAX_PARAMS || (size_t)params_off + rs->num_params * 12ull > size)
        return -1;
    for (i = 0; i < rs->num_params; i++)
    {
        const uint8_t *e = p + params_off + i * 12;
        uint32_t type = rd32(e), off = rd32(e + 8);

        if (type > SM2_RS_UAV)
            return -1;
        rs->param_type[i] = type;
        if (type == SM2_RS_TABLE)
        {
            uint32_t n, ranges_off, stride = rs->version == 1 ? 20 : 24;   /* 1.1 and 1.2 ranges are alike */

            if ((size_t)off + 8 > size)
                return -1;
            n = rd32(p + off);
            ranges_off = rd32(p + off + 4);
            if ((size_t)ranges_off + (size_t)n * stride > size)
                return -1;
            for (j = 0; j < n; j++)
            {
                const uint8_t *r = p + ranges_off + j * stride;
                struct sm2_rs_range *d;

                if (rs->num_ranges >= SM2_RS_MAX_RANGES)
                    return -1;
                d = &rs->ranges[rs->num_ranges++];
                d->param = i;
                d->type = rd32(r);
                d->count = rd32(r + 4);
                d->reg = rd32(r + 8);
                d->space = rd32(r + 12);
                d->offset = rd32(r + (rs->version == 1 ? 16 : 20));
            }
        }
        else
        {
            if ((size_t)off + 12 > size)
                return -1;
            rs->param_reg[i] = rd32(p + off);
            rs->param_space[i] = rd32(p + off + 4);
            rs->param_num[i] = type == SM2_RS_CONSTANTS ? rd32(p + off + 8) : 0;
        }
    }
    /* resolve D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND (0xffffffff) */
    for (i = 0; i < rs->num_params; i++)
    {
        uint32_t next = 0;

        for (j = 0; j < rs->num_ranges; j++)
        {
            struct sm2_rs_range *r = &rs->ranges[j];

            if (r->param != i)
                continue;
            if (r->offset == 0xffffffffu)
                r->offset = next;
            next = r->count == 0xffffffffu ? r->offset : r->offset + r->count;
        }
    }
    return 0;
}

uint32_t sm2_rootsig_table_size(const struct sm2_rootsig *rs, uint32_t param, uint32_t unbounded)
{
    uint32_t j, end = 0;

    for (j = 0; j < rs->num_ranges; j++)
    {
        const struct sm2_rs_range *r = &rs->ranges[j];
        uint32_t e;

        if (r->param != param || r->type == 3)
            continue;
        e = r->offset + (r->count == 0xffffffffu ? unbounded : r->count);
        if (e > end)
            end = e;
    }
    return end;
}

unsigned int sm2_format_size(unsigned int f)
{
    if (f >= 1 && f <= 4) return 16;     /* R32G32B32A32 */
    if (f >= 5 && f <= 8) return 12;     /* R32G32B32 */
    if (f >= 9 && f <= 14) return 8;     /* R16G16B16A16 */
    if (f >= 15 && f <= 18) return 8;    /* R32G32 */
    if (f >= 23 && f <= 26) return 4;    /* R10G10B10A2, R11G11B10 */
    if (f >= 27 && f <= 32) return 4;    /* R8G8B8A8 */
    if (f >= 33 && f <= 38) return 4;    /* R16G16 */
    if (f >= 39 && f <= 43) return 4;    /* R32 */
    if (f >= 48 && f <= 52) return 2;    /* R8G8 */
    if (f >= 53 && f <= 59) return 2;    /* R16 */
    if (f >= 60 && f <= 64) return 1;    /* R8 */
    if (f >= 87 && f <= 88) return 4;    /* B8G8R8A8/X8 */
    return 0;
}

int sm2_rootsig_locate(const struct sm2_rootsig *rs, uint32_t cls, uint32_t space, uint32_t reg,
        uint32_t *param, uint32_t *slot)
{
    static const uint32_t root_type[3] = { SM2_RS_SRV, SM2_RS_UAV, SM2_RS_CBV };
    uint32_t i;

    for (i = 0; i < rs->num_ranges; i++)
    {
        const struct sm2_rs_range *r = &rs->ranges[i];

        if (r->type != cls || r->space != space || reg < r->reg
                || (r->count != 0xffffffffu && reg - r->reg >= r->count))
            continue;
        *param = r->param;
        *slot = r->offset + (reg - r->reg);
        return 0;
    }
    for (i = 0; i < rs->num_params && cls < 3; i++)
    {
        if (rs->param_type[i] == root_type[cls] && rs->param_reg[i] == reg && rs->param_space[i] == space)
        {
            *param = i;
            *slot = 0xffffffffu;
            return 0;
        }
    }
    return -1;
}

int sm2_shader_bindings(const void *dxbc, size_t size, struct sm2_binding *out, int max)
{
    const uint8_t *p = dxbc;
    uint32_t parts, i;

    if (!p || size < 32 || memcmp(p, "DXBC", 4) || (parts = rd32(p + 28)) > 64 || 32 + (size_t)parts * 4 > size)
        return -1;
    for (i = 0; i < parts; i++)
    {
        uint32_t off = rd32(p + 32 + i * 4), len, info, count, bind, k, n = 0;
        const uint8_t *d;

        if ((size_t)off + 8 > size || memcmp(p + off, "PSV0", 4))
            continue;
        len = rd32(p + off + 4);
        d = p + off + 8;
        if ((size_t)off + 8 + len > size || len < 4)
            return -1;
        info = rd32(d);
        if (4ull + info + 4 > len)
            return -1;
        count = rd32(d + 4 + info);
        if (!count)
            return 0;
        if (4ull + info + 8 > len)
            return -1;
        bind = rd32(d + 8 + info);
        if (bind < 16 || 8ull + info + 4 + (uint64_t)bind * count > len)
            return -1;
        for (k = 0; k < count && (int)n < max; k++)
        {
            const uint8_t *e = d + 12 + info + (size_t)k * bind;

            out[n].type = rd32(e);
            out[n].space = rd32(e + 4);
            out[n].lo = rd32(e + 8);
            out[n].hi = rd32(e + 12);
            n++;
        }
        return (int)n;
    }
    return -1;
}
