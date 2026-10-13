/* Builds the pool_fix shaders on the user's machine from their own copy of the game:
 * finds a shader in Spider-Man2.exe and rewrites its typed pool reads (or writes) into raw
 * ones (an edit of the `dxc -dumpbin` text).
 * No game data ships with the fix. */
#ifndef SHADERFIX_H
#define SHADERFIX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 64-bit FNV-1 (multiply, then xor), the hash sm2fix and vkd3d-proton use for shaders. */
uint64_t sf_shader_hash(const void *data, size_t size);

/* Finds the DXBC container with this hash in `data` (e.g. a mapped exe).
 * Returns 1 and sets *offset and *found_size if found. */
int sf_find_container(const uint8_t *data, size_t size, uint64_t hash, size_t *offset, size_t *found_size);

/* Rewrites the typed SRV at (space, reg) into a raw one in LLVM IR text from
 * `dxc -dumpbin`. Returns a malloc'd string, or NULL with a message in `error`.
 * *handles and *loads (may be NULL) receive how many handles and loads changed. */
char *sf_typed_to_raw(const char *ir, int space, int reg, int *handles, int *loads, char *error, size_t error_size);

/* The same for the typed RWBuffer<uint> UAV at (space, reg): its loads become raw loads and its
 * stores raw stores of one 32-bit word, and the shader declares raw buffer use (flag 0x10).
 * *stores (may be NULL) receives how many stores changed. */
char *sf_typed_to_raw_uav(const char *ir, int space, int reg, int *handles, int *loads, int *stores,
        char *error, size_t error_size);

/* Names every unnamed local value and block (%N -> %vN) so DXC's assembler accepts the
 * edited module. Returns a malloc'd string, or NULL on allocation failure. */
char *sf_name_values(const char *ir);

#ifdef __cplusplus
}
#endif

#endif
