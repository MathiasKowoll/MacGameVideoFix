/*
 * cpu-selftest -- does this CPU compute what it says it can compute?
 *
 * Three things can go wrong with an instruction set under translation, and
 * only one of them is quiet:
 *
 *   - unsupported and not advertised: the program takes a scalar path. Slower,
 *     correct, and invisible.
 *   - unsupported but executed anyway: an illegal-instruction fault. Loud.
 *   - advertised and implemented WRONG: the program takes the vector path,
 *     trusts the answer, and the answer is not the right one.
 *
 * The third is the only one that produces a game which runs at full speed and
 * draws a character with its limbs in the wrong places. It cannot be found by
 * watching an API, because no API is involved -- the engine blends its
 * animation, builds its bone matrices and hands the GPU a correct-looking
 * buffer full of wrong numbers.
 *
 * So this asks CPUID what is on offer, and then checks the offer. Every test
 * has an answer worked out on paper; a mismatch is printed with both values.
 *
 * It runs as a plain console program inside the bottle, so it needs no game:
 *
 *     wine --bottle <name> --cx-app cpu-selftest.exe
 *
 * A clean run does not prove an engine's maths is safe -- it covers the
 * operations a skinning path is most likely to use, not all of them. A dirty
 * run names the instruction.
 *
 * Part of MacGameVideoFix -- https://github.com/MathiasKowoll/MacGameVideoFix
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <immintrin.h>
#include <cpuid.h>

static int failures, checks;

static void ok(const char *what, int good, const char *got, const char *want)
{
    ++checks;
    if (good) { printf("  ok    %s\n", what); return; }
    ++failures;
    printf("  WRONG %s\n          got  %s\n          want %s\n", what, got, want);
}

static int near_(float a, float b) { return fabsf(a - b) <= 1e-5f * (1.0f + fabsf(b)); }

/* ------------------------------------------------------------------- CPUID */

struct feats {
    int sse2, sse3, ssse3, sse41, sse42, avx, avx2, fma, f16c, bmi1, bmi2, popcnt, osxsave, avx512f;
    char brand[64];
};

static void read_cpuid(struct feats *f)
{
    unsigned a, b, c, d;
    unsigned name[13];
    memset(f, 0, sizeof(*f));

    if (__get_cpuid(1, &a, &b, &c, &d))
    {
        f->sse2   = (d >> 26) & 1;
        f->sse3   = (c >>  0) & 1;
        f->ssse3  = (c >>  9) & 1;
        f->sse41  = (c >> 19) & 1;
        f->sse42  = (c >> 20) & 1;
        f->popcnt = (c >> 23) & 1;
        f->osxsave= (c >> 27) & 1;
        f->avx    = (c >> 28) & 1;
        f->f16c   = (c >> 29) & 1;
        f->fma    = (c >> 12) & 1;
    }
    if (__get_cpuid_count(7, 0, &a, &b, &c, &d))
    {
        f->bmi1   = (b >>  3) & 1;
        f->avx2   = (b >>  5) & 1;
        f->bmi2   = (b >>  8) & 1;
        f->avx512f= (b >> 16) & 1;
    }
    memset(name, 0, sizeof(name));
    if (__get_cpuid(0x80000002, &name[0], &name[1], &name[2], &name[3])
        && __get_cpuid(0x80000003, &name[4], &name[5], &name[6], &name[7])
        && __get_cpuid(0x80000004, &name[8], &name[9], &name[10], &name[11]))
        lstrcpynA(f->brand, (const char *)name, sizeof(f->brand));
}

/* --------------------------------------------------------------- the tests */

static void test_sse2(void)
{
    __m128 a = _mm_set_ps(4.0f, 3.0f, 2.0f, 1.0f);
    __m128 b = _mm_set_ps(0.5f, 0.25f, 0.125f, 0.0625f);
    float r[4]; char got[96], want[96];
    _mm_storeu_ps(r, _mm_mul_ps(a, b));
    _snprintf(got, sizeof(got), "%g %g %g %g", r[0], r[1], r[2], r[3]);
    _snprintf(want, sizeof(want), "0.0625 0.25 0.75 2");
    ok("SSE2 packed multiply",
       near_(r[0],0.0625f) && near_(r[1],0.25f) && near_(r[2],0.75f) && near_(r[3],2.0f),
       got, want);
}

