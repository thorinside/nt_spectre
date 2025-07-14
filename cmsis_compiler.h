/*
 * Minimal CMSIS Compiler Header for CMSIS-DSP compilation
 * This provides the basic compiler intrinsics needed by CMSIS-DSP
 */

#ifndef __CMSIS_COMPILER_H
#define __CMSIS_COMPILER_H

#include <stdint.h>

/* Define compiler intrinsics */
#define __STATIC_INLINE static inline
#define __STATIC_FORCEINLINE static inline
#define __INLINE inline
#define __WEAK __attribute__((weak))
#define __PACKED __attribute__((packed))
#define __PACKED_STRUCT struct __attribute__((packed))
#define __ALIGNED(x) __attribute__((aligned(x)))

/* ARM memory barriers */
#define __DSB() __asm volatile ("dsb 0xF":::"memory")
#define __ISB() __asm volatile ("isb 0xF":::"memory")
#define __DMB() __asm volatile ("dmb 0xF":::"memory")

/* No-op for unused variables */
#define __UNUSED __attribute__((unused))

/* ARM instruction intrinsics that CMSIS-DSP might use */
#define __CLZ(x) __builtin_clz(x)
#define __RBIT(x) __builtin_arm_rbit(x)

/* ARM DSP intrinsics - basic implementations for compilation */
#define __SMUAD(x, y) ((int32_t)(((int16_t)(x) * (int16_t)(y >> 16)) + ((int16_t)(x >> 16) * (int16_t)(y))))
#define __SMLALD(x, y, z) ((int64_t)(z) + (int32_t)__SMUAD(x, y))
#define __SSAT(x, n) ((x) > ((1 << ((n) - 1)) - 1) ? ((1 << ((n) - 1)) - 1) : (x) < -(1 << ((n) - 1)) ? -(1 << ((n) - 1)) : (x))
#define __QADD(x, y) ((int32_t)(((int64_t)(x) + (int64_t)(y)) > 0x7FFFFFFF ? 0x7FFFFFFF : ((int64_t)(x) + (int64_t)(y)) < -0x80000000 ? -0x80000000 : ((int32_t)((int64_t)(x) + (int64_t)(y)))))
#define __QSUB(x, y) ((int32_t)(((int64_t)(x) - (int64_t)(y)) > 0x7FFFFFFF ? 0x7FFFFFFF : ((int64_t)(x) - (int64_t)(y)) < -0x80000000 ? -0x80000000 : ((int32_t)((int64_t)(x) - (int64_t)(y)))))

/* For GCC ARM compiler */
#if defined(__GNUC__)
  #define __ASM            __asm
  #define __RESTRICT       __restrict
#else
  #define __ASM            
  #define __RESTRICT       
#endif

#endif /* __CMSIS_COMPILER_H */