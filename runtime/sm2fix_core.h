/* Platform-independent pieces of the Spider-Man 2 barrier shim: shader hashing,
 * pipeline-stream parsing and config parsing. Kept free of Windows headers so the
 * logic can be unit-tested natively (see ../tests/test_core.c). */
#ifndef SM2FIX_CORE_H
#define SM2FIX_CORE_H

#include <stddef.h>
#include <stdint.h>

/* Same hash vkd3d-proton uses to identify shaders (vkd3d_shader_hash): FNV-1 style
 * over the raw DXBC/DXIL container bytes handed to CreateComputePipelineState. */
uint64_t sm2_shader_hash(const void *code, size_t size);

/* The compute shader vkd3d-proton forces a post-dispatch barrier on for
 * Spider-Man2.exe (vkd3d-proton commit cc9d6cc7, "Add missing barrier in Spider Man 2"). */
#define SM2_DEFAULT_HASH 0x324071d329f05cccull
/* The game's object-space skinning compute shader, CS_ModelSkinning. */
#define SM2_DEFAULT_SKINNING_HASH 0x3d50acdb571dce22ull

enum sm2_stream_result
{
    SM2_STREAM_COMPUTE = 0,   /* compute pipeline, CS found */
    SM2_STREAM_GRAPHICS,      /* has a graphics/mesh stage: not interesting */
    SM2_STREAM_NO_CS,         /* parsed fine, no CS present */
    SM2_STREAM_UNKNOWN,       /* hit a subobject type we can't size: gave up */
    SM2_STREAM_MALFORMED,
};

/* Walks a D3D12_PIPELINE_STATE_STREAM_DESC payload (64-bit layout) looking for the CS. */
enum sm2_stream_result sm2_stream_find_cs(const void *stream, size_t size,
        const void **cs_code, size_t *cs_size);

/* Byte size of one stream subobject of `type` (header + padding + payload), 0 if unknown. */
size_t sm2_stream_subobject_size(uint32_t type);
/* Offset of the CS subobject in a compute-only stream, or -1. */
long sm2_stream_cs_offset(const void *stream, size_t size);
/* Calls fn for every shader subobject (VS, PS, DS, HS, GS, CS, AS, MS). Returns the number
 * of subobjects walked, or -1 if the stream has a type we can't size. */
int sm2_stream_for_each_shader(const void *stream, size_t size,
        void (*fn)(void *ctx, uint32_t type, const void *code, size_t len), void *ctx);

enum sm2_mode
{
    SM2_MODE_OFF = 0,   /* pass-through, only logs */
    SM2_MODE_HASH,      /* barrier after dispatches of the listed shaders (default) */
    SM2_MODE_ALL,       /* barrier after every compute dispatch (diagnostic) */
    SM2_MODE_MAX,       /* ALL + wait for the GPU after every queue submission (diagnostic, slow) */
};

#define SM2_MAX_HASHES 32

struct sm2_config
{
    enum sm2_mode mode;
    int log_shaders;            /* log every compute shader hash seen */
    unsigned int hash_count;
    uint64_t hashes[SM2_MAX_HASHES];
    char real_d3d12[512];       /* optional explicit path of the real d3d12.dll */
    unsigned int spoof;         /* SM2_SPOOF_* bits: features to hide from the game */
    int dump_compute;           /* dump every compute shader to sm2fix-dumps */
    int dump_graphics;          /* dump every vertex/mesh shader to sm2fix-dumps */
    int minmax;                 /* enum sm2_minmax_rewrite: what to do with min/max samplers */
    int profile;                /* count dispatches per compute shader and log the top ones */
    unsigned int cpu_count;     /* 0 = untouched, else max processors the game is told about */
    int trace_queries;          /* log GPU queries (timestamps, occlusion) and the values read back */
    int probe_skin;             /* hook the game's skin-job launcher and log its gate values */
    uint32_t probe_skin_rva;    /* launcher RVA (default for the analysed build) */
    uint32_t probe_jobsys_rva;  /* job-system state RVA (flag at +0x24) */
    uint32_t probe_sb_rva;      /* stub that creates the SkinningSB buffer (sized by ModelSkinGroupMax) */
    uint32_t probe_dispatch_rva;/* function that fills SkinningSB and dispatches CS_ModelSkinning */
    uint64_t skinning_hash;     /* CS_ModelSkinning's hash (overridable for tests) */
    int trap_writer;            /* catch the code that writes a character's bone matrices (guard page) */
    uint32_t manager_rva;       /* the skinning manager object (default for the analysed build) */
    int force_anim;             /* override animation culling/freezing in the game's anim config objects */
    unsigned int force_anim_delay_ms; /* wait before the first scan */
    uint32_t anim_vtbl_rva[3];  /* AnimUpdateLOD / AnimControllerComponent / SimpleAnimComponent Prius vtables */
    unsigned int snapshot_ms;   /* probe_skin: period of the binding/palette snapshots */
    int replace_skinning;       /* swap CS_ModelSkinning for sm2fix-skinning.dxil (array-free rewrite) */
    int pool_fix;               /* huge typed R32 buffer SRVs -> raw views, + sm2fix-pool-<hash>.dxil shaders */
    unsigned int pool_fix_min;  /* typed views with more elements than this are converted (default 2^28) */
    int skip_skinning;          /* diagnostic: drop every Dispatch while CS_ModelSkinning is bound */
    unsigned int skin_budget;   /* 0 = untouched, else raise RenderBudgets.ModelSkinGroupMax to at least this */
    int fake_timestamps;        /* replace D3DMetal's GPU timestamps (60 Hz, all zero on M1-M4) with real CPU time */
    unsigned int pso_delay_ms;  /* sleep this long in every pipeline creation (imitates a cold shader cache) */
    int ziva_cpu;               /* enum sm2_ziva_cpu: which CPU code Ziva RT (faces, cloth) runs */
    uint32_t ziva_level_rva;    /* game function returning the best SIMD level (CPUID) */
    uint32_t ziva_impl_rva;     /* the global object whose vtable is the chosen implementation */
    uint32_t ziva_vtbl_rva[3];  /* ReferenceImpl, SSE4_1Impl, AVX2Impl vtables */
    unsigned int capture;       /* save inputs/outputs of this many dispatches of each capture_hashes shader */
    unsigned int capture_after_ms; /* only capture this long after the device was created (in game, not menus) */
    unsigned int capture_hash_count;
    uint64_t capture_hashes[8]; /* default: the Ziva RT face/cloth deformation shaders */
    int capture_all;            /* capture every compute shader (capture times each), not just capture_hashes */
    unsigned int capture_max;   /* stop after this many captures */
    unsigned int capture_budget_mb; /* ...or after this much data */
    unsigned int capture_view_mb;   /* skip a view's data above this size */
    unsigned int capture_total_mb;  /* per capture: skip further views above this total */
};

