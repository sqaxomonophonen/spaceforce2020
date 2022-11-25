#ifndef COMMON_H

#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

union ivec3 {
	struct { int x,y,z; };
	int s[3];
};

#define ARRAY_LENGTH(x) (sizeof(x) / sizeof(x[0]))
#define MEMBER_SIZE(t,m) sizeof(((t *)0)->m)
#define MEMBER_OFFSET(t,m) (void*)((size_t)&(((t *)0)->m))

#define ABS(x) ((x)<0?-x:x)
#define SIGN(x) ((x)>0?1:(x)<0?-1:0)
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))

static inline int potceil(int v, int p)
{
	int m = (1 << p) - 1;
	return (v+m) & ~m;
}

#define UNREACHABLE assert(!"UNREACHABLE")
#define BANG assert(!"BANG")
#define PROGRAMMER_ERROR assert(!"PROGRAMMER ERROR")
#define TODO assert(!"TODO")

// XA(x): "expensive assert" or "debug-only assert"
#ifdef DEBUG
#define XA(x) assert(x)
#else
#define XA(x)
#endif

#define COMMON_H
#endif