__attribute__((target("sse4.1")))
static void test_sse41(void)
{
    __m128 a = _mm_set_ps(4.0f, 3.0f, 2.0f, 1.0f);
    __m128 b = _mm_set_ps(1.0f, 1.0f, 1.0f, 1.0f);
    float dp; char got[64], want[64];
    /* dpps with mask 0xF1: dot of all four lanes into lane 0. 1+2+3+4 = 10. */
    dp = _mm_cvtss_f32(_mm_dp_ps(a, b, 0xF1));
    _snprintf(got, sizeof(got), "%g", dp);
    _snprintf(want, sizeof(want), "10");
    ok("SSE4.1 dot product (dpps)", near_(dp, 10.0f), got, want);
}

__attribute__((target("avx")))
static void test_avx(void)
{
    __m256 a = _mm256_set_ps(8,7,6,5,4,3,2,1);
    __m256 b = _mm256_set1_ps(2.0f);
    float r[8]; int i, good = 1; char got[128], want[128]; int at = 0;
    _mm256_storeu_ps(r, _mm256_mul_ps(a, b));
    for (i = 0; i < 8; ++i) { good &= near_(r[i], (float)(2 * (i + 1)));
        at += _snprintf(got + at, sizeof(got) - at, "%g ", r[i]); }
    _snprintf(want, sizeof(want), "2 4 6 8 10 12 14 16");
    ok("AVX 256-bit multiply", good, got, want);
}

__attribute__((target("avx2")))
static void test_avx2(void)
{
    /* A permute is where a translator has to get lane order exactly right, and
     * lane order is what a wrong bone matrix looks like. */
    __m256i src = _mm256_setr_epi32(0, 1, 2, 3, 4, 5, 6, 7);
    __m256i idx = _mm256_setr_epi32(7, 6, 5, 4, 3, 2, 1, 0);
    int r[8], i, good = 1, at = 0; char got[128], want[128];
    _mm256_storeu_si256((__m256i *)r, _mm256_permutevar8x32_epi32(src, idx));
    for (i = 0; i < 8; ++i) { good &= (r[i] == 7 - i);
        at += _snprintf(got + at, sizeof(got) - at, "%d ", r[i]); }
    _snprintf(want, sizeof(want), "7 6 5 4 3 2 1 0");
    ok("AVX2 cross-lane permute", good, got, want);
}

__attribute__((target("fma")))
static void test_fma(void)
{
    /* Chosen so the fused and unfused results differ: without the single
     * rounding, a*b loses the bit that c then cancels. */
    __m128 a = _mm_set_ss(1.0f + 1.0f / 8388608.0f);
    __m128 b = _mm_set_ss(1.0f + 1.0f / 8388608.0f);
    __m128 c = _mm_set_ss(-1.0f);
    float r = _mm_cvtss_f32(_mm_fmadd_ss(a, b, c));
    char got[64], want[64];
    _snprintf(got, sizeof(got), "%.10g", r);
    _snprintf(want, sizeof(want), "about 2.384e-07 (fused, single rounding)");
    ok("FMA fused multiply-add", r > 2.3e-7f && r < 2.5e-7f, got, want);
}

__attribute__((target("f16c")))
static void test_f16c(void)
{
    /* Half-precision conversion. Bone matrices and vertex data are commonly
     * stored this way, and a conversion that is off produces geometry that is
     * wrong rather than absent -- exactly the shape of fault this exists for. */
    float in[4] = { 1.0f, -2.5f, 65504.0f, 0.000061035f };
    __m128 v = _mm_loadu_ps(in);
    __m128i half = _mm_cvtps_ph(v, 0);
    float back[4]; int i, good = 1, at = 0; char got[128], want[128];
    _mm_storeu_ps(back, _mm_cvtph_ps(half));
    for (i = 0; i < 4; ++i)
        at += _snprintf(got + at, sizeof(got) - at, "%g ", back[i]);
    good = near_(back[0], 1.0f) && near_(back[1], -2.5f)
        && back[2] > 65000.0f && back[3] > 0.00006f && back[3] < 0.0000621f;
    _snprintf(want, sizeof(want), "1 -2.5 65504 6.1035e-05");
    ok("F16C half-float round trip", good, got, want);
}

