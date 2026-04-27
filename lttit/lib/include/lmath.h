#ifndef MATH_H
#define MATH_H

#include <stdint.h>

static inline uint8_t log2_clz32(uint32_t v)
{
    return 31 - __builtin_clz(v);
}

static inline uint8_t log2_clz64(uint64_t v)
{
    return 63 - __builtin_clzll(v);
}

static inline uint32_t next_power_of_two_u32(uint32_t x)
{
    if (x <= 1)
        return 1;
    return 1u << (log2_clz32(x - 1) + 1);
}

static inline uint64_t next_power_of_two_u64(uint64_t x)
{
    if (x <= 1)
        return 1;
    return 1ULL << (log2_clz64(x - 1) + 1);
}

static inline uint8_t next_power_of_two_index_u32(uint32_t x)
{
    if (x <= 1)
        return 0;
    return log2_clz32(x - 1) + 1;
}

static inline uint8_t next_power_of_two_index_u64(uint64_t x)
{
    if (x <= 1)
        return 0;
    return log2_clz64(x - 1) + 1;
}

static inline uint8_t math_log_partition_height_u32(uint32_t x, uint8_t part_bits)
{
    if (!x)
        return 1;
    return log2_clz32(x) / part_bits + 1;
}

static inline uint8_t math_log_partition_height_u64(uint64_t x, uint8_t part_bits)
{
    if (!x)
        return 1;
    return log2_clz64(x) / part_bits + 1;
}

#endif