enum sm2_ziva_cpu { SM2_ZIVA_AUTO = 0, SM2_ZIVA_REFERENCE, SM2_ZIVA_SSE41, SM2_ZIVA_AVX2 };

/* A root signature, flattened: what each root parameter is, and for descriptor tables
 * every range (offset in the table, count, register, space, SRV/UAV/CBV/sampler). */
#define SM2_RS_MAX_PARAMS 64
#define SM2_RS_MAX_RANGES 1024
enum sm2_rs_param { SM2_RS_TABLE = 0, SM2_RS_CONSTANTS = 1, SM2_RS_CBV = 2, SM2_RS_SRV = 3, SM2_RS_UAV = 4 };
struct sm2_rs_range { uint32_t param, type, count, reg, space, offset; };  /* type: 0 SRV 1 UAV 2 CBV 3 sampler */
struct sm2_rootsig
{
    uint32_t version, num_params, num_ranges;
    uint32_t param_type[SM2_RS_MAX_PARAMS];
    uint32_t param_reg[SM2_RS_MAX_PARAMS], param_space[SM2_RS_MAX_PARAMS], param_num[SM2_RS_MAX_PARAMS];
    struct sm2_rs_range ranges[SM2_RS_MAX_RANGES];
};
/* Parses a serialized root signature (bare RTS0 or inside a DXBC container). 0 on success. */
int sm2_rootsig_parse(const void *blob, size_t size, struct sm2_rootsig *rs);
/* Number of descriptors a table parameter spans (unbounded ranges count as `unbounded`). */
uint32_t sm2_rootsig_table_size(const struct sm2_rootsig *rs, uint32_t param, uint32_t unbounded);
/* Finds where a shader register lives in a root signature. cls: 0 SRV, 1 UAV, 2 CBV.
 * Returns 0 and sets *param and *slot (descriptor offset in the table, or 0xffffffff for a
 * root descriptor); -1 if the root signature doesn't cover it. */
int sm2_rootsig_locate(const struct sm2_rootsig *rs, uint32_t cls, uint32_t space, uint32_t reg,
        uint32_t *param, uint32_t *slot);

/* The resources a shader declares (DXIL PSV0 part). type: 1 sampler, 2 CBV, 3-5 SRV
 * (typed/raw/structured), 6-9 UAV. hi is 0xffffffff for unbounded arrays. */
struct sm2_binding { uint32_t type, space, lo, hi; };
/* Returns the number of bindings written (up to max), or -1 if there is no readable PSV0. */
int sm2_shader_bindings(const void *dxbc, size_t size, struct sm2_binding *out, int max);

/* Bytes per element of a DXGI format used in typed buffer views, 0 if unknown. */
unsigned int sm2_format_size(unsigned int format);

/* Feature answers sm2fix can change, to steer the game onto a different code path. */
#define SM2_SPOOF_SM65      0x01u  /* report Shader Model 6.5 instead of 6.6 */
#define SM2_SPOOF_NO_INT64  0x02u  /* OPTIONS1.Int64ShaderOps = 0 */
#define SM2_SPOOF_NO_FP16   0x04u  /* OPTIONS4.Native16BitShaderOpsSupported = 0 */
#define SM2_SPOOF_UMA       0x08u  /* ARCHITECTURE(1).UMA = CacheCoherentUMA = 1 */
#define SM2_SPOOF_NO_RT     0x10u  /* OPTIONS5.RaytracingTier = not supported */

void sm2_config_defaults(struct sm2_config *cfg);
/* Applies one "key=value" setting. Returns 0 if the key was recognised. */
int sm2_config_set(struct sm2_config *cfg, const char *key, const char *value);
/* Applies every line of an ini-style text buffer ('#'/';' comments, [sections] ignored). */
void sm2_config_parse_text(struct sm2_config *cfg, const char *text);
int sm2_config_has_hash(const struct sm2_config *cfg, uint64_t hash);

#endif