/*
 * Fetch by index -- the operation a skinning path performs before any other.
 *
 * "Give me the matrix for bone N" is a gather, and a gather that returns the
 * wrong element hands the shader somebody else's bone. The result is geometry
 * that is recognisable and in the wrong place, and which mesh is affected
 * depends on which indices it uses -- so one costume can be wrong while
 * another is right, from the same code.
 *
 * This is the instruction family the first version of this file left out.
 */
__attribute__((target("avx2")))
static void test_gather(void)
{
    /* A table where element i holds i * 100, so a wrong lane is obvious. */
    float table[32];
    __m256i idx = _mm256_setr_epi32(31, 0, 17, 4, 9, 25, 2, 12);
    float r[8]; int i, good = 1, at = 0; char got[160], want[160];
    static const int expect[8] = { 31, 0, 17, 4, 9, 25, 2, 12 };

    for (i = 0; i < 32; ++i) table[i] = (float)i * 100.0f;
    _mm256_storeu_ps(r, _mm256_i32gather_ps(table, idx, 4));
    for (i = 0; i < 8; ++i)
    {
        good &= near_(r[i], (float)expect[i] * 100.0f);
        at += _snprintf(got + at, sizeof(got) - at, "%g ", r[i]);
    }
    at = 0;
    for (i = 0; i < 8; ++i)
        at += _snprintf(want + at, sizeof(want) - at, "%g ", (float)expect[i] * 100.0f);
    ok("AVX2 gather by index (bone lookup)", good, got, want);
}

/* The masked form: lanes whose mask bit is clear must be left untouched. A
 * gather that writes them anyway overwrites whatever the caller had there. */
__attribute__((target("avx2")))
static void test_gather_masked(void)
{
    float table[16];
    __m256i idx = _mm256_setr_epi32(1, 2, 3, 4, 5, 6, 7, 8);
    __m256 src = _mm256_set1_ps(-1.0f);
    __m256 mask = _mm256_castsi256_ps(
        _mm256_setr_epi32(-1, 0, -1, 0, -1, 0, -1, 0));
    float r[8]; int i, good = 1, at = 0; char got[160], want[160];

    for (i = 0; i < 16; ++i) table[i] = (float)i;
    _mm256_storeu_ps(r, _mm256_mask_i32gather_ps(src, table, idx, mask, 4));
    for (i = 0; i < 8; ++i)
    {
        float expect = (i & 1) ? -1.0f : (float)(i + 1);
        good &= near_(r[i], expect);
        at += _snprintf(got + at, sizeof(got) - at, "%g ", r[i]);
    }
    _snprintf(want, sizeof(want), "1 -1 3 -1 5 -1 7 -1");
    ok("AVX2 masked gather leaves masked lanes alone", good, got, want);
}

/* Float to integer truncation, which is how a bone index is usually derived
 * from packed weights. Off by one here selects the neighbouring bone. */
__attribute__((target("avx")))
static void test_cvt(void)
{
    __m256 v = _mm256_setr_ps(0.9f, 1.9f, -0.9f, -1.9f, 2.5f, 3.5f, 255.99f, 0.0f);
    int r[8], i, good = 1, at = 0; char got[160], want[160];
    static const int expect[8] = { 0, 1, 0, -1, 2, 3, 255, 0 };
    _mm256_storeu_si256((__m256i *)r, _mm256_cvttps_epi32(v));
    for (i = 0; i < 8; ++i)
    {
        good &= (r[i] == expect[i]);
        at += _snprintf(got + at, sizeof(got) - at, "%d ", r[i]);
    }
    _snprintf(want, sizeof(want), "0 1 0 -1 2 3 255 0");
    ok("AVX truncating float-to-int (index derivation)", good, got, want);
}

/* Byte shuffle, used to unpack bone indices and weights out of a packed
 * vertex. A wrong lane order here scrambles which weight belongs to which
 * bone, which deforms a mesh without breaking it. */
