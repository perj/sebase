// Copyright 2018 Schibsted

#ifndef COMMON_MACROS_H
#define COMMON_MACROS_H

#ifdef __clang__
/* clang is not gnu C */
#if __clang_major__ < 3
#error Clang 2 has known bugs when compiling our code.
#endif
#undef GNU_C
#else
#define GNU_C __GNUC__
#endif

/* Feature checking macros is the future */
#ifndef __has_builtin
#define __has_builtin(x) 0
#endif
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#ifndef __has_extension
#define __has_extension(x) __has_feature(x)
#endif
#ifndef __has_attribute
#define __has_attribute(x) __has_feature(attribute_##x)
#endif
#ifndef __has_include
#define __has_include(x) 0
#endif

/* C11 says this define exists but doesn't seem to on devtoolset-2 */
#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif

#ifndef UNUSED
#define UNUSED __attribute__((unused))
#endif
#ifndef __unused
// Used but not defined by libbsd
#define __unused UNUSED
#endif

#ifndef WEAK
#define WEAK __attribute__((weak))
#endif

#ifndef UNUSED_RESULT
#define UNUSED_RESULT(f) do { \
        if (f) { }; \
} while(0)
#endif

#ifndef USED
#define USED __attribute__((used))
#endif

#ifndef NORETURN
#define NORETURN __attribute__((noreturn))
#endif

#ifndef FORMAT_PRINTF
#define FORMAT_PRINTF(x, y) __attribute__((format (printf, x, y)))
#endif

#ifndef SENTINEL
#if __has_attribute(sentinel) || GNU_C >= 4
#define SENTINEL(x) __attribute__((sentinel (x)))
#else
#define SENTINEL(x)
#endif
#endif

#ifndef ARTIFICIAL
#if __has_attribute(artificial) || GNU_C >= 4
#define ARTIFICIAL __attribute__((artificial))
#else
#define ARTIFICIAL
#endif
#endif

#ifndef NONNULL
#define NONNULL(...) __attribute__((nonnull (__VA_ARGS__)))
#endif

#ifndef NONNULL_ALL
#define NONNULL_ALL __attribute__((nonnull))
#endif

#ifndef DONTUSE
#define DONTUSE(msg) __attribute__((error (msg)))
#endif

#ifndef WARN_UNUSED_RESULT
#define WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#endif

#ifndef ALLOCATOR
#define ALLOCATOR __attribute__((malloc)) WARN_UNUSED_RESULT
#endif

#ifndef VISIBILITY_HIDDEN
/* Not visible outside of current shared object. This is now default, use EXPORTED to undo. */
#define VISIBILITY_HIDDEN __attribute__((visibility ("hidden")))
#endif

#ifndef EXPORTED
/* Exported symbol, needed to be found by dlsym. */
#define EXPORTED __attribute__((visibility ("default")))
#endif

#ifndef FUNCTION_PURE
/* No side effects, can read nonvolatile memory */
#define FUNCTION_PURE __attribute__((pure)) WARN_UNUSED_RESULT
#endif

#ifndef FUNCTION_CONST
/* No side effects, no memory read, ie no pointer arguments */
#define FUNCTION_CONST __attribute__((const)) WARN_UNUSED_RESULT
#endif

#ifndef RESTRICT
#define RESTRICT __restrict__
#endif

#define __predict_true(exp)     __builtin_expect(((exp) != 0), 1)
#define __predict_false(exp)    __builtin_expect(((exp) != 0), 0)

#define MACRO_STRINGIFICATION(x) "" #x
#define MACRO_TO_STRING(x) MACRO_STRINGIFICATION(x)

/*
 * Macros used to compose internal function / structures
 * names for templates
 */
#define __TMPL_CONCATENATOR_PREFIX(x,y) _bt_ ## x ## _ ## y
#define __TMPL_CONCATENATOR(y) _bt_ ## y

#define __TMPL_TEMPLATE(name) __TMPL_CONCATENATOR(name)
#define __TMPL_FILTER(name) __TMPL_CONCATENATOR_PREFIX(fil,name)
#define __TMPL_FUNCTION(name) __TMPL_CONCATENATOR_PREFIX(fun, name)
#define __TMPL_LOOP_FILTER(name) __TMPL_CONCATENATOR_PREFIX(l, name)

/* Power of two assumed. */
#define ALIGNUP(sz, al) (((sz) + al - 1) & ~(al - 1))
#define ALIGNTOTYPE(sz, type) ALIGNUP(sz, __alignof__(type))

#ifndef TYPE_ALIGN
#if __has_attribute(aligned)
#define TYPE_ALIGN(x) __attribute__((aligned(x)))
#else
#define TYPE_ALIGN(x)
#endif
#endif

#ifndef VAR_ALIGN
#if __has_attribute(aligned)
#define VAR_ALIGN(x) __attribute__((aligned(x)))
#else
#define VAR_ALIGN(x)
#endif
#endif

#include "compat.h"

#endif /*COMMON_MACROS_H*/
