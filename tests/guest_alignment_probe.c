/* Compile with the Android ARMv7 toolchain and inspect the actual machine code.
 * The original instruction in issue #7 writes a double at guest object + 0x5f3. */
#include "recomp_types.h"
void original_store(uint32_t object, double value) { *(double*)ADDR(object + 0x5f3) = value + 0.5; }
void corrected_store(uint32_t object, double value) { *(recomp_f64_una*)ADDR(object + 0x5f3) = value + 0.5; }
double corrected_load(uint32_t object) { return *(recomp_f64_una*)ADDR(object + 0x5f3); }
