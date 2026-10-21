/* lwIP port for the Anemos kernel driver.
 *
 * No C runtime, no stdio: diagnostics and asserts compile to nothing for K2.
 * Randomness is a local xorshift until the kernel entropy path is wired; the
 * CSP already owns the real RtlGenRandom seeding on the user side. */
#ifndef ANEM_KERNEL_ARCH_CC_H
#define ANEM_KERNEL_ARCH_CC_H

#include <stdint.h>

typedef uint8_t u8_t;
typedef int8_t s8_t;
typedef uint16_t u16_t;
typedef int16_t s16_t;
typedef uint32_t u32_t;
typedef int32_t s32_t;
typedef uintptr_t mem_ptr_t;

#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_USE_INCLUDES 1

#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { } while (0)

static inline u32_t anem_lwip_rand(void)
{
    static u32_t state = 0x9E3779B9u;

    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}
#define LWIP_RAND() anem_lwip_rand()

#ifndef LITTLE_ENDIAN
#define LITTLE_ENDIAN 1234
#endif
#ifndef BIG_ENDIAN
#define BIG_ENDIAN 4321
#endif
#define BYTE_ORDER LITTLE_ENDIAN

/* lwIP calls a few C library helpers. mingw declares the ctype family as
 * dllimport, which cannot resolve in a driver, so route them to local
 * implementations before <ctype.h> is seen. */
int anem_isdigit(int c);
int anem_isxdigit(int c);
int anem_islower(int c);
int anem_isspace(int c);
#define isdigit(c) anem_isdigit(c)
#define isxdigit(c) anem_isxdigit(c)
#define islower(c) anem_islower(c)
#define isspace(c) anem_isspace(c)

#endif /* ANEM_KERNEL_ARCH_CC_H */