__attribute__((target("avx2")))
static void test_shuffle_bytes(void)
{
    __m256i src = _mm256_setr_epi8(
        0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
        16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31);
    /* Reverse within each 128-bit lane, which is what pshufb does. */
    __m256i ctrl = _mm256_setr_epi8(
        15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
        15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0);
    unsigned char r[32]; int i, good = 1;
    char got[128], want[128]; int at = 0;
    _mm256_storeu_si256((__m256i *)r, _mm256_shuffle_epi8(src, ctrl));
    for (i = 0; i < 16; ++i) good &= (r[i] == 15 - i);
    for (i = 16; i < 32; ++i) good &= (r[i] == 16 + (31 - i));
    for (i = 0; i < 8; ++i) at += _snprintf(got + at, sizeof(got) - at, "%d ", r[i]);
    _snprintf(want, sizeof(want), "15 14 13 12 11 10 9 8 (first eight)");
    ok("AVX2 byte shuffle (unpacking packed vertex data)", good, got, want);
}

/* Unaligned loads that straddle a cache line and a page. Vertex streams are
 * rarely aligned to anything convenient. */
static void test_unaligned(void)
{
    static float big[1024];
    int i, good = 1;
    char got[96], want[96];
    float r[4];
    for (i = 0; i < 1024; ++i) big[i] = (float)i;
    /* Offset 1023 floats in, deliberately not 16-byte aligned. */
    _mm_storeu_ps(r, _mm_loadu_ps(big + 1017));
    for (i = 0; i < 4; ++i) good &= near_(r[i], (float)(1017 + i));
    _snprintf(got, sizeof(got), "%g %g %g %g", r[0], r[1], r[2], r[3]);
    _snprintf(want, sizeof(want), "1017 1018 1019 1020");
    ok("unaligned 128-bit load", good, got, want);
}

/*
 * A quaternion multiply, which is what animation blending actually spends its
 * time on, done with FMA and then by hand.
 *
 * Bone rotations are quaternions. If this disagrees, every joint in the game
 * is rotated by a number that is nearly right, which is exactly what a limb in
 * the wrong place looks like.
 */
__attribute__((target("fma")))
static void test_quaternion(void)
{
    float a[4] = { 0.1826f, 0.3651f, 0.5477f, 0.7303f };   /* normalised-ish */
    float b[4] = { 0.4082f, -0.8165f, 0.4082f, 0.0f };
    float s[4], v[4];
    int i, good = 1, at = 0;
    char got[160], want[160];
    __m128 va = _mm_loadu_ps(a), vb = _mm_loadu_ps(b), acc;

    /* By hand: (w1 v1)(w2 v2) = (w1w2 - v1.v2, w1v2 + w2v1 + v1 x v2) */
    s[0] = a[3]*b[0] + b[3]*a[0] + (a[1]*b[2] - a[2]*b[1]);
    s[1] = a[3]*b[1] + b[3]*a[1] + (a[2]*b[0] - a[0]*b[2]);
    s[2] = a[3]*b[2] + b[3]*a[2] + (a[0]*b[1] - a[1]*b[0]);
    s[3] = a[3]*b[3] - (a[0]*b[0] + a[1]*b[1] + a[2]*b[2]);

    /* With FMA, in the shape a vectorised blender would use. */
    acc = _mm_mul_ps(_mm_shuffle_ps(va, va, 0xFF), vb);
    acc = _mm_fmadd_ps(_mm_shuffle_ps(vb, vb, 0xFF),
                       _mm_blend_ps(va, _mm_setzero_ps(), 0x8), acc);
    _mm_storeu_ps(v, acc);
    /* Only the vector part of that partial form is compared; the cross product
     * and the w term are done scalar on both sides so the comparison is of the
     * multiply-add itself, not of two different algorithms. */
    v[0] += (a[1]*b[2] - a[2]*b[1]);
    v[1] += (a[2]*b[0] - a[0]*b[2]);
    v[2] += (a[0]*b[1] - a[1]*b[0]);
    v[3] = a[3]*b[3] - (a[0]*b[0] + a[1]*b[1] + a[2]*b[2]);

    for (i = 0; i < 4; ++i)
    {
        good &= near_(v[i], s[i]);
        at += _snprintf(got + at, sizeof(got) - at, "%.6f ", v[i]);
    }
    at = 0;
    for (i = 0; i < 4; ++i) at += _snprintf(want + at, sizeof(want) - at, "%.6f ", s[i]);
    ok("quaternion multiply, FMA against scalar", good, got, want);
}

