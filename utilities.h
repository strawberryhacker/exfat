// Author: strawberryhacker

#ifndef UTILITIES_H
#define UTILITIES_H

#include "stdint.h"
#include "stdalign.h"
#include "stdbool.h"

//--------------------------------------------------------------------------------------------------

typedef uint64_t u64;
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef int64_t s64;
typedef int32_t s32;
typedef int16_t s16;
typedef int8_t  s8;

//--------------------------------------------------------------------------------------------------

#define PACKED __attribute__((packed))

//--------------------------------------------------------------------------------------------------

typedef struct {
    char* text;
    int length;
} String;

#endif
