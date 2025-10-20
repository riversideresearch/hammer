#ifndef HAMMER_COMPILER_SPECIFICS__H
#define HAMMER_COMPILER_SPECIFICS__H

#if defined(__clang__) || defined(__GNUC__)
#define H_GCC_ATTRIBUTE(x) __attribute__(x)
#else
#define H_GCC_ATTRIBUTE(x)
#endif

#define H_MSVC_DECLSPEC(x)

#endif