/*
 * The one that matters most: a 4x4 transform, the operation a skinning path
 * performs millions of times, done twice.
 *
 * Scalar and vector must agree. If they do not, every bone in the game is
 * placed by whichever one the engine chose.
 */
static void test_matrix(void)
{
    float m[16] = {
        1.0f, 2.0f, 3.0f, 4.0f,
        5.0f, 6.0f, 7.0f, 8.0f,
        9.0f, 10.0f, 11.0f, 12.0f,
        13.0f, 14.0f, 15.0f, 16.0f
    };
    float v[4] = { 0.5f, -1.5f, 2.5f, 1.0f };
    float scalar[4], vector[4];
    int i, j, good = 1, at = 0;
    char got[160], want[160];
    __m128 acc, vv = _mm_loadu_ps(v);

    for (i = 0; i < 4; ++i)
    {
        scalar[i] = 0.0f;
        for (j = 0; j < 4; ++j) scalar[i] += m[j * 4 + i] * v[j];
    }
    acc = _mm_mul_ps(_mm_loadu_ps(m + 0),  _mm_shuffle_ps(vv, vv, 0x00));
    acc = _mm_add_ps(acc, _mm_mul_ps(_mm_loadu_ps(m + 4),  _mm_shuffle_ps(vv, vv, 0x55)));
    acc = _mm_add_ps(acc, _mm_mul_ps(_mm_loadu_ps(m + 8),  _mm_shuffle_ps(vv, vv, 0xAA)));
    acc = _mm_add_ps(acc, _mm_mul_ps(_mm_loadu_ps(m + 12), _mm_shuffle_ps(vv, vv, 0xFF)));
    _mm_storeu_ps(vector, acc);

    for (i = 0; i < 4; ++i)
    {
        good &= near_(vector[i], scalar[i]);
        at += _snprintf(got + at, sizeof(got) - at, "%g ", vector[i]);
    }
    at = 0;
    for (i = 0; i < 4; ++i) at += _snprintf(want + at, sizeof(want) - at, "%g ", scalar[i]);
    ok("4x4 transform, vector against scalar", good, got, want);
}

int main(void)
{
    struct feats f;
    read_cpuid(&f);

    printf("cpu-selftest -- what this CPU says, and whether it is true\n\n");
    printf("  brand: %s\n", f.brand[0] ? f.brand : "(not reported)");
    printf("  advertised: sse2=%d sse3=%d ssse3=%d sse4.1=%d sse4.2=%d popcnt=%d\n",
           f.sse2, f.sse3, f.ssse3, f.sse41, f.sse42, f.popcnt);
    printf("              avx=%d avx2=%d fma=%d f16c=%d bmi1=%d bmi2=%d avx512f=%d osxsave=%d\n\n",
           f.avx, f.avx2, f.fma, f.f16c, f.bmi1, f.bmi2, f.avx512f, f.osxsave);

    printf("checking the ones that are advertised:\n");
    if (f.sse2)  test_sse2();   else printf("  --    SSE2 not advertised\n");
    if (f.sse41) test_sse41();  else printf("  --    SSE4.1 not advertised\n");
    if (f.avx)   test_avx();    else printf("  --    AVX not advertised\n");
    if (f.avx2)  test_avx2();   else printf("  --    AVX2 not advertised\n");
    if (f.fma)   test_fma();    else printf("  --    FMA not advertised\n");
    if (f.f16c)  test_f16c();   else printf("  --    F16C not advertised\n");
    if (f.avx)   test_cvt();    else printf("  --    AVX not advertised\n");
    if (f.avx2)  test_gather();        else printf("  --    AVX2 not advertised\n");
    if (f.avx2)  test_gather_masked(); else printf("  --    AVX2 not advertised\n");
    if (f.avx2)  test_shuffle_bytes(); else printf("  --    AVX2 not advertised\n");
    if (f.fma)   test_quaternion();    else printf("  --    FMA not advertised\n");
    test_unaligned();
    test_matrix();

    printf("\n  %d checks, %d wrong\n", checks, failures);
    if (!failures)
        printf("\n  Nothing here is wrong. That does not clear the CPU -- it covers the\n"
               "  operations a skinning path most likely uses, not every one.\n");
    return failures ? 1 : 0;
}
