/*
 *  TPC - Tiny Poly Compiler
 *
 *  Copyright (c) 2001-2004 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _TCC_H
#define _TCC_H

#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "config.h"

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
/* gnu headers use to #define `__attribute__` to empty for non-gcc compilers */
#ifdef __TINYC__
# undef __attribute__
#endif
#include <string.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <setjmp.h>
#include <time.h>

#ifndef _WIN32
# include <unistd.h>
# include <sys/time.h>
# ifndef CONFIG_TCC_STATIC
#  include <dlfcn.h>
# endif
/* `XXX`: need to define this to use them in non `ISOC99` context */
extern float strtof (const char *__nptr, char **__endptr);
extern long double strtold (const char *__nptr, char **__endptr);
#endif

#ifdef _WIN32
# define WIN32_LEAN_AND_MEAN 1
# ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x502 /* `AddVectoredExceptionHandler` */
# endif
# include <windows.h>
# include <io.h> /* open, close etc. */
# include <direct.h> /* getcwd */
# include <malloc.h> /* alloca */
# ifndef _MSC_VER
#  include <stdint.h>
# endif
# define inline __inline
# define snprintf _snprintf
# define vsnprintf _vsnprintf
# ifndef __GNUC__
#  define strtold (long double)strtod
#  define strtof (float)strtod
#  define strtoll _strtoi64
#  define strtoull _strtoui64
# endif
  /* some compilers don't know ldexpl and windows doesn't have long doubles anyway */
# undef ldexpl
# define ldexpl ldexp
# ifdef LIBTCC_AS_DLL
#  define LIBTCCAPI __declspec(dllexport)
#  define PUB_FUNC LIBTCCAPI
# endif
# ifdef _MSC_VER
#  pragma warning (disable : 4244)  // conversion from 'uint64_t' to 'int', possible loss of data
#  pragma warning (disable : 4267)  // conversion from 'size_t' to 'int', possible loss of data
#  pragma warning (disable : 4996)  // The POSIX name for this item is deprecated. Instead, use the ISO C and C++ conformant name
#  pragma warning (disable : 4018)  // signed/unsigned mismatch
#  pragma warning (disable : 4146)  // unary minus operator applied to unsigned type, result still unsigned
#  define ssize_t intptr_t
#  ifdef _X86_
#   define __i386__ 1
#  endif
#  ifdef _AMD64_
#   define __x86_64__ 1
#  endif
# endif
# if defined(_M_ARM64) && !defined(__aarch64__)
#  define __aarch64__ 1
# endif
# ifndef va_copy
#  define va_copy(a,b) a = b
# endif
#endif

#ifndef O_BINARY
# define O_BINARY 0
#endif

#ifndef offsetof
#ifdef __clang__ // clang -fsanitize compains about: NULL+value
#define offsetof(type, field) __builtin_offsetof(type, field)
#else
#define offsetof(type, field) ((size_t) &((type *)0)->field)
#endif
#endif

#ifndef countof
#define countof(tab) (sizeof(tab) / sizeof((tab)[0]))
#endif

#ifdef _MSC_VER
# define NORETURN __declspec(noreturn)
# define ALIGNED(x) __declspec(align(x))
# define PRINTF_LIKE(x,y)
#else
# define NORETURN __attribute__((noreturn))
# define ALIGNED(x) __attribute__((aligned(x)))
# define PRINTF_LIKE(x,y) __attribute__ ((format (printf, (x), (y))))
#endif

#ifdef _WIN32
# define IS_DIRSEP(c) (c == '/' || c == '\\')
# define IS_ABSPATH(p) (IS_DIRSEP(p[0]) || (p[0] && p[1] == ':' && IS_DIRSEP(p[2])))
# define PATHCMP stricmp
# define PATHSEP ";"
#else
# define IS_DIRSEP(c) (c == '/')
# define IS_ABSPATH(p) IS_DIRSEP(p[0])
# define PATHCMP strcmp
# define PATHSEP ":"
#endif

/* -------------------------------------------- */

/* parser debug */
/* #define `PARSE_DEBUG` */
/* preprocessor debug */
/* #define `PP_DEBUG` */
/* include file debug */
/* #define `INC_DEBUG` */
/* memory leak debug (only for single threaded usage) */
/* #define `MEM_DEBUG` 1,2,3 */
/* assembler debug */
/* #define `ASM_DEBUG` */

/* target selection */
/* #define `TCC_TARGET_I386`   *//* i386 code generator */
/* #define `TCC_TARGET_X86_64` *//* x86-64 code generator */
/* #define `TCC_TARGET_ARM`    *//* `ARMv4` code generator */
/* #define `TCC_TARGET_ARM64`  *//* `ARMv8` code generator */
/* #define `TCC_TARGET_C67`    *//* `TMS320C67xx` code generator */
/* #define `TCC_TARGET_RISCV64` *//* risc-v code generator */

/* default target is `I386` */
#if !defined(TCC_TARGET_I386) && !defined(TCC_TARGET_ARM) && \
    !defined(TCC_TARGET_ARM64) && !defined(TCC_TARGET_C67) && \
    !defined(TCC_TARGET_X86_64) && !defined(TCC_TARGET_RISCV64)
# if defined __x86_64__
#  define TCC_TARGET_X86_64
# elif defined __arm__
#  define TCC_TARGET_ARM
#  define TCC_ARM_EABI
#  define TCC_ARM_VFP
#  define TCC_ARM_HARDFLOAT
# elif defined __aarch64__
#  define TCC_TARGET_ARM64
# elif defined __riscv
#  define TCC_TARGET_RISCV64
# else
#  define TCC_TARGET_I386
# endif
# ifdef _WIN32
#  define TCC_TARGET_PE 1
# endif
# ifdef __APPLE__
#  define TCC_TARGET_MACHO 1
# endif
#endif

/* `Thread`-local storage for per-thread compiler state.  `This` is the
   groundwork for compiling in multiple threads.  `TLS` is not used on
   targets whose code generator or runtime does not support it yet.
   `Define` `TCC_TLS` as empty (for example with -`DTCC_TLS`=) to build
   without `TLS`, which is required when the result is to be used with
   tcc's -run mode. */
#ifndef TCC_TLS
# if defined TCC_TARGET_ARM || defined TCC_TARGET_C67 || defined TCC_TARGET_MACHO
#  define TCC_TLS
# elif defined _MSC_VER
#  define TCC_TLS __declspec(thread)
# elif defined __GNUC__ || defined __clang__ || defined __TINYC__
#  define TCC_TLS __thread
# else
#  define TCC_TLS
# endif
#endif

/* only native compiler supports -run */
#if defined _WIN32 == defined TCC_TARGET_PE \
    && defined __APPLE__ == defined TCC_TARGET_MACHO
# if defined __i386__ && defined TCC_TARGET_I386
#  define TCC_IS_NATIVE
# elif defined __x86_64__ && defined TCC_TARGET_X86_64
#  define TCC_IS_NATIVE
# elif defined __arm__ && defined TCC_TARGET_ARM
#  define TCC_IS_NATIVE
# elif defined __aarch64__ && defined TCC_TARGET_ARM64
#  define TCC_IS_NATIVE
# elif defined __riscv && defined __LP64__ && defined TCC_TARGET_RISCV64
#  define TCC_IS_NATIVE
# endif
#endif

#if defined CONFIG_TCC_BACKTRACE && CONFIG_TCC_BACKTRACE==0
# undef CONFIG_TCC_BACKTRACE
#else
# define CONFIG_TCC_BACKTRACE 1 /**< enable builtin stack backtraces */
#endif

#if defined CONFIG_TCC_BCHECK && CONFIG_TCC_BCHECK==0
#  undef CONFIG_TCC_BCHECK
#else
#  define CONFIG_TCC_BCHECK 1 /* enable bound checking code */
#endif

#if defined CONFIG_NEW_MACHO && CONFIG_NEW_MACHO==0
#  undef CONFIG_NEW_MACHO
#else
#  define CONFIG_NEW_MACHO 1 /* enable new macho code */
#endif

#if defined TARGETOS_OpenBSD \
    || defined TARGETOS_FreeBSD \
    || defined TARGETOS_NetBSD \
    || defined TARGETOS_FreeBSD_kernel
# define TARGETOS_BSD 1
#elif !(defined TCC_TARGET_PE || defined TCC_TARGET_MACHO)
# define TARGETOS_Linux 1 /**< for `tccdefs_`.h */
#endif

#if defined TCC_TARGET_PE || defined TCC_TARGET_MACHO
# define ELF_OBJ_ONLY /**< create elf .o but native executables */
#else
# define TCC_TARGET_UNIX 1
#endif

/* `No` ten-byte long doubles on window and macos except in
   cross-compilers made by a mingw-`GCC` */
#if defined TCC_TARGET_PE \
    || (defined TCC_TARGET_MACHO && defined TCC_TARGET_ARM64)
# define TCC_USING_DOUBLE_FOR_LDOUBLE 1
#endif

#ifdef CONFIG_TCC_PIE
# define CONFIG_TCC_PIC 1
#endif

/* support using libtcc from threads */
#ifndef CONFIG_TCC_SEMLOCK
# define CONFIG_TCC_SEMLOCK 1
#endif

/* ------------ path configuration ------------ */

#ifndef CONFIG_SYSROOT
# define CONFIG_SYSROOT ""
#endif

#if !defined CONFIG_TCCDIR && !defined _WIN32
# define CONFIG_TCCDIR "/usr/local/lib/tcc"
#endif

#ifdef CONFIG_TRIPLET
# define USE_TRIPLET(s) s "/" CONFIG_TRIPLET
# define ALSO_TRIPLET(s) USE_TRIPLET(s) PATHSEP s
#else
# define USE_TRIPLET(s) s
# define ALSO_TRIPLET(s) s
#endif

/* path to find crt1.o, crti.o and crtn.o */
#ifndef CONFIG_TCC_CRTPREFIX
# define CONFIG_TCC_CRTPREFIX \
    USE_TRIPLET(CONFIG_SYSROOT "/usr/lib")
#endif

/* `Below`: {B} is substituted by `CONFIG_TCCDIR` (rsp. -B option) */

/* system include paths */
#ifndef CONFIG_TCC_SYSINCLUDEPATHS
# if defined TCC_TARGET_PE || defined _WIN32
#  define CONFIG_TCC_SYSINCLUDEPATHS \
    "{B}/include" PATHSEP "{B}/include/winapi"
# else
#  define CONFIG_TCC_SYSINCLUDEPATHS \
    "{B}/include" PATHSEP ALSO_TRIPLET(CONFIG_SYSROOT "/usr/include")
# endif
#endif

/* library search paths */
#ifndef CONFIG_TCC_LIBPATHS
# if defined TCC_TARGET_PE || defined _WIN32
#  define CONFIG_TCC_LIBPATHS \
    "{B}/lib"
# else
#  define CONFIG_TCC_LIBPATHS \
    "{B}" PATHSEP ALSO_TRIPLET(CONFIG_SYSROOT "/usr/lib")
# endif
#endif

/* name of `ELF` interpreter */
#ifndef CONFIG_TCC_ELFINTERP
# if defined __GNU__
#  define CONFIG_TCC_ELFINTERP "/lib/ld.so"
# elif defined(TCC_TARGET_PE)
#  define CONFIG_TCC_ELFINTERP "-"
# elif defined TCC_TARGET_ARM64
#  define CONFIG_TCC_ELFINTERP "/lib/ld-linux-aarch64.so.1"
# elif defined(TCC_TARGET_X86_64)
#  define CONFIG_TCC_ELFINTERP "/lib64/ld-linux-x86-64.so.2"
# elif defined(TCC_TARGET_RISCV64)
#  define CONFIG_TCC_ELFINTERP "/lib/ld-linux-riscv64-lp64d.so.1"
# elif defined(TCC_TARGET_ARM)
#  define CONFIG_TCC_ELFINTERP "/lib/ld-linux.so.3"
#  define CONFIG_TCC_ELFINTERP_ARMHF "/lib/ld-linux-armhf.so.3"
# else
#  define CONFIG_TCC_ELFINTERP "/lib/ld-linux.so.2"
# endif
#endif

/* (target specific) libtcc1.a */
#ifndef TCC_LIBTCC1
# define TCC_LIBTCC1 "libtcc1.a"
#endif

/* <cross-prefix-to->libtcc1.a */
#ifndef CONFIG_TCC_CROSSPREFIX
# define CONFIG_TCC_CROSSPREFIX ""
#endif

/* -------------------------------------------- */

#include "libtcc.h"
#include "elf.h"
#include "stab.h"
#include "dwarf.h"

/* -------------------------------------------- */

#ifndef PUB_FUNC /* functions used by tcc.c but not in libtcc.h */
# define PUB_FUNC
#endif

#ifndef ONE_SOURCE
# define ONE_SOURCE 0
#endif

#if ONE_SOURCE
#define ST_INLN static inline
#define ST_FUNC static
#define ST_DATA static TCC_TLS /**< `Storage` specifier macro for compiler state data */
#define ST_CONST static
#else
#define ST_INLN
#define ST_FUNC
#define ST_DATA extern TCC_TLS /**< `Storage` specifier macro for compiler state data */
#define ST_CONST extern
#endif

#ifdef TCC_PROFILE /* profile all functions */
# define static
# define inline
#endif

/* -------------------------------------------- */
/* include the target specific definitions */

#define TARGET_DEFS_ONLY
#ifdef TCC_TARGET_I386
# include "i386-gen.c"
# include "i386-link.c"
#elif defined TCC_TARGET_X86_64
# include "x86_64-gen.c"
# include "x86_64-link.c"
#elif defined TCC_TARGET_ARM
# include "arm-gen.c"
# include "arm-link.c"
# include "arm-asm.c"
#elif defined TCC_TARGET_ARM64
# include "arm64-gen.c"
# include "arm64-link.c"
# include "arm64-asm.c"
#elif defined TCC_TARGET_C67
# define TCC_TARGET_COFF
# include "coff.h"
# include "c67-gen.c"
# include "c67-link.c"
#elif defined(TCC_TARGET_RISCV64)
# include "riscv64-gen.c"
# include "riscv64-link.c"
# include "riscv64-asm.c"
#else
#error unknown target
#endif
#undef TARGET_DEFS_ONLY

/* -------------------------------------------- */

#if PTR_SIZE == 8
# define ELFCLASSW ELFCLASS64
# define ElfW(type) Elf##64##_##type
# define ELFW(type) ELF##64##_##type
# define ElfW_Rel ElfW(Rela)
# define SHT_RELX SHT_RELA
# define REL_SECTION_FMT ".rela%s"
#else
# define ELFCLASSW ELFCLASS32
# define ElfW(type) Elf##32##_##type
# define ELFW(type) ELF##32##_##type
# define ElfW_Rel ElfW(Rel)
# define SHT_RELX SHT_REL
# define REL_SECTION_FMT ".rel%s"
#endif
/* target address type */
#define addr_t ElfW(Addr)
#define ElfSym ElfW(Sym)

#if PTR_SIZE == 8 && !defined TCC_TARGET_PE
# define LONG_SIZE 8
#else
# define LONG_SIZE 4
#endif

/* -------------------------------------------- */

#define INCLUDE_STACK_SIZE  32 /**< `Maximum` nested include file depth */
#define IFDEF_STACK_SIZE    64 /**< `Maximum` nested #if/#ifdef directive depth */
#define VSTACK_SIZE         512 /**< `Expression` evaluation value stack size */
#define STRING_MAX_SIZE     1024 /**< `Maximum` buffer size for strings and token buffers */
#define TOKSTR_MAX_SIZE     256 /**< `Initial` allocation size for token strings */
#define PACK_STACK_SIZE     8 /**< `Maximum` structure alignment #pragma pack stack depth */

#define TOK_HASH_SIZE       16384 /**< must be a power of two */
#define TOK_ALLOC_INCR      512  /**< must be a power of two */
#define TOK_MAX_SIZE        4 /**< token max size in int unit when stored in string */

/* token symbol management */
/**
 * @brief Token symbol entry in the global token hash table.
 */
typedef struct TokenSym {
    struct TokenSym *hash_next;   /**< Pointer to the next token symbol in the hash bucket chain. */
    struct Sym *sym_define;       /**< Direct pointer to the macro definition symbol if defined. */
    struct Sym *sym_label;        /**< Direct pointer to the label symbol if applicable. */
    struct Sym *sym_struct;       /**< Direct pointer to the structure, union, or enum symbol. */
    struct Sym *sym_identifier;   /**< Direct pointer to the identifier symbol table entry. */
    int tok;                      /**< Unique numeric token identifier assigned by the lexer. */
    int len;                      /**< Length of the token string in bytes. */
    char str[1];                  /**< Flexible array member storing the null-terminated token string text. */
} TokenSym;

#ifdef TCC_TARGET_PE
typedef unsigned short nwchar_t;
#else
typedef int nwchar_t;
#endif

/**
 * @brief Non-owning immutable string slice view (pointer and length).
 */
typedef struct StringSlice {
    const char *ptr;
    size_t len;
} StringSlice;

static inline int slice_eq(StringSlice a, StringSlice b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0;
}

static inline const char *slice_memchr(StringSlice s, int c) {
    return (const char *)memchr(s.ptr, c, s.len);
}

static inline const char *slice_memmem(StringSlice haystack, StringSlice needle) {
    const char *p, *end;
    if (needle.len == 0)
        return haystack.ptr;
    if (needle.len > haystack.len)
        return NULL;
    end = haystack.ptr + (haystack.len - needle.len);
    for (p = haystack.ptr; p <= end; p++) {
        if (p[0] == needle.ptr[0] && memcmp(p, needle.ptr, needle.len) == 0)
            return p;
    }
    return NULL;
}

/**
 * @brief Dynamic string buffer descriptor used for preprocessor and identifier strings.
 */
typedef struct CString {
    int size;                     /**< Current size of the string content in bytes. */
    int size_allocated;           /**< Total allocated capacity of the data buffer in bytes. */
    char *data;                   /**< Pointer to the character or wide-character (`nwchar_t*`) data buffer. */
} CString;

/**
 * @brief C type descriptor combining type flags and an optional reference symbol.
 */
typedef struct CType {
    int t;                        /**< Type flags specifying basic type, qualifiers (const/volatile), and modifiers. */
    struct Sym *ref;              /**< Reference symbol pointer for structs, unions, enums, functions, or pointer targets. */
} CType;

/* long double words on host(!) platform */
#define LDOUBLE_WORDS ((sizeof(long double)+3)/4) /**< Number of 32-bit words for long double on host */

/**
 * @brief Union holding compile-time constant scalar, floating-point, or string values.
 */
typedef union CValue {
    long double ld;               /**< Long double precision floating-point constant value. */
    double d;                     /**< Double precision floating-point constant value. */
    float f;                      /**< Single precision floating-point constant value. */
    uint64_t i;                   /**< Unsigned 64-bit integer constant value or pointer representation. */
    struct {
        char *data;               /**< Pointer to string constant literal data bytes. */
        int size;                 /**< Size of the string constant literal in bytes. */
    } str;                        /**< String literal constant descriptor. */
    int tab[LDOUBLE_WORDS];       /**< Raw 32-bit word array representation for long double or large constants. */
} CValue;

/**
 * @brief Evaluation stack value descriptor tracking type, register allocation, and constant/symbol state.
 */
typedef struct SValue {
    CType type;                   /**< Complete C type descriptor of the expression value. */
    unsigned short r;             /**< Register allocation flags, register index, and storage flags (`VT_*`). */
    unsigned short r2;            /**< Second register index (used for 64-bit `long long` values on 32-bit targets; `VT_CONST` if unused). */
    union {
      struct { int jtrue, jfalse; }; /**< Jump targets (true and false condition branches) for boolean expressions. */
      CValue c;                      /**< Constant value descriptor if the storage class is `VT_CONST`. */
    };
    union {
      struct { unsigned short cmp_op, cmp_r; }; /**< Comparison operation and register details for `VT_CMP` conditional values. */
      struct Sym *sym;                          /**< Associated symbol pointer if `VT_SYM` | `VT_CONST`, or result of unary identifier resolution. */
    };
} SValue;

/**
 * @brief `Symbol` attributes describing alignment, packing, linkage, and special modifiers.
 */
struct SymAttr {
    unsigned int
    aligned     : 5, /**< Alignment constraint specified as log2 + 1 (0 == unspecified). */
    packed      : 1, /**< Struct or field packing attribute (forces byte alignment and removes padding). */
    weak        : 1, /**< Weak symbol linkage attribute (permits overriding by strong definitions at link time). */
    visibility  : 2, /**< Symbol visibility level (e.g., default, hidden, internal, protected). */
    dllexport   : 1, /**< Windows PE dllexport attribute (exports symbol from a dynamic library/DLL). */
    nodecorate  : 1, /**< Windows PE nodecorate attribute (suppresses standard name decoration mangling). */
    dllimport   : 1, /**< Windows PE dllimport attribute (imports symbol from an external dynamic library/DLL). */
    addrtaken   : 1, /**< Address-taken flag (indicates that the address of the variable or function has been taken). */
    nodebug     : 1, /**< Nodebug attribute (suppresses emission of debugging metadata and debug entries for this symbol). */
    nocopy      : 1, /**< Non-copyable struct type attribute (prevents structural copying or assignment of struct instances). */
    is_slice    : 1, /**< Slice type flag (indicates a synthesized struct generated for a D/TPC slice type T[..]). */
    is_tuple    : 1, /**< Tuple type flag (indicates a synthesized struct generated for a tuple type (T1, ..., Tn)). */
    str_lit     : 1, /**< String literal array flag (indicates an array created for a string literal including terminating null). */
    used        : 1, /**< Symbol used flag (tracks whether a variable or function symbol has been referenced in expressions or assignments). */
    is_param    : 1, /**< Function parameter flag (indicates that the symbol represents a formal parameter of a function). */
    final_var   : 1, /**< Final variable attribute (prevents reassignment). */
    scoped      : 1, /**< Scoped variable attribute. */
    assigned    : 1; /**< Whether a final variable has been assigned/initialized. */
};
/**
 * @brief Function purity level representing mathematical purity guarantees.
 */
typedef enum {
    FUNC_PURITY_UNKNOWN = 0,     /**< Purity not explicitly specified nor yet inferred */
    FUNC_PURITY_TRUE,            /**< Purity enforced to true via `@pure`. */
    FUNC_PURITY_INFERRED_FALSE,  /**< Purity inferred false */
    FUNC_PURITY_INFERRED_TRUE,   /**< Function was inferred pure */
} FuncPurity;

#define IS_PURE(p) ((p) == FUNC_PURITY_TRUE || (p) == FUNC_PURITY_INFERRED_TRUE)

/**
 * @brief `Function safety level for memory safety checks.
 */
typedef enum {
    FUNC_SAFETY_UNKNOWN = 0,     /**< Safety not explicitly specified nor yet inferred */
    FUNC_SAFETY_SAFE,            /**< Safe level (`@safe` attribute, pointer checks enforced) */
    FUNC_SAFETY_SYSTEM,          /**< System safety level (unchecked) */
    FUNC_SAFETY_TRUSTED,         /**< Trusted safety level (exempt from checks) */
    FUNC_SAFETY_INFERRED_SYSTEM, /**< Safety inferred system / unsafe */
    FUNC_SAFETY_INFERRED_SAFE,   /**< Function was inferred safe */
} FuncSafety;

#define IS_SAFE(s) ((s) == FUNC_SAFETY_SAFE || (s) == FUNC_SAFETY_INFERRED_SAFE || (s) == FUNC_SAFETY_TRUSTED)

/* function attributes or temporary attributes for parsing */
struct FuncAttr {
    unsigned
    func_call   : 3, /**< Calling convention encoding: `FUNC_CDECL`, `FUNC_STDCALL`, `FUNC_FASTCALL1`, `FUNC_FASTCALL2`, `FUNC_FASTCALL3` or `FUNC_THISCALL`. */
    func_type   : 2, /**< Function prototype kind: `FUNC_NEW`, `FUNC_OLD` or `FUNC_ELLIPSIS`. */
    func_noreturn : 1, /**< Whether the function is declared `noreturn` via `attribute((noreturn))`. */
    func_ctor   : 1, /**< Whether the function is registered via `attribute((constructor))`. */
    func_dtor   : 1, /**< Whether the function is registered via `attribute((destructor))`. */
    func_args   : 8, /**< Number of argument words affected by the PE `__stdcall` calling convention. */
    func_alwinl : 1, /**< Whether the function was marked `always_inline`. */
    func_pure   : 2, /**< Purity level: `FUNC_PURITY_UNKNOWN`, `FUNC_PURITY_TRUE` or an inferred purity variant. */
    func_safety : 3, /**< Memory safety level: `FUNC_SAFETY_UNKNOWN`, `FUNC_SAFETY_SAFE`, `FUNC_SAFETY_SYSTEM`, `FUNC_SAFETY_TRUSTED` or an inferred safety variant. */
    xxxx        : 8;
};
/* symbol management */
/**
 * @brief Symbol table entry describing variables, functions, types, labels, macros, and typedefs.
 */
typedef struct Sym {
    int v;                        /**< Symbol token identifier representing the name. */
    unsigned short r;             /**< Associated register, or `VT_CONST`/`VT_LOCAL` storage class and lvalue flags. */
    struct SymAttr a;             /**< Extended symbol attributes (alignment, packing, weak linkage, visibility). */
    union {
        struct {
            int c;                /**< Associated integer constant value, stack offset, or ELF symbol index. */
            union {
                int sym_scope;    /**< Lexical scope nesting level for local variables. */
                int jnext;        /**< Next jump label index in chain. */
                int jind;         /**< Generated code index position for labels. */
                struct FuncAttr f;/**< Function attributes (calling convention, purity, safety, noreturn). */
                int auxtype;      /**< Auxiliary bitfield access type or underlying integer type. */
            };
        };
        long long enum_val;       /**< Enumerator constant integer value if `IS_ENUM_VAL` is set. */
        int *d;                   /**< Preprocessor macro definition token stream. */
        struct Sym *cleanup_func; /**< Cleanup function pointer for variable cleanup attribute. */
    };

    CType type;                   /**< Complete C type descriptor associated with this symbol. */
    union {
        struct Sym *next;         /**< Pointer to next related symbol (e.g., struct/union fields or anonymous members). */
        int *e;                   /**< Expanded token stream with preprocessor macro expansions. */
        int asm_label;            /**< Associated inline assembly label identifier. */
        struct Sym *cleanupstate; /**< Cleanup state pointer for defined labels. */
        int *vla_array_str;       /**< Variable-length array allocation code token string. */
    };
    struct Sym *prev;             /**< Pointer to the previous symbol in the current scope symbol stack. */
    union {
        struct Sym *prev_tok;     /**< Previous symbol sharing the same token hash bucket. */
        struct Sym *cleanup_sym;  /**< Symbol associated with `__attribute__((cleanup()))`. */
        struct Sym *cleanup_label;/**< Label in the pending gotos resolution chain. */
    };
} Sym;

/**
 * @brief ELF section descriptor managing section data, relocations, and output attributes.
 */
typedef struct Section {
    unsigned long data_offset;    /**< Current write offset position within the section data buffer in bytes. */
    unsigned char *data;          /**< Pointer to the raw section content data buffer. */
    unsigned long data_allocated; /**< Total allocated capacity of the data buffer (used for reallocation management). */
    TCCState *s1;                 /**< Pointer to the parent `TCCState` compilation session context. */
    int sh_name;                  /**< ELF section name string table index (used during final output generation). */
    int sh_num;                   /**< ELF section table index number. */
    int sh_type;                  /**< ELF section type (e.g., `SHT_PROGBITS`, `SHT_SYMTAB`, `SHT_RELA`). */
    int sh_flags;                 /**< ELF section attribute flags (e.g., `SHF_ALLOC`, `SHF_WRITE`, `SHF_EXECINSTR`). */
    int sh_info;                  /**< ELF section extra information field. */
    int sh_addralign;             /**< Required memory address alignment boundary for the section. */
    int sh_entsize;               /**< Entry size in bytes for sections containing fixed-size table entries. */
    unsigned long sh_size;        /**< Final computed section size in bytes (used during output emission). */
    addr_t sh_addr;               /**< Virtual memory address at which the section is loaded or relocated. */
    unsigned long sh_offset;      /**< File offset in the generated output file where section data begins. */
    int nb_hashed_syms;           /**< Number of hashed symbols (used to track and resize symbol hash tables). */
    struct Section *link;         /**< Pointer to a linked ELF section (such as symbol table linking to string table). */
    struct Section *reloc;        /**< Pointer to the corresponding relocation section for this section, if any. */
    struct Section *hash;         /**< Hash table section for fast symbol lookup within symbol tables. */
    struct Section *prev;         /**< Pointer to the previous section on the active section stack. */
    char name[1];                 /**< Flexible array member holding the null-terminated section name string. */
} Section;

/**
 * @brief Dynamic link library (DLL/shared library) reference descriptor.
 */
typedef struct DLLReference {
    int level;                    /**< Dependency nesting depth level of the loaded dynamic library. */
    void *handle;                 /**< Dynamic library loading handle returned by `dlopen` or `LoadLibrary`. */
    unsigned char found;          /**< Flag indicating whether the library file was successfully found and opened. */
    unsigned char index;          /**< Assigned internal library index number. */
    char name[1];                 /**< Flexible array member storing the library filename or path string. */
} DLLReference;

/* -------------------------------------------------- */

/* -------------------------------------------------- */

#define SYM_STRUCT     0x40000000 /**< struct/union/enum symbol space */
#define SYM_FIELD      0x20000000 /**< struct/union field symbol space */
#define SYM_FIRST_ANOM 0x10000000 /**< first anonymous sym */

/* stored in '`Sym`->f.`func_type`' field */
#define FUNC_NEW       1 /**< ansi function prototype */
#define FUNC_OLD       2 /**< old function prototype */
#define FUNC_ELLIPSIS  3 /**< ansi function prototype with ... */

/* stored in '`Sym`->f.`func_call`' field */
#define FUNC_CDECL     0 /**< standard c call */
#define FUNC_STDCALL   1 /**< pascal c call */
#define FUNC_FASTCALL1 2 /**< first param in %eax */
#define FUNC_FASTCALL2 3 /**< first parameters in %eax, %edx */
#define FUNC_FASTCALL3 4 /**< first parameter in %eax, %edx, %ecx */
#define FUNC_FASTCALLW 5 /**< first parameter in %ecx, %edx */
#define FUNC_THISCALL  6 /**< first param in %ecx */

/* field '`Sym`.t' for macros */
#define MACRO_OBJ      0 /**< object like macro */
#define MACRO_FUNC     1 /**< function like macro */
#define MACRO_JOIN     2 /**< macro uses ## */

/* field '`Sym`.r' for C labels */
#define LABEL_DEFINED  0 /**< label is defined */
#define LABEL_FORWARD  1 /**< label is forward defined */
#define LABEL_DECLARED 2 /**< label is declared but never used */
#define LABEL_GONE     3 /* label isn't in scope, but not yet popped
                            from `local_label_stack` (stmt exprs) */

/* `type_decl`() types */
#define TYPE_ABSTRACT  1 /**< type without variable */
#define TYPE_DIRECT    2 /**< type with variable */
#define TYPE_PARAM     4 /**< type declares function parameter */
#define TYPE_NEST      8 /**< nested call to `post_type` */

#define IO_BUF_SIZE 8192 /**< `Input` file read buffer size */

/**
 * @brief Lexer input stream buffer descriptor for source files and included headers.
 */
typedef struct BufferedFile {
    uint8_t *buf_ptr;             /**< Current read pointer position within the input text buffer. */
    uint8_t *buf_end;             /**< Pointer to the end of valid data in the input buffer. */
    int fd;                       /**< File descriptor of the opened source file (-1 if string buffer). */
    struct BufferedFile *prev;    /**< Pointer to the enclosing outer include file buffer (include stack). */
    int line_num;                 /**< Current source line number (tracked for error messages and debug info). */
    int line_ref;                 /**< Last printed line number (used by preprocessor `-E` output formatting). */
    int ifndef_macro;             /**< Preprocessor macro identifier for include guard detection (`#ifndef ... #endif`). */
    int ifndef_macro_saved;       /**< Saved outer include guard macro identifier. */
    int *ifdef_stack_ptr;         /**< Pointer into the conditional inclusion stack at file entry. */
    int include_next_index;       /**< Next search path index for `#include_next` directives. */
    int prev_tok_flags;           /**< Saved lexer token parsing flags from prior context. */
    char filename[1024];          /**< Path string of the source file currently being parsed. */
    char *true_filename;          /**< Unmodified source filename string (ignoring `#line` preprocessor directives). */
    unsigned char unget[4];       /**< Pushback character buffer for lexer lookahead. */
    unsigned char buffer[1];      /**< Flexible array buffer containing source text data with sentinel padding. */
} BufferedFile;

#define CH_EOB   '\\'       /**< end of buffer or '\0' char in file */
#define CH_EOF   (-1)   /**< end of file */

/**
 * @brief Recorded token string buffer for macros, inlines, and token streams.
 */
typedef struct TokenString {
    int *str;                     /**< Pointer to the dynamically allocated array of token integers. */
    int len;                      /**< Number of tokens currently stored in the token stream. */
    int need_spc;                 /**< Flag indicating whether whitespace separation is required before next token. */
    int allocated_len;            /**< Total allocated capacity of the token array. */
    int last_line_num;            /**< Source line number of the last recorded token. */
    int save_line_num;            /**< Saved source line number during token stream manipulation. */
    struct TokenString *prev;     /**< Pointer to previous token string for chaining macro expansions. */
    const int *prev_ptr;          /**< Previous token pointer in expansion stream. */
    char alloc;                   /**< Allocation type flag indicating memory ownership. */
} TokenString;

/**
 * @brief Combined attribute definition container for declarations and symbols.
 */
typedef struct AttrDef {
    struct SymAttr a;             /**< Symbol attributes (alignment, packing, visibility, weak linkage). */
    struct FuncAttr f;            /**< Function attributes (calling convention, purity, safety, noreturn). */
    struct Section *section;      /**< Target section pointer specified via section attribute. */
    Sym *cleanup_func;            /**< Cleanup function symbol pointer for variable cleanup attribute. */
    int alias_target;             /**< Target symbol token identifier for alias attributes. */
    int asm_label;                /**< Associated inline assembly label identifier. */
    char attr_mode;               /**< Machine mode attribute value for type sizing (`__attribute__((__mode__(...)))`). */
    char new_section;             /**< Flag indicating whether a new section was created. */
    int noinit;                   /**< Non-zero if `@noinit` storage attribute is specified. */
} AttrDef;

/**
 * @brief Inline function definition record for deferred compilation of inline functions.
 */
typedef struct InlineFunc {
    TokenString *func_str;        /**< Recorded token string of the inline function body. */
    Sym *sym;                     /**< Symbol table entry corresponding to the inline function. */
    char filename[1];             /**< Flexible array member storing the source filename where defined. */
} InlineFunc;

/**
 * @brief Cached include file guard record to speed up header inclusion checks.
 */
typedef struct CachedInclude {
    int ifndef_macro;             /**< Include guard preprocessor macro token identifier. */
    int once;                     /**< Flag indicating whether `#pragma once` was specified in the header. */
    int hash_next;                /**< Hash chain index pointing to the next cached include entry (-1 if none). */
    char filename[1];             /**< Flexible array member storing the canonical include file path string. */
} CachedInclude;

#define CACHED_INCLUDES_HASH_SIZE 32 /**< Hash table size for included files (#pragma once) */

#ifdef CONFIG_TCC_ASM
/**
 * @brief Evaluated constant expression value structure for assembler expressions.
 */
typedef struct ExprValue {
    uint64_t v;                   /**< Evaluated integer constant value offset. */
    Sym *sym;                     /**< Optional base symbol associated with the expression. */
    int pcrel;                    /**< Flag indicating whether the expression is PC-relative. */
} ExprValue;

#define MAX_ASM_OPERANDS 30 /**< Maximum number of inline assembly operands */
/**
 * @brief Inline assembly operand description structure for extended inline assembly (`__asm__`).
 */
typedef struct ASMOperand {
    int id;                       /**< GCC 3 optional C identifier token (0 if only positional number supported). */
    char constraint[16];          /**< Operand constraint string (e.g., "r", "m", "i"). */
    char asm_str[16];             /**< Computed output assembler string representation for the operand. */
    SValue *vt;                   /**< C value of the expression on evaluation stack. */
    int ref_index;                /**< Reference index giving connection to output constraint if >= 0. */
    int input_index;              /**< Reference index giving connection to input constraint if >= 0. */
    int priority;                 /**< Register allocation priority used to assign hardware registers. */
    int reg;                      /**< Hardware register number used for this operand if >= 0. */
    int is_llong;                 /**< Flag indicating a 64-bit long long double register value. */
    int is_memory;                /**< Flag indicating the operand resides in memory. */
    int is_rw;                    /**< Flag indicating read-write modifier (`+`) is set. */
    int is_label;                 /**< Flag indicating the operand is an asm goto label. */
} ASMOperand;
#endif

/* extra symbol attributes (not in symbol table) */
struct sym_attr {
    unsigned got_offset;
    unsigned plt_offset;
    int plt_sym;
    int dyn_index;
    unsigned char linker_sym:1;
#ifdef TCC_TARGET_ARM
    unsigned char plt_thumb_stub:1;
#endif
};

/* `Runtime` policies for D slice assignments whose lengths differ.  */
#define TCC_SLICE_MISMATCH_ABORT  0 /**< D-style range error (default) */
#define TCC_SLICE_MISMATCH_COPY   1 /**< copy only the common prefix */
#define TCC_SLICE_MISMATCH_IGNORE 2 /**< copy nothing */

#define WARN_ON 1
struct TCCState {
    unsigned char verbose; /**< Whether to display some information during compilation. */
    unsigned char nostdinc; /**< Whether to skip adding the standard system include paths. */
    unsigned char nostdlib; /**< Whether to skip adding the standard system libraries. */
    unsigned char nostdlib_paths; /**< Whether to skip searching the default library paths. */
    unsigned char nocommon; /**< Whether to use explicit data symbols instead of common (tentative) symbols for uninitialized globals. */
    unsigned char static_link; /**< Whether static linking of the standard libraries is performed. */
    unsigned char rdynamic; /**< Whether all global symbols are exported. */
    unsigned char symbolic; /**< Whether symbols are resolved preferring the current module before external ones. */
    unsigned char znodelete; /**< Whether to set the dynamic loader `DF_1_NODELETE` flag. */
    unsigned char filetype; /**< File type of the compilation: `NONE`, C source or ASM source. */
    unsigned char optimize; /**< Whether to define the `__OPTIMIZE__` macro. */
    unsigned char option_pthread; /**< Whether the `-pthread` option was set. */
    unsigned char enable_new_dtags; /**< Whether the `-Wl,--enable-new-dtags` option was set. */
    unsigned int  cversion; /**< Supported C `ISO` version (199901 by default, 201112, ...). */

    /* C language options */
    unsigned char char_is_unsigned; /**< Whether plain `char` is treated as unsigned. */
    unsigned char leading_underscore; /**< Whether identifiers may start with an underscore. */
    unsigned char ms_extensions; /**< Whether MS extensions are allowed (e.g. nested named struct w/o identifier behaves like unnamed). */
    unsigned char dollars_in_identifiers; /**< Whether the '$' character is allowed in identifiers. */
    unsigned char ms_bitfields; /**< Whether the MS algorithm for aligning bitfields is emulated. */
    unsigned char reverse_funcargs; /**< Whether arguments are evaluated last-first (reversed calling convention). */
    unsigned char gnu89_inline; /**< Whether `extern inline` is treated like `static inline`. */
    unsigned char unwind_tables; /**< Whether an `eh_frame` exception handling section is generated. */
    unsigned char lang_d; /**< Whether extended D language semantics are enabled (`-lang=d`). */
    unsigned char diagnostics_show_column; /**< Whether to display column numbers in diagnostics (-f[no-]diagnostics-show-column). */
    /* D conditional compilation (version and debug blocks).  */
    char **d_version_ids; /**< Array of D language version identifiers for conditional compilation. */
    int nb_d_version_ids; /**< Number of D language version identifiers. */
    char **d_debug_ids; /**< Array of D language debug identifiers for conditional compilation. */
    int nb_d_debug_ids; /**< Number of D language debug identifiers. */
    int d_version_level; /**< D language version conditional compilation level. */
    int d_debug_level; /**< D language debug conditional compilation level. */
    /* `Runtime` policy for slice copies whose lengths differ.  */
    unsigned char slice_len_mismatch; /**< Policy for slice assignments with mismatched lengths: abort, copy prefix, or ignore. */
    /* `Maximum` nesting of lexical scopes (-fscope-depth=N).  `Zero` means
       the built-in `MAX_SCOPE_DEPTH`.  */
    int max_scope_depth; /**< Maximum lexical scope nesting depth (`MAX_SCOPE_DEPTH` when zero). */

    /* warning switches */
    unsigned char warn_none; /**< Whether all warnings are disabled. */
    unsigned char warn_all; /**< Whether all warnings are enabled. */
    unsigned char warn_error; /**< Whether warnings are treated as errors. */
    unsigned char warn_write_strings; /**< Whether writing string literals to memory is warned against. */
    unsigned char warn_unsupported; /**< Whether unsupported language features are warned about. */
    unsigned char warn_implicit_function_declaration; /**< Whether implicit function declarations are warned about. */
    unsigned char warn_discarded_qualifiers; /**< Whether discarded qualifiers (e.g. `const int` dropped) are warned about. */
    unsigned char warn_uncovered; /**< Whether uncovered (unused) warnings are enabled. */
    unsigned char warn_unused; /**< Whether unused symbols are warned about. */
    unsigned char warn_unused_variable; /**< Whether unused local variables are warned about. */
    unsigned char warn_unused_parameter; /**< Whether unused function parameters are warned about. */
    unsigned char warn_unused_alias; /**< Whether unused type aliases are warned about. */
    unsigned char warn_stack_address_escape;
    unsigned char stack_address_escape_error;
    unsigned char warn_num; /**< Temporary variable holding the currently issued warning number (`tcc_warning_c()`). */

    unsigned char option_r; /* option -r */
    unsigned char do_bench; /* option -bench */
    unsigned char do_unittest; /* option -unittest */
    unsigned char just_deps; /* option -M  */
    unsigned char gen_deps; /* option -`MD`  */
    unsigned char include_sys_deps; /* option -`MD`  */
    unsigned char gen_phony_deps; /* option -`MP` */

    /* compile with debug symbol (and use them if error during execution) */
    unsigned char do_debug; /**< Whether debug information is generated for the compilation. */
    unsigned char dwarf; /**< Whether `DWARF` debug information is emitted. */
    unsigned char do_backtrace; /**< Whether stack backtraces are generated for runtime errors. */
#ifdef CONFIG_TCC_BCHECK
    /* compile with built-in memory and bounds checker */
    unsigned char do_bounds_check; /**< Whether the built-in memory and bounds checker is compiled in. */
#endif
    unsigned char test_coverage; /**< Whether test coverage code is generated. */
    unsigned char dump_types; /**< Whether type information is dumped during compilation. */
    unsigned char dump_decls; /**< Whether declarations are dumped during compilation. */
    unsigned char dump_vstack; /**< Whether the expression value stack is dumped during compilation. */
    /* use `GNU` C extensions */
    unsigned char gnu_ext; /**< Whether `GNU` C language extensions are enabled. */
    /* use `TinyPC` extensions */
    unsigned char tcc_ext; /**< Whether `TinyPC` language extensions are enabled. */

    unsigned char dflag; /**< Debug information selector flag (`-dX` option). */
    unsigned char Pflag; /**< Preprocessor line macro output format switch (`-P` option). */

#ifdef TCC_TARGET_X86_64
    unsigned char nosse; /**< Whether SSE (`-mno-sse`) support is disabled. */
#endif
#ifdef TCC_TARGET_ARM
    unsigned char float_abi; /**< Floating point `ABI` of the generated code. */
#endif

    unsigned char has_text_addr; /**< Whether a fixed text address is set. */
    addr_t text_addr; /**< Address of the text section in the output image. */
    unsigned section_align; /**< Section alignment requirement. */
#ifdef TCC_TARGET_I386
    int seg_size; /**< Size of the code segment (32, or 16 with i386 assembler `.code16`). */
#endif

    char *tcc_lib_path; /**< Library path (`CONFIG_TCCDIR` or `-B` option). */
    char *soname; /**< Shared object name specified on the command line (`-soname`). */
    char *rpath; /**< Runtime library search path (`-Wl,-rpath=`). */
    char *elfint; /**< ELF interpreter path (`-Wl,-I...` on command line). */
    char *elf_entryname; /**< Entry symbol name (defaults to `_start`). */
    char *init_symbol; /**< Symbol called at load time (not used currently). */
    char *fini_symbol; /**< Symbol called at unload time (not used currently). */
    char *mapfile; /**< Whether to create a map file (not used currently). */

    /* output type, see `TCC_OUTPUT_XXX` */
    int output_type; /**< Output type: memory, executable, library, object or preprocessed. */
    /* output format, see `TCC_OUTPUT_FORMAT_xxx` */
    int output_format; /**< Output file format (ELF, binary, COFF, ...). */
    /* nth test to run with -dt -run */
    int run_test; /**< Index of the nth test to run with `-dt -run`. */

    /* array of all loaded dlls (including those referenced by loaded dlls) */
    DLLReference **loaded_dlls; /**< Array of loaded dynamic library references. */
    int nb_loaded_dlls; /**< Number of loaded dynamic libraries. */

    /* include paths */
    char **include_paths; /**< Array of user include search paths. */
    int nb_include_paths; /**< Number of user include paths. */

    char **sysinclude_paths; /**< Array of system include search paths. */
    int nb_sysinclude_paths; /**< Number of system include paths. */

    /* library paths */
    char **library_paths; /**< Array of user library search paths. */
    int nb_library_paths; /**< Number of user library paths. */

    /* crt?.o object path */
    char **crt_paths; /**< Array of C runtime object file paths. */
    int nb_crt_paths; /**< Number of C runtime object paths. */

    /* -D / -U options */
    CString cmdline_defs;
    /* -include options */
    CString cmdline_incl;

    /* error handling */
    void *error_opaque; /**< Opaque context passed to the error/warning callback. */
    void (*error_func)(void *opaque, const char *msg); /**< Error callback receiving the error context message. */
    int error_set_jmp_enabled; /**< Whether error handling uses longjmp-based recovery. */
    jmp_buf error_jmp_buf; /**< Long jump buffer used for error recovery. */
    int nb_errors; /**< Number of errors encountered during compilation. */

    /* output file for preprocessing (-E) */
    FILE *ppfp; /**< Output file for preprocessing (`-E`). */

    /* for -`MD`/-`MF`: collected dependencies for this compilation */
    char **target_deps; /**< Array of source files collected as compilation dependencies (`-MD`/`-MF`). */
    int nb_target_deps; /**< Number of collected dependency files. */

    /* compilation */
    BufferedFile *include_stack[INCLUDE_STACK_SIZE]; /**< Stack of buffered input files (include nesting). */
    BufferedFile **include_stack_ptr; /**< Pointer to the current top of the include stack. */

    int ifdef_stack[IFDEF_STACK_SIZE]; /**< Stack of `#ifdef`/`#ifndef` conditional compilation states. */
    int *ifdef_stack_ptr; /**< Pointer to the current position in the conditional stack. */

    /* included files enclosed with #ifndef `MACRO` */
    int cached_includes_hash[CACHED_INCLUDES_HASH_SIZE]; /**< Hash table of cached include guard states. */
    CachedInclude **cached_includes; /**< Linked list of cached included files. */
    int nb_cached_includes; /**< Number of cached included files. */

    /* #pragma pack stack */
    int pack_stack[PACK_STACK_SIZE]; /**< Stack of `#pragma pack` alignment settings. */
    int *pack_stack_ptr; /**< Pointer to the current position in the pack stack. */
    char **pragma_libs; /**< Array of library names given via `#pragma lib`. */
    int nb_pragma_libs; /**< Number of pragma libraries. */

    /* inline functions are stored as token lists and compiled last
       only if referenced */
    struct InlineFunc **inline_fns; /**< Array of deferred inline function definitions. */
    int nb_inline_fns; /**< Number of deferred inline functions. */

    /* sections */
    Section **sections; /**< Array of section descriptors. */
    int nb_sections; /**< Number of sections, including the first dummy section. */

    Section **priv_sections; /**< Array of private section descriptors. */
    int nb_priv_sections; /**< Number of private sections. */

    /* predefined sections */
    Section *text_section, *data_section, *rodata_section, *bss_section; /**< Predefined section pointers (text, data, read-only data, bss). */
    Section *common_section; /**< Section holding common (tentative) symbols. */
    Section *cur_text_section; /**< Current section where function code is generated. */
#ifdef CONFIG_TCC_BCHECK
    /* bound check related sections */
    Section *bounds_section; /**< Section containing global data bound description. */
    Section *lbounds_section; /**< Section containing local data bound description. */
#endif
    /* symbol section */
    union { Section *symtab_section, *symtab; }; /**< Historical alias for the symbol table section. */
    /* temporary dynamic symbol sections (for dll loading) */
    Section *dynsymtab_section; /**< Temporary dynamic symbol section (for dll loading). */
    /* exported dynamic symbol section */
    Section *dynsym; /**< Exported dynamic symbol section. */
    /* got & plt handling */
    Section *got, *plt; /**< Global offset table and procedure linkage table sections. */
    /* exception handling */
    Section *eh_frame_section; /**< Exception handling frame section. */
    Section *eh_frame_hdr_section; /**< Exception handling frame header section. */
    unsigned long eh_start; /**< Start offset of the exception handling data. */
    /* debug sections */
    Section *stab_section; /**< Symbol table debug section. */
    Section *dwarf_info_section; /**< `DWARF` debug info section. */
    Section *dwarf_abbrev_section; /**< `DWARF` debug info abbrev section. */
    Section *dwarf_line_section; /**< `DWARF` debug line section. */
    Section *dwarf_aranges_section; /**< `DWARF` debug aranges section. */
    Section *dwarf_str_section; /**< `DWARF` debug string table section. */
    Section *dwarf_line_str_section; /**< `DWARF` debug line string table section. */
    int dwlo, dwhi; /**< `DWARF` section range (low and high offsets). */
    /* test coverage */
    Section *tcov_section; /**< Test coverage section. */
    /* debug state */
    struct _tccdbg *dState; /**< Pointer to the debug state structure. */

    /* extra attributes (eg. `GOT`/`PLT` value) for symtab symbols */
    struct sym_attr *sym_attrs; /**< Array of extra symbol attributes. */
    int nb_sym_attrs; /**< Number of extra symbol attributes. */
    /* ptr to next reloc entry reused */
    ElfW_Rel *qrel; /**< Pointer to the next relocation entry to reuse. */
    #define qrel s1->qrel

    addr_t tls_start, tls_end; /**< Start and end addresses of the thread local storage. */

#ifdef TCC_TARGET_RISCV64
    struct pcrel_hi { addr_t addr, val; } **pcrel_hi_entries; /**< Array of PC-relative high-part relocation entries. */
    int nb_pcrel_hi_entries; /**< Number of PC-relative high-part relocation entries. */
#endif

#ifdef TCC_TARGET_PE
    /* `PE` info */
    int pe_subsystem;
    unsigned pe_characteristics;
    unsigned pe_dll_characteristics;
    unsigned pe_file_align;
    unsigned pe_stack_size;
    addr_t pe_imagebase;
# if defined(TCC_TARGET_X86_64) || defined(TCC_TARGET_ARM64)
    Section *uw_pdata;
    int uw_sym;
    int uw_xsym;
    unsigned uw_offs;
# endif
#endif

#if defined TCC_TARGET_MACHO
    char *install_name;
    uint32_t compatibility_version;
    uint32_t current_version;
#endif

#ifndef ELF_OBJ_ONLY
    int nb_sym_versions;
    struct sym_version *sym_versions;
    int nb_sym_to_version;
    int *sym_to_version;
    int dt_verneednum;
    Section *versym_section;
    Section *verneed_section;
#endif

#ifdef TCC_IS_NATIVE
    const char *run_main; /* entry for `tcc_run`() */
    void *run_ptr; /* `runtime_memory` */
    unsigned run_size; /* size of `runtime_memory`  */
    const char *run_stdin; /* custom stdin file for `run_main` */
#ifdef _WIN64
    void *run_function_table; /* unwind data */
#endif
    struct TCCState *next;
    struct rt_context *rc; /* pointer to backtrace info block */
    void *run_lj, *run_jb; /* sj/lj for `tcc_setjmp`()/`tcc_run`() */
    TCCBtFunc *bt_func;
    void *bt_data;
#endif

#ifdef CONFIG_TCC_BACKTRACE
    int rt_num_callers;
#endif

    /* benchmark info */
    int total_idents;
    int total_tokens;
    int total_lines;
    unsigned int total_bytes;
    unsigned int total_output[4];

    /* used by `tcc_load_ldscript` */
    unsigned char *ld_p; /* text pointer */

    /* for warnings/errors for object files */
    const char *current_filename;

    /* used by main and `tcc_parse_args` only */
    struct filespec **files; /* files seen on command line */
    int nb_files; /* number thereof */
    int nb_libraries; /* number of libs thereof */
    char *outfile; /* output filename */
    char *deps_outfile; /* option -`MF` */
    int argc;
    char **argv;
    /* -`Wl` options */
    char **link_argv;
    int link_argc, link_optind;
};

/**
 * @brief Command line input file specification (C source, library, object, ...).
 */
struct filespec {
    char type; /**< File type classification of the input file. */
    char name[1]; /**< Null-terminated file name of the input file. */
};

/* `The` current value can be: */
#define VT_VALMASK   0x003f  /**< mask for value location, register or: */
#define VT_CONST     0x0030  /**< constant in vc (must be first non register value) */
#define VT_LLOCAL    0x0031  /**< lvalue, offset on stack */
#define VT_LOCAL     0x0032  /**< offset on stack */
#define VT_CMP       0x0033  /**< the value is stored in processor flags (in vc) */
#define VT_JMP       0x0034  /**< value is the consequence of jmp true (even) */
#define VT_JMPI      0x0035  /**< value is the consequence of jmp false (odd) */
#define VT_LVAL      0x0100  /**< var is an lvalue */
#define VT_SYM       0x0200  /**< a symbol value is added */
#define VT_MUSTCAST  0x0C00  /* value must be casted to be correct (used for
                                char/short stored in integer registers) */
#define VT_NONCONST  0x1000  /* `VT_CONST`, but not an (C standard) integer
                                constant expression */
#define VT_MUSTBOUND 0x4000  /* bound checking must be done before
                                dereferencing value */
#define VT_BOUNDED   0x8000  /* value is bounded. `The` address of the
                                bounding function call point is in vc */
/* types */
#define VT_BTYPE       0x000f  /**< mask for basic type */
#define VT_VOID             0  /**< void type */
#define VT_BYTE             1  /**< signed byte type */
#define VT_SHORT            2  /**< short type */
#define VT_INT              3  /**< integer type */
#define VT_LLONG            4  /**< 64 bit integer */
#define VT_PTR              5  /**< pointer */
#define VT_FUNC             6  /**< function type */
#define VT_STRUCT           7  /**< struct/union definition */
#define VT_FLOAT            8  /**< `IEEE` float */
#define VT_DOUBLE           9  /**< `IEEE` double */
#define VT_LDOUBLE         10  /**< `IEEE` long double */
#define VT_BOOL            11  /**< `ISOC99` boolean type */
#define VT_SLICE           12  /**< D-style slice type */
#define VT_QLONG           13  /**< 128-bit integer. `Only` used for x86-64 `ABI` */
#define VT_QFLOAT          14  /**< 128-bit float. `Only` used for x86-64 `ABI` */
#define VT_AUTO            15  /**< auto type inference */

#define VT_UNSIGNED    0x0010  /**< unsigned type */
#define VT_DEFSIGN     0x0020  /**< explicitly signed or unsigned */
#define VT_ARRAY       0x0040  /**< array type (also has `VT_PTR`) */
#define VT_BITFIELD    0x0080  /**< bitfield modifier */
#define VT_CONSTANT    0x0100  /**< const modifier */
#define VT_VOLATILE    0x0200  /**< volatile modifier */
#define VT_RESTRICT    0x0400  /**< restrict modifier */
#define VT_IMMUTABLE   0x00040000 /**< D-style immutable modifier */
#define VT_QUAL        (VT_CONSTANT | VT_VOLATILE | VT_RESTRICT | VT_IMMUTABLE) /**< `Type` descriptor modifier or basic type flag */
#define VT_VLA         0x0800  /**< `VLA` type (also has `VT_PTR` and `VT_ARRAY`) */
#define VT_LONG        0x1000  /**< long type (also has `VT_INT` rsp. `VT_LLONG`) */

/* storage */
#define VT_EXTERN  0x00002000  /**< extern definition */
#define VT_STATIC  0x00004000  /**< static variable */
#define VT_TYPEDEF 0x00008000  /**< typedef definition */
#define VT_INLINE  0x00010000  /**< inline definition */
#define VT_TLS     0x00020000  /**< thread-local storage */
/* currently unused: 0x00080000 */

#define VT_STRUCT_SHIFT 20     /**< shift for bitfield shift values (32 - 2*6) */
#define VT_STRUCT_MASK (((1U << (6+6)) - 1) << VT_STRUCT_SHIFT | VT_BITFIELD) /**< `Bitmask` for structure/union field representation */
#define BIT_POS(t) (((t) >> VT_STRUCT_SHIFT) & 0x3f) /**< `Extract` bitfield bit position from type attribute */
#define BIT_SIZE(t) (((t) >> (VT_STRUCT_SHIFT + 6)) & 0x3f) /**< `Extract` bitfield bit size from type attribute */

#define VT_UNION    (1 << VT_STRUCT_SHIFT | VT_STRUCT) /**< `Union` type indicator stored in type descriptor */
#define VT_ENUM     (2 << VT_STRUCT_SHIFT) /**< integral type is an enum really */
#define VT_ENUM_VAL (3 << VT_STRUCT_SHIFT) /**< integral type is an enum constant really */

#define IS_ENUM(t) ((t & VT_STRUCT_MASK) == VT_ENUM) /**< `Check` if type descriptor represents an enumeration */
#define IS_ENUM_VAL(t) ((t & VT_STRUCT_MASK) == VT_ENUM_VAL) /**< `Check` if type descriptor represents an enum constant */
#define IS_UNION(t) ((t & (VT_STRUCT_MASK|VT_BTYPE)) == VT_UNION) /**< `Check` if type descriptor represents a union */

#define VT_ATOMIC   VT_VOLATILE /**< `Atomic` type modifier alias */

/* type mask (except storage) */
#define VT_STORAGE (VT_EXTERN | VT_STATIC | VT_TYPEDEF | VT_INLINE | VT_TLS) /**< `Mask` covering all storage class specifiers */
#define VT_TYPE (~(VT_STORAGE|VT_STRUCT_MASK)) /**< `Mask` covering base types excluding storage and bitfield */

/* symbol was created by tccasm.c first */
#define VT_ASM (VT_VOID | 4 << VT_STRUCT_SHIFT) /**< `Assembler` symbol type marker */
#define VT_ASM_FUNC (VT_VOID | 5 << VT_STRUCT_SHIFT) /**< `Assembler` function symbol type marker */
#define IS_ASM_SYM(sym) (((sym)->type.t & ((VT_BTYPE|VT_STRUCT_MASK) & ~(1<<VT_STRUCT_SHIFT))) == VT_ASM) /**< `Check` if symbol was created by assembler */
#define IS_ASM_FUNC(t) ((t & (VT_BTYPE|VT_STRUCT_MASK)) == VT_ASM_FUNC) /**< `Check` if type represents an assembler function */

/* base type is array (from typedef/typeof) */
#define VT_BT_ARRAY (6 << VT_STRUCT_SHIFT) /**< `Array` base type indicator from typedef/typeof */
#define IS_BT_ARRAY(t) ((t & VT_STRUCT_MASK) == VT_BT_ARRAY) /**< `Check` if base type is an array */

/* general: set/get the pseudo-bitfield value for bit-mask M */
#define BFVAL(M,N) ((unsigned)((M) & ~((M) << 1)) * (N)) /**< `Pseudo`-bitfield value generation helper */
#define BFGET(X,M) (((X) & (M)) / BFVAL(M,1)) /**< `Extract` pseudo-bitfield value from mask */
#define BFSET(X,M,N) ((X) = ((X) & ~(M)) | BFVAL(M,N)) /**< `Assign` pseudo-bitfield value to mask */

/* token values */

/* conditional ops */
#define TOK_LAND  0x90 /**< `Logical` `AND` operator token */
#define TOK_LOR   0x91 /**< `Logical` OR operator token */
/* warning: the following compare tokens depend on i386 asm code */
#define TOK_ULT 0x92 /**< `Relational` or comparison operator token */
#define TOK_UGE 0x93 /**< `Relational` or comparison operator token */
#define TOK_EQ  0x94 /**< `Relational` or comparison operator token */
#define TOK_NE  0x95 /**< `Relational` or comparison operator token */
#define TOK_ULE 0x96 /**< `Relational` or comparison operator token */
#define TOK_UGT 0x97 /**< `Relational` or comparison operator token */
#define TOK_Nset 0x98 /**< `Relational` or comparison operator token */
#define TOK_Nclear 0x99 /**< `Relational` or comparison operator token */
#define TOK_LT  0x9c /**< `Relational` or comparison operator token */
#define TOK_GE  0x9d /**< `Relational` or comparison operator token */
#define TOK_LE  0x9e /**< `Relational` or comparison operator token */
#define TOK_GT  0x9f /**< `Relational` or comparison operator token */

#define TOK_ISCOND(t) (t >= TOK_LAND && t <= TOK_GT) /**< `Check` if token is a conditional comparison operator */

#define TOK_DEC     0x80 /**< -- */
#define TOK_MID     0x81 /**< inc/dec, to void constant */
#define TOK_INC     0x82 /**< ++ */
#define TOK_UDIV    0x83 /**< unsigned division */
#define TOK_UMOD    0x84 /**< unsigned modulo */
#define TOK_PDIV    0x85 /**< fast division with undefined rounding for pointers */
#define TOK_UMULL   0x86 /**< unsigned 32x32 -> 64 mul */
#define TOK_ADDC1   0x87 /**< add with carry generation */
#define TOK_ADDC2   0x88 /**< add with carry use */
#define TOK_SUBC1   0x89 /**< add with carry generation */
#define TOK_SUBC2   0x8a /**< add with carry use */
#define TOK_SHL     '<' /**< shift left */
#define TOK_SAR     '>' /**< signed shift right */
#define TOK_SHR     0x8b /**< unsigned shift right */
#define TOK_NEG     TOK_MID /**< unary minus operation (for floats) */

#define TOK_ARROW   0xa0 /**< -> */
#define TOK_DOTS    0xa1 /**< three dots */
#define TOK_TWODOTS 0xa2 /**< C++ token ? */
#define TOK_TWOSHARPS 0xa3 /**< ## preprocessing token */
#define TOK_PLCHLDR 0xa4 /**< placeholder token as defined in `C99` */
#define TOK_COLON_ASSIGN 0xa5 /**< := assignment/declaration operator */
#define TOK_PPJOIN  (TOK_TWOSHARPS | SYM_FIELD) /**< A '##' in a macro to mean pasting */
#define TOK_SOTYPE  0xa7 /**< alias of '(' for parsing sizeof (type) */

/* assignment operators */
#define TOK_A_ADD   0xb0 /**< `Compound` assignment operator token */
#define TOK_A_SUB   0xb1 /**< `Compound` assignment operator token */
#define TOK_A_MUL   0xb2 /**< `Compound` assignment operator token */
#define TOK_A_DIV   0xb3 /**< `Compound` assignment operator token */
#define TOK_A_MOD   0xb4 /**< `Compound` assignment operator token */
#define TOK_A_AND   0xb5 /**< `Compound` assignment operator token */
#define TOK_A_OR    0xb6 /**< `Compound` assignment operator token */
#define TOK_A_XOR   0xb7 /**< `Compound` assignment operator token */
#define TOK_A_SHL   0xb8 /**< `Compound` assignment operator token */
#define TOK_A_SAR   0xb9 /**< `Compound` assignment operator token */
#define TOK_A_CAT   0xba /**< '~=' append to a D-style slice */

#define TOK_ASSIGN(t) (t >= TOK_A_ADD && t <= TOK_A_CAT) /**< `Check` if token is an assignment operator */
#define TOK_ASSIGN_OP(t) ("+-*/%&|^<>"[t - TOK_A_ADD]) /**< `Map` assignment token to base operator character */

/* tokens that carry values (in additional token string space / tokc) --> */
#define TOK_CCHAR   0xc0 /**< char constant in tokc */
#define TOK_LCHAR   0xc1 /**< `Wide` character constant token */
#define TOK_CINT    0xc2 /**< number in tokc */
#define TOK_CUINT   0xc3 /**< unsigned int constant */
#define TOK_CLLONG  0xc4 /**< long long constant */
#define TOK_CULLONG 0xc5 /**< unsigned long long constant */
#define TOK_CLONG   0xc6 /**< long constant */
#define TOK_CULONG  0xc7 /**< unsigned long constant */
#define TOK_STR     0xc8 /**< pointer to string in tokc */
#define TOK_LSTR    0xc9 /**< `Wide` string literal token */
#define TOK_CFLOAT  0xca /**< float constant */
#define TOK_CDOUBLE 0xcb /**< double constant */
#define TOK_CLDOUBLE 0xcc /**< long double constant */
#define TOK_PPNUM   0xcd /**< preprocessor number */
#define TOK_PPSTR   0xce /**< preprocessor string */
#define TOK_LINENUM 0xcf /**< line number info */

#define TOK_HAS_VALUE(t) (t >= TOK_CCHAR && t <= TOK_LINENUM) /**< `Check` if token type carries an additional literal value */

#define TOK_EOF       (-1)  /**< end of file */
#define TOK_LINEFEED  10    /**< line feed */

/* all identifiers and strings have token above that */
#define TOK_IDENT 256 /**< `Base` token index for identifiers */

enum tcc_token {
    TOK_LAST = TOK_IDENT - 1
#define DEF(id, str) ,id /**< `Token` definition helper macro for tcctok.h */
#include "tcctok.h"
#undef DEF
};

/* keywords: tok >= `TOK_IDENT` && tok < `TOK_UIDENT` */
#define TOK_UIDENT TOK_DEFINE /**< `Upper` boundary for keyword token range */

/* ------------ libtcc.c ------------ */

ST_DATA struct TCCState *tcc_state;
ST_DATA void** stk_data;
ST_DATA int nb_stk_data;
ST_DATA int g_debug;

/* public functions currently used by the tcc main function */
ST_FUNC char *pstrcpy(char *buf, size_t buf_size, const char *s);
ST_FUNC char *pstrcat(char *buf, size_t buf_size, const char *s);
ST_FUNC char *pstrncpy(char *out, size_t buf_size, const char *s, size_t num);
PUB_FUNC char *tcc_basename(const char *name);
PUB_FUNC char *tcc_fileextension (const char *name);

/* all allocations - even `MEM_DEBUG` - use these */
PUB_FUNC void tcc_free(void *ptr);
PUB_FUNC void *tcc_malloc(unsigned long size);
PUB_FUNC void *tcc_mallocz(unsigned long size);
PUB_FUNC void *tcc_realloc(void *ptr, unsigned long size);
PUB_FUNC char *tcc_strdup(const char *str);

#ifdef MEM_DEBUG
#define tcc_free(ptr)           tcc_free_debug(ptr) /**< `Debug` memory free wrapper */
#define tcc_malloc(size)        tcc_malloc_debug(size, __FILE__, __LINE__) /**< `Debug` memory allocation wrapper */
#define tcc_mallocz(size)       tcc_mallocz_debug(size, __FILE__, __LINE__) /**< `Debug` zero-initialized memory allocation wrapper */
#define tcc_realloc(ptr,size)   tcc_realloc_debug(ptr, size, __FILE__, __LINE__) /**< `Debug` memory reallocation wrapper */
#define tcc_strdup(str)         tcc_strdup_debug(str, __FILE__, __LINE__) /**< `Debug` string duplication wrapper */
PUB_FUNC void tcc_free_debug(void *ptr);
PUB_FUNC void *tcc_malloc_debug(unsigned long size, const char *file, int line);
PUB_FUNC void *tcc_mallocz_debug(unsigned long size, const char *file, int line);
PUB_FUNC void *tcc_realloc_debug(void *ptr, unsigned long size, const char *file, int line);
PUB_FUNC char *tcc_strdup_debug(const char *str, const char *file, int line);
#endif

ST_FUNC void libc_free(void *ptr);
/* defined to be not used */
#define free(p) use_tcc_free(p) /**< `Override` standard free routing to tcc memory manager */
#define malloc(s) use_tcc_malloc(s) /**< `Override` standard malloc routing to tcc memory manager */
#define realloc(p, s) use_tcc_realloc(p, s) /**< `Override` standard realloc routing to tcc memory manager */
#undef strdup
#define strdup(s) use_tcc_strdup(s) /**< `Override` standard strdup routing to tcc memory manager */
PUB_FUNC int _tcc_error_noabort(const char *fmt, ...) PRINTF_LIKE(1,2);
PUB_FUNC NORETURN void _tcc_error(const char *fmt, ...) PRINTF_LIKE(1,2);
PUB_FUNC void _tcc_warning(const char *fmt, ...) PRINTF_LIKE(1,2);
/** `Trigger` internal compiler error with file and line location */
#define tcc_internal_error(msg) \
    tcc_error("internal compiler error in %s:%d: %s", __FUNCTION__,__LINE__,msg)

/* other utilities */
/**
 * @brief Growable pointer array used to track allocated resources for cleanup.
 */
ST_FUNC void dynarray_add(void *ptab, int *nb_ptr, void *data);
/**
 * @brief Reset the growable pointer array (freeing tracked entries if requested).
 */
ST_FUNC void dynarray_reset(void *pp, int *n);
/**
 * @brief Append a single character to a `CString` buffer.
 */
ST_INLN void cstr_ccat(CString *cstr, int ch);
/**
 * @brief Append a string to a `CString` buffer.
 */
ST_FUNC void cstr_cat(CString *cstr, const char *str, int len);
/**
 * @brief Append a wide character to a `CString` buffer.
 */
ST_FUNC void cstr_wccat(CString *cstr, int ch);
/**
 * @brief Initialize a `CString` buffer.
 */
ST_FUNC void cstr_new(CString *cstr);
/**
 * @brief Free a `CString` buffer.
 */
ST_FUNC void cstr_free(CString *cstr);
/**
 * @brief Format a string into a `CString` buffer.
 */
ST_FUNC int cstr_printf(CString *cs, const char *fmt, ...) PRINTF_LIKE(2,3);
/**
 * @brief Format a string from a varargs list into a `CString` buffer.
 */
ST_FUNC int cstr_vprintf(CString *cstr, const char *fmt, va_list ap);
/**
 * @brief Reset a `CString` buffer.
 */
ST_FUNC void cstr_reset(CString *cstr);
/**
 * @brief Open a buffered input file for the compiler.
 */
ST_FUNC void tcc_open_bf(TCCState *s1, const char *filename, int initlen);
/**
 * @brief Open and load a C source file into the compilation context.
 */
ST_FUNC int tcc_open(TCCState *s1, const char *filename);
/**
 * @brief Close the compiler buffered input state.
 */
ST_FUNC void tcc_close(void);

/* mark a memory pointer on stack for cleanup after errors */
#define stk_push(p) dynarray_add(&stk_data, &nb_stk_data, p) /**< `Push` pointer onto token string scratch stack */
#define stk_pop() (--nb_stk_data) /**< `Pop` pointer from token string scratch stack */
/* mark `CString` on stack for cleanup errors */
#define cstr_new_s(cstr) (cstr_new(cstr), stk_push(&(cstr)->data)) /**< `Initialize` scratch dynamic string buffer */
#define cstr_free_s(cstr) (cstr_free(cstr), stk_pop()) /**< `Free` scratch dynamic string buffer */

/**
 * @brief Add a source, object, library or DLL file to the compilation context.
 * @return `0` on success, `-1` on error, `FILE_NOT_FOUND` if the file does not exist.
 */
ST_FUNC int tcc_add_file_internal(TCCState *s1, const char *filename, int flags);
/* flags: */
#define AFF_PRINT_ERROR     0x10 /**< print error if file not found */
#define AFF_REFERENCED_DLL  0x20 /**< load a referenced dll from another dll */
#define AFF_TYPE_BIN        0x40 /**< file to add is binary */
#define AFF_WHOLE_ARCHIVE   0x80 /**< load all objects from archive */
/* s->filetype: */
#define AFF_TYPE_NONE   0 /**< `No` specific file addition type */
#define AFF_TYPE_C      1 /**< `File` addition type is C source */
#define AFF_TYPE_ASM    2 /**< `File` addition type is assembler source */
#define AFF_TYPE_ASMPP  4 /**< `File` addition type is preprocessed assembler source */
#define AFF_TYPE_LIB    8 /**< `File` addition type is library archive */
#define AFF_TYPE_MASK   (7 | AFF_TYPE_BIN) /**< `Mask` for file addition source type */
/* values from `tcc_object_type`(...) */
#define AFF_BINTYPE_REL 1 /**< `Binary` file type is relocatable object */
#define AFF_BINTYPE_DYN 2 /**< `Binary` file type is dynamic library */
#define AFF_BINTYPE_AR  3 /**< `Binary` file type is static archive */
#define AFF_BINTYPE_C67 4 /**< `Binary` file type is `TMS320C67` code */

/* return value of `tcc_add_file_internal`(): 0, -1, or `FILE_NOT_FOUND` */
#define FILE_NOT_FOUND -2 /**< `Error` code indicating input file could not be found */
#define FILE_NOT_RECOGNIZED -3 /**< unrecognized file type */

/**
 * @brief Add the C runtime startup object (`crt?.o`) to the compilation context.
 */
#ifndef ELF_OBJ_ONLY
ST_FUNC int tcc_add_crt(TCCState *s, const char *filename);
#endif
/**
 * @brief Add a dynamic library reference to the compilation context.
 */
ST_FUNC int tcc_add_dll(TCCState *s, const char *filename, int flags);
/**
 * @brief Add a support library or assembly support file to the compilation context.
 */
ST_FUNC int tcc_add_support(TCCState *s1, const char *filename);
#ifdef CONFIG_TCC_BCHECK
/**
 * @brief Add the built-in memory and bounds checker runtime.
 */
ST_FUNC void tcc_add_bcheck(TCCState *s1);
#endif
#ifdef CONFIG_TCC_BACKTRACE
/**
 * @brief Add the stack backtrace runtime stub.
 */
ST_FUNC void tcc_add_btstub(TCCState *s1);
#endif
/**
 * @brief Add libraries specified via `#pragma lib` to the compilation context.
 */
ST_FUNC void tcc_add_pragma_libs(TCCState *s1);
/**
 * @brief Add a library to the compilation context, returning an error code.
 */
PUB_FUNC int tcc_add_library_err(TCCState *s, const char *f);
/**
 * @brief Print compilation statistics for a finished compilation.
 */
PUB_FUNC void tcc_print_stats(TCCState *s, unsigned total_time);
/**
 * @brief Parse the compiler command line options into the compilation state.
 */
PUB_FUNC int tcc_parse_args(TCCState *s, int *argc, char ***argv);
#ifdef _WIN32
ST_FUNC char *normalize_slashes(char *path);
PUB_FUNC FILE *tcc_fopen(const char *f, const char *m);
PUB_FUNC int tcc_fclose(FILE *f);
#else
# define tcc_fopen fopen
# define tcc_fclose fclose
#endif
ST_FUNC DLLReference *tcc_add_dllref(TCCState *s1, const char *dllname, int level);
ST_FUNC char *tcc_load_text(int fd);
/* for #pragma once */
ST_FUNC int normalized_PATHCMP(const char *f1, const char *f2);

/* `tcc_parse_args` return codes: */
#define OPT_HELP 1 /**< `Command` line option: help */
#define OPT_HELP2 2 /**< `Command` line option: extended help */
#define OPT_V 3 /**< `Command` line option: version or verbose */
#define OPT_PRINT_DIRS 4 /**< `Command` line option: print search directories */
#define OPT_AR 5 /**< `Command` line option: archive library command */
#define OPT_IMPDEF 6 /**< `Command` line option: generate import definition */
#define OPT_STYLE 7 /**< `Command` line option: coding style check */
#define OPT_M32 32 /**< `Command` line option: 32-bit architecture target */
#define OPT_M64 64 /**< `Command` line option: 64-bit architecture target */

/* ------------ tccpp.c ------------ */

ST_DATA struct BufferedFile *file;
ST_DATA int tok;
ST_DATA CValue tokc;
ST_DATA const int *macro_ptr;
ST_DATA int parse_flags;
ST_DATA int tok_flags;
ST_DATA CString tokcstr; /* current parsed string, if any */

/* display benchmark infos */
ST_DATA int tok_ident;
ST_DATA TokenSym **table_ident;
ST_DATA int pp_expr;

#define TOK_FLAG_BOL   0x0001 /**< beginning of line before */
#define TOK_FLAG_BOF   0x0002 /**< beginning of file before */
#define TOK_FLAG_ENDIF 0x0004 /**< a endif was found matching starting #ifdef */

#define PARSE_FLAG_PREPROCESS 0x0001 /**< activate preprocessing */
#define PARSE_FLAG_TOK_NUM    0x0002 /**< return numbers instead of `TOK_PPNUM` */
#define PARSE_FLAG_LINEFEED   0x0004 /* line feed is returned as a
                                        token. line feed is also
                                        returned at eof */
#define PARSE_FLAG_ASM_FILE 0x0008 /**< we processing an asm file: '#' can be used for line comment, etc. */
#define PARSE_FLAG_SPACES     0x0010 /**< next() returns space tokens (for -E) */
#define PARSE_FLAG_ACCEPT_STRAYS 0x0020 /**< next() returns '\\' token */
#define PARSE_FLAG_TOK_STR    0x0040 /**< return parsed strings instead of `TOK_PPSTR` */

/* `isidnum_table` flags: */
#define IS_SPC 1 /**< `Character` classification: whitespace character */
#define IS_ID  2 /**< `Character` classification: identifier character */
#define IS_NUM 4 /**< `Character` classification: numeric digit character */

/**
 * @brief Output format for line markers in preprocessed output (`-E`).
 */
enum line_macro_output_format {
    LINE_MACRO_OUTPUT_FORMAT_GCC,
    LINE_MACRO_OUTPUT_FORMAT_NONE,
    LINE_MACRO_OUTPUT_FORMAT_STD,
    LINE_MACRO_OUTPUT_FORMAT_P10 = 11
};

ST_FUNC TokenSym *tok_alloc(const char *str, int len);
ST_FUNC int tok_alloc_const(const char *str);
ST_FUNC const char *get_tok_str(int v, CValue *cv);
ST_FUNC void begin_macro(TokenString *str, int alloc);
ST_FUNC void end_macro(void);
ST_FUNC TokenString *macro_stack_top(void);
ST_FUNC int set_idnum(int c, int val);
ST_INLN void tok_str_new(TokenString *s);
ST_FUNC TokenString *tok_str_alloc(void);
ST_FUNC void tok_str_add2(TokenString *s, int t, CValue *cv);
ST_FUNC void tok_str_free(TokenString *s);
ST_FUNC void tok_str_free_str(int *str);
ST_INLN void tok_str_add(TokenString *s, int t);
ST_FUNC void tok_str_add_tok(TokenString *s);
ST_INLN void define_push(int v, int macro_type, int *str, Sym *first_arg);
ST_FUNC void define_undef(Sym *s);
ST_INLN Sym *define_find(int v);
ST_FUNC void free_defines(Sym *b);
ST_FUNC void parse_define(void);
ST_FUNC void skip_to_eol(int warn);
/**
 * @brief Run the full preprocessor pass over the input.
 */
ST_FUNC void preprocess(int is_bof);
/**
 * @brief Fetch the next token, resolving macros as needed.
 */
ST_FUNC void next(void);
ST_INLN void unget_tok(int last_tok);
ST_FUNC void preprocess_start(TCCState *s1, int filetype);
ST_FUNC void preprocess_end(TCCState *s1);
ST_FUNC void tccpp_new(TCCState *s);
ST_FUNC void tccpp_delete(TCCState *s);
ST_FUNC void tccpp_putfile(const char *filename);
ST_FUNC int tcc_preprocess(TCCState *s1);
ST_FUNC void skip(int c);
ST_FUNC NORETURN void expect(const char *msg);
ST_FUNC void pp_error(CString *cs);


/* space excluding newline */
static inline int is_space(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f' || ch == '\r';
}
static inline int isid(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static inline int isnum(int c) {
    return c >= '0' && c <= '9';
}
static inline int isoct(int c) {
    return c >= '0' && c <= '7';
}
static inline int toup(int c) {
    return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c;
}

/* ------------ tccgen.c ------------ */

#define SYM_POOL_NB (8192 / sizeof(Sym)) /**< `Symbol` pool allocation block size */

/* `Maximum` nesting of lexical scopes.  `The` scope stack is a fixed
   array; exceeding this limit is a compile error, not a crash.  */
#define MAX_SCOPE_DEPTH 256 /**< `Maximum` nested lexical scope depth limit */

/**
 * @brief `Per`-function state.
 * `Every` lexical scope inherits a copy from its enclosing scope, so block-level
 * attributes such as '@safe' apply to the block and to everything nested in it.
 */
typedef struct FuncState {
    CType vt;             /**< `Function` return type descriptor */
    const char *name;     /**< `Function` name for diagnostics and `__func__` */
    int vc;               /**< `Stack` address of implicit struct return */
    int ind;              /**< `Function` start code address */
    FuncSafety safety;    /**< `Function` memory safety level */
    FuncPurity pure;      /**< `Function` purity level */
    unsigned char variadic;       /**< `Non`-zero if function is variadic */
    unsigned char old_style;      /**< `Non`-zero if K&R old-style parameter declaration */
} FuncState;

/**
 * @brief `Lexical` scope descriptor for symbol resolution and block-level attributes.
 */
typedef struct Scope {
    struct Scope *prev;   /**< `Enclosing` parent scope */
    struct {
        int loc;     /**< `Variable`-length array location offset */
        int locorig; /**< `Original` variable-length array location offset */
        int num;     /**< `Number` of active variable-length arrays */
    } vla;           /**< `Variable`-length array tracking */
    struct { Sym *s; int n; } cl; /**< `Cleanup` functions */
    int *bsym, *csym;     /**< `Break` and continue jump target symbol stacks */
    Sym *lstk, *llstk;    /**< `Local` symbol stack */
    FuncState fn;         /**< `Function` state for this scope */
} Scope;

ST_DATA Scope *cur_scope, *loop_scope, *root_scope;

#define func_vt     (cur_scope->fn.vt) /**< `Current` scope function return value type descriptor */
#define func_var    (cur_scope->fn.variadic) /**< `Current` scope function variadic status */
#define func_vc     (cur_scope->fn.vc) /**< `Current` scope function constant value */
#define func_ind    (cur_scope->fn.ind) /**< `Current` scope function instruction index */
#define func_old    (cur_scope->fn.old_style) /**< `Current` scope function old-style C prototype flag */
#define funcname    (cur_scope->fn.name) /**< `Current` scope function name identifier */

ST_DATA Sym *global_stack;
ST_DATA Sym *local_stack;
ST_DATA Sym *local_label_stack;
ST_DATA Sym *global_label_stack;
ST_DATA Sym *define_stack;
ST_DATA CType int_type, func_old_type, char_pointer_type;
ST_DATA SValue *vtop;
ST_DATA int rsym, anon_sym, ind, loc;
ST_DATA char debug_modes;

ST_DATA int nocode_wanted; /* true if no code generation wanted for an expression */
ST_DATA int global_expr;  /* true if compound literals must be allocated globally (used during initializers parsing */

/**
 * @brief Initialize the code generator state for a compilation context.
 */
ST_FUNC void tccgen_init(TCCState *s1);
/**
 * @brief Compile the accumulated internal state into output code/objects.
 */
ST_FUNC int tccgen_compile(TCCState *s1);
/**
 * @brief Finalize the code generation and emit output sections.
 */
ST_FUNC void tccgen_finish(TCCState *s1);
/**
 * @brief Add a D language version identifier for conditional compilation.
 */
ST_FUNC void tcc_add_d_version(TCCState *s, const char *id);
/**
 * @brief Add a D language debug identifier for conditional compilation.
 */
ST_FUNC void tcc_add_d_debug(TCCState *s, const char *id);
/**
 * @brief Check that the expression value stack was not corrupted during code generation.
 */
ST_FUNC void check_vstack(void);

ST_INLN int is_float(int t);
ST_FUNC int ieee_finite(double d);
ST_FUNC int exact_log2p1(int i);
ST_FUNC void test_lvalue(void);

ST_FUNC ElfSym *elfsym(Sym *);
ST_FUNC void update_storage(Sym *sym);
ST_FUNC void put_extern_sym2(Sym *sym, int sh_num, addr_t value, unsigned long size, int can_add_underscore);
ST_FUNC void put_extern_sym(Sym *sym, Section *section, addr_t value, unsigned long size);
#if PTR_SIZE == 4
ST_FUNC void greloc(Section *s, Sym *sym, unsigned long offset, int type);
#endif
ST_FUNC void greloca(Section *s, Sym *sym, unsigned long offset, int type, addr_t addend);

ST_INLN void sym_free(Sym *sym);
ST_FUNC Sym *sym_push(int v, CType *type, int r, int c);
ST_FUNC void sym_pop(Sym **ptop, Sym *b, int keep);
ST_FUNC Sym *sym_push2(Sym **ps, int v, int t, int c);
ST_FUNC Sym *sym_find2(Sym *s, int v);
ST_INLN Sym *sym_find(int v);
ST_FUNC Sym *label_find(int v);
ST_FUNC Sym *label_push(Sym **ptop, int v, int flags);
ST_FUNC void label_pop(Sym **ptop, Sym *slast, int keep);
ST_INLN Sym *struct_find(int v);

ST_FUNC Sym *global_identifier_push(int v, int t, int c);
ST_FUNC Sym *external_global_sym(int v, CType *type);
ST_FUNC Sym *external_helper_sym(int v);
ST_FUNC void vpush_helper_func(int v);
ST_FUNC void vset(CType *type, int r, int v);
ST_FUNC void vset_VT_CMP(int op);
ST_FUNC void vpushi(int v);
ST_FUNC void vpushv(SValue *v);
ST_FUNC void vpushsym(CType *type, Sym *sym);
ST_FUNC void vswap(void);
ST_FUNC void vrott(int n);
ST_FUNC void vrotb(int n);
ST_FUNC void vrev(int n);
ST_FUNC void vpop(void);
#if PTR_SIZE == 4
ST_FUNC void lexpand(void);
#endif
#ifdef TCC_TARGET_ARM
ST_FUNC int get_reg_ex(int rc, int rc2);
#endif
ST_FUNC void save_reg(int r);
ST_FUNC void save_reg_upstack(int r, int n);
ST_FUNC int get_reg(int rc);
ST_FUNC void save_regs(int n);
ST_FUNC void gaddrof(void);
ST_FUNC int gv(int rc);
ST_FUNC void gv2(int rc1, int rc2);
ST_FUNC void gen_op(int op);
ST_FUNC int type_size(CType *type, int *a);
ST_FUNC void mk_pointer(CType *type);
ST_FUNC void vstore(void);
ST_FUNC void inc(int post, int c);
ST_FUNC CString* parse_mult_str(const char *msg);
ST_FUNC CString* parse_asm_str(void);
ST_FUNC void indir(void);
ST_FUNC void unary(void);
ST_FUNC void gexpr(void);
ST_FUNC int expr_const(void);
#if defined CONFIG_TCC_BCHECK || defined TCC_TARGET_C67
ST_FUNC Sym *get_sym_ref(CType *type, Section *sec, unsigned long offset, unsigned long size);
#endif
#if defined TCC_TARGET_X86_64 && !defined TCC_TARGET_PE
ST_FUNC int classify_x86_64_va_arg(CType *ty);
#endif
#ifdef CONFIG_TCC_BCHECK
ST_FUNC void gbound_args(int nb_args);
ST_DATA int func_bound_add_epilog;
#endif
ST_FUNC Sym *gfunc_set_param(Sym *s, int c, int byref);

/* ------------ tccelf.c ------------ */

#define TCC_OUTPUT_FORMAT_ELF    0 /**< default output format: `ELF` */
#define TCC_OUTPUT_FORMAT_BINARY 1 /**< binary image output */
#define TCC_OUTPUT_FORMAT_COFF   2 /**< `COFF` */
#define TCC_OUTPUT_DYN           TCC_OUTPUT_DLL /**< `Output` format alias for dynamic library */

#define ARMAG  "!<arch>\n"    /**< `For` `COFF` and a.out archives */

typedef struct {
    unsigned int n_strx;         /* index into string table of name */
    unsigned char n_type;         /* type of symbol */
    unsigned char n_other;        /* misc info (usually empty) */
    unsigned short n_desc;        /* description field */
    unsigned int n_value;        /* value of symbol */
} Stab_Sym;

ST_FUNC void tccelf_new(TCCState *s);
ST_FUNC void tccelf_delete(TCCState *s);
ST_FUNC void tccelf_begin_file(TCCState *s1);
ST_FUNC void tccelf_end_file(TCCState *s1);
ST_FUNC Section *new_section(TCCState *s1, const char *name, int sh_type, int sh_flags);
ST_FUNC void section_realloc(Section *sec, unsigned long new_size);
ST_FUNC size_t section_add(Section *sec, addr_t size, int align);
ST_FUNC void *section_ptr_add(Section *sec, addr_t size);
ST_FUNC Section *find_section(TCCState *s1, const char *name);
ST_FUNC void free_section(Section *s);
ST_FUNC Section *new_symtab(TCCState *s1, const char *symtab_name, int sh_type, int sh_flags, const char *strtab_name, const char *hash_name, int hash_sh_flags);
ST_FUNC void init_symtab(Section *s);

ST_FUNC int put_elf_str(Section *s, const char *sym);
ST_FUNC int put_elf_sym(Section *s, addr_t value, unsigned long size, int info, int other, int shndx, const char *name);
ST_FUNC int set_elf_sym(Section *s, addr_t value, unsigned long size, int info, int other, int shndx, const char *name);
ST_FUNC int find_elf_sym(Section *s, const char *name);
ST_FUNC void put_elf_reloc(Section *symtab, Section *s, unsigned long offset, int type, int symbol);
ST_FUNC void put_elf_reloca(Section *symtab, Section *s, unsigned long offset, int type, int symbol, addr_t addend);

ST_FUNC void resolve_common_syms(TCCState *s1);
ST_FUNC void relocate_syms(TCCState *s1, Section *symtab, int do_resolve);
ST_FUNC void relocate_sections(TCCState *s1);

ST_FUNC ssize_t full_read(int fd, void *buf, size_t count);
ST_FUNC void *load_data(int fd, unsigned long file_offset, unsigned long size);
ST_FUNC int tcc_object_type(int fd, ElfW(Ehdr) *h);
ST_FUNC int tcc_load_object_file(TCCState *s1, int fd, unsigned long file_offset);
ST_FUNC int tcc_load_archive(TCCState *s1, int fd, int alacarte);
ST_FUNC void add_array(TCCState *s1, const char *sec, int c);

ST_FUNC struct sym_attr *get_sym_attr(TCCState *s1, int index, int alloc);
ST_FUNC addr_t get_sym_addr(TCCState *s, const char *name, int err, int forc);
ST_FUNC void list_elf_symbols(TCCState *s, void *ctx,
    void (*symbol_cb)(void *ctx, const char *name, const void *val));
ST_FUNC int set_global_sym(TCCState *s1, const char *name, Section *sec, addr_t offs);

/* `Browse` each elem of type <type> in section <sec> starting at elem <startoff>
   using variable <elem> */
/** `Iterate` over each element in a section */
#define for_each_elem(sec, startoff, elem, type) \
    for (elem = (type *) sec->data + startoff; \
         elem < (type *) (sec->data + sec->data_offset); elem++)

#ifndef ELF_OBJ_ONLY
ST_FUNC int tcc_load_dll(TCCState *s1, int fd, const char *filename, int level);
ST_FUNC int tcc_load_ldscript(TCCState *s1, int fd);
ST_FUNC void tccelf_add_crtbegin(TCCState *s1);
ST_FUNC void tccelf_add_crtend(TCCState *s1);
#endif
#ifndef TCC_TARGET_PE
ST_FUNC void tcc_add_runtime(TCCState *s1);
#endif

/* ------------ xxx-link.c ------------ */

#ifndef TCC_TARGET_PE
ST_FUNC int code_reloc (int reloc_type);
ST_FUNC int gotplt_entry_type (int reloc_type);
/* `Whether` to generate a `GOT`/`PLT` entry and when. `NO_GOTPLT_ENTRY` is first so
   that unknown relocation don't create a `GOT` or `PLT` entry */
enum gotplt_entry {
    NO_GOTPLT_ENTRY,	/* never generate (eg. `GLOB_DAT` & `JMP_SLOT` relocs) */
    BUILD_GOT_ONLY,	/* only build `GOT` (eg. `TPOFF` relocs) */
    AUTO_GOTPLT_ENTRY,	/* generate if sym is `UNDEF` */
    ALWAYS_GOTPLT_ENTRY	/* always generate (eg. `PLTOFF` relocs) */
};
#define NEED_RELOC_TYPE /**< `Architecture` requirement for relocation type handling */
#if !defined TCC_TARGET_MACHO || defined TCC_IS_NATIVE
ST_FUNC unsigned create_plt_entry(TCCState *s1, unsigned got_offset, struct sym_attr *attr);
ST_FUNC void relocate_plt(TCCState *s1);
ST_FUNC void build_got_entries(TCCState *s1, int got_sym); /* in tccelf.c */
#define NEED_BUILD_GOT /**< `Architecture` requirement for global offset table */
#endif
#endif

ST_FUNC void relocate(TCCState *s1, ElfW_Rel *rel, int type, unsigned char *ptr, addr_t addr, addr_t val);

/* ------------ xxx-gen.c ------------ */
ST_CONST const char * const target_machine_defs;
ST_CONST const int reg_classes[NB_REGS];

ST_FUNC void gsym_addr(int t, int a);
ST_FUNC void gsym(int t);
ST_FUNC void load(int r, SValue *sv);
ST_FUNC void store(int r, SValue *v);
ST_FUNC int gfunc_sret(CType *vt, int variadic, CType *ret, int *align, int *regsize);
ST_FUNC void gfunc_call(int nb_args);
ST_FUNC void gfunc_prolog(Sym *func_sym);
ST_FUNC void gfunc_epilog(void);
ST_FUNC void gen_fill_nops(int);
ST_FUNC int gjmp(int t);
ST_FUNC void gjmp_addr(int a);
ST_FUNC int gjmp_cond(int op, int t);
ST_FUNC int gjmp_append(int n, int t);
ST_FUNC void gen_opi(int op);
ST_FUNC void gen_opf(int op);
ST_FUNC void gen_cvt_ftoi(int t);
ST_FUNC void gen_cvt_itof(int t);
ST_FUNC void gen_cvt_ftof(int t);
ST_FUNC void ggoto(void);
#ifndef TCC_TARGET_C67
ST_FUNC void o(unsigned int c);
#endif
ST_FUNC void gen_vla_sp_save(int addr);
ST_FUNC void gen_vla_sp_restore(int addr);
ST_FUNC void gen_vla_alloc(CType *type, int align);

static inline uint16_t read16le(unsigned char *p) {
    return p[0] | (uint16_t)p[1] << 8;
}
static inline void write16le(unsigned char *p, uint16_t x) {
    p[0] = x & 255;  p[1] = x >> 8 & 255;
}
static inline uint32_t read32le(unsigned char *p) {
  return read16le(p) | (uint32_t)read16le(p + 2) << 16;
}
static inline void write32le(unsigned char *p, uint32_t x) {
    write16le(p, x);  write16le(p + 2, x >> 16);
}
static inline void add32le(unsigned char *p, int32_t x) {
    write32le(p, read32le(p) + x);
}
static inline uint64_t read64le(unsigned char *p) {
  return read32le(p) | (uint64_t)read32le(p + 4) << 32;
}
static inline void write64le(unsigned char *p, uint64_t x) {
    write32le(p, x);  write32le(p + 4, x >> 32);
}
static inline void add64le(unsigned char *p, int64_t x) {
    write64le(p, read64le(p) + x);
}
/* ------------ i386-gen.c ------------ */
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64 || defined TCC_TARGET_ARM
ST_FUNC void g(int c);
ST_FUNC void gen_le16(int c);
ST_FUNC void gen_le32(int c);
#endif
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64
ST_FUNC void gen_addr32(int r, Sym *sym, int c);
ST_FUNC void gen_addrpc32(int r, Sym *sym, int c);
ST_FUNC void gen_cvt_csti(int t);
ST_FUNC void gen_increment_tcov (SValue *sv);
#endif

/* ------------ `x86_64`-gen.c ------------ */
#ifdef TCC_TARGET_X86_64
ST_FUNC void gen_opl(int op);
#ifdef TCC_TARGET_PE
ST_FUNC void gen_vla_result(int addr);
#endif
ST_FUNC void gen_cvt_sxtw(void);
ST_FUNC void gen_cvt_csti(int t);
#endif

/* ------------ arm-gen.c ------------ */
#ifdef TCC_TARGET_ARM
#if defined(TCC_ARM_EABI) && !defined(CONFIG_TCC_ELFINTERP)
PUB_FUNC const char *default_elfinterp(struct TCCState *s);
#endif
ST_FUNC void arm_init(struct TCCState *s);
ST_FUNC void gen_increment_tcov (SValue *sv);
#endif

/* ------------ arm64-gen.c ------------ */
#ifdef TCC_TARGET_ARM64
ST_FUNC void gen_opl(int op);
ST_FUNC void gfunc_return(CType *func_type);
ST_FUNC void gen_va_start(void);
ST_FUNC void gen_va_arg(CType *t);
ST_FUNC void gen_clear_cache(void);
ST_FUNC void gen_cvt_sxtw(void);
ST_FUNC void gen_cvt_csti(int t);
ST_FUNC void gen_increment_tcov (SValue *sv);
#endif

/* ------------ riscv64-gen.c ------------ */
#ifdef TCC_TARGET_RISCV64
ST_FUNC void gen_opl(int op);
ST_FUNC void gen_va_start(void);
ST_FUNC void arch_transfer_ret_regs(int);
ST_FUNC void gen_cvt_sxtw(void);
ST_FUNC void gen_cvt_csti(int t);
ST_FUNC void gen_increment_tcov (SValue *sv);
ST_FUNC void gen_clear_cache(void);
#endif

/* ------------ c67-gen.c ------------ */
#ifdef TCC_TARGET_C67
#endif

/* ------------ tcccoff.c ------------ */
#ifdef TCC_TARGET_COFF
ST_FUNC int tcc_output_coff(TCCState *s1, FILE *f);
ST_FUNC int tcc_load_coff(TCCState * s1, int fd);
#endif

/* ------------ tccasm.c ------------ */
ST_FUNC void asm_instr(void);
ST_FUNC void asm_global_instr(void);
ST_FUNC int tcc_assemble(TCCState *s1, int do_preprocess);
#ifdef CONFIG_TCC_ASM
ST_FUNC int find_constraint(ASMOperand *operands, int nb_operands, const char *name, const char **pp);
ST_FUNC Sym* get_asm_sym(int name, Sym *csym);
ST_FUNC void asm_expr(TCCState *s1, ExprValue *pe);
ST_FUNC int asm_int_expr(TCCState *s1);
#if PTR_SIZE == 8
ST_FUNC void gen_expr64(ExprValue *pe);
#endif
/* ------------ i386-asm.c ------------ */
ST_FUNC void gen_expr32(ExprValue *pe);
ST_FUNC void asm_opcode(TCCState *s1, int opcode);
ST_FUNC int asm_parse_regvar(int t);
ST_FUNC void asm_compute_constraints(ASMOperand *operands, int nb_operands, int nb_outputs, const uint8_t *clobber_regs, int *pout_reg);
ST_FUNC void subst_asm_operand(CString *add_str, SValue *sv, int modifier);
ST_FUNC void asm_gen_code(ASMOperand *operands, int nb_operands, int nb_outputs, int is_output, uint8_t *clobber_regs, int out_reg);
ST_FUNC void asm_clobber(uint8_t *clobber_regs, const char *str);
#endif

/* ------------ tccpe.c -------------- */
#ifdef TCC_TARGET_PE
ST_FUNC int pe_load_file(struct TCCState *s1, int fd, const char *filename);
ST_FUNC int pe_output_file(TCCState * s1, const char *filename);
ST_FUNC int pe_putimport(TCCState *s1, int dllindex, const char *name, addr_t value);
ST_FUNC int pe_setsubsy(TCCState *s1, const char *arg);
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64
#endif
#if defined(TCC_TARGET_X86_64) || defined(TCC_TARGET_ARM64)
ST_FUNC void pe_add_unwind_data(unsigned start, unsigned end, unsigned stack);
#endif
PUB_FUNC int tcc_get_dllexports(const char *filename, char **pp);
/* symbol properties stored in `Elf32_Sym`->`st_other` */
# define ST_PE_EXPORT 0x10 /**< `PE` symbol export attribute flag */
# define ST_PE_IMPORT 0x20 /**< `PE` symbol import attribute flag */
# define ST_PE_STDCALL 0x40 /**< `PE` stdcall calling convention flag */
#endif
#define ST_ASM_SET 0x04 /**< `Assembler` set symbol flag */

/* ------------ tccmacho.c ----------------- */
#ifdef TCC_TARGET_MACHO
ST_FUNC int macho_output_file(TCCState * s1, const char *filename);
ST_FUNC int macho_load_dll(TCCState *s1, int fd, const char *filename, int lev);
ST_FUNC int macho_load_tbd(TCCState *s1, int fd, const char *filename, int lev);
#ifdef TCC_IS_NATIVE
ST_FUNC void tcc_add_macos_sdkpath(TCCState* s);
ST_FUNC char* macho_tbd_soname(int fd);
#endif
#endif
/* ------------ tccrun.c ----------------- */
#ifdef TCC_IS_NATIVE
#ifdef CONFIG_TCC_STATIC
#define RTLD_LAZY       0x001 /**< `Dynamic` loading: lazy symbol binding */
#define RTLD_NOW        0x002 /**< `Dynamic` loading: immediate symbol binding */
#define RTLD_GLOBAL     0x100 /**< `Dynamic` loading: make symbols globally visible */
#define RTLD_DEFAULT    NULL /**< `Dynamic` loading default symbol handle */
/* dummy function for profiling */
ST_FUNC void *dlopen(const char *filename, int flag);
ST_FUNC void dlclose(void *p);
ST_FUNC const char *dlerror(void);
ST_FUNC void *dlsym(void *handle, const char *symbol);
#endif
ST_FUNC void tcc_run_free(TCCState *s1);
#endif

/* ------------ tcctools.c ----------------- */
#if 0 /* included in tcc.c */
ST_FUNC int tcc_tool_ar(TCCState *s, int argc, char **argv);
#ifdef TCC_TARGET_PE
ST_FUNC int tcc_tool_impdef(TCCState *s, int argc, char **argv);
#endif
ST_FUNC int tcc_tool_cross(TCCState *s, char **argv, int option);
ST_FUNC int gen_makedeps(TCCState *s, const char *target, const char *filename);
#endif

/* ------------ tccdbg.c ------------ */

ST_FUNC void tcc_debug_new(TCCState *s);

ST_FUNC void tcc_debug_start(TCCState *s1);
ST_FUNC void tcc_debug_end(TCCState *s1);
ST_FUNC void tcc_debug_bincl(TCCState *s1);
ST_FUNC void tcc_debug_eincl(TCCState *s1);
ST_FUNC void tcc_debug_newfile(TCCState *s1);

ST_FUNC void tcc_debug_line(TCCState *s1);
ST_FUNC void tcc_add_debug_info(TCCState *s1, Sym *s, Sym *e);
ST_FUNC void tcc_debug_funcstart(TCCState *s1, Sym *sym);
ST_FUNC void tcc_debug_prolog_epilog(TCCState *s1, int value);
ST_FUNC void tcc_debug_funcend(TCCState *s1, int size);
ST_FUNC void tcc_debug_extern_sym(TCCState *s1, Sym *sym, int sh_num, int sym_bind, int sym_type);
ST_FUNC void tcc_debug_typedef(TCCState *s1, Sym *sym);
ST_FUNC void tcc_debug_stabn(TCCState *s1, int type, int value);
ST_FUNC void tcc_debug_fix_forw(TCCState *s1, CType *t);

#if !(defined ELF_OBJ_ONLY || defined TCC_TARGET_ARM || defined TARGETOS_BSD)
ST_FUNC void tcc_eh_frame_start(TCCState *s1);
ST_FUNC void tcc_eh_frame_end(TCCState *s1);
ST_FUNC void tcc_eh_frame_hdr(TCCState *s1, int final);
#define TCC_EH_FRAME 1 /**< `Enable` exception handling frame generation */
#endif

ST_FUNC void tcc_tcov_start(TCCState *s1);
ST_FUNC void tcc_tcov_end(TCCState *s1);
ST_FUNC void tcc_tcov_check_line(TCCState *s1, int start);
ST_FUNC void tcc_tcov_block_end(TCCState *s1, int line);
ST_FUNC void tcc_tcov_block_begin(TCCState *s1);
ST_FUNC void tcc_tcov_reset_ind(TCCState *s1);

#define stab_section            s1->stab_section /**< `Accessor` for debug stabs section */
#define stabstr_section         stab_section->link /**< `Accessor` for stab string table section */
#define tcov_section            s1->tcov_section /**< `Accessor` for code coverage section */
#define eh_frame_section        s1->eh_frame_section /**< `Accessor` for exception handler frame section */
#define eh_frame_hdr_section    s1->eh_frame_hdr_section /**< `Accessor` for exception handler frame header section */
#define dwarf_info_section      s1->dwarf_info_section /**< `Accessor` for `DWARF` debugging info section */
#define dwarf_abbrev_section    s1->dwarf_abbrev_section /**< `Accessor` for `DWARF` abbreviation section */
#define dwarf_line_section      s1->dwarf_line_section /**< `Accessor` for `DWARF` line number section */
#define dwarf_aranges_section   s1->dwarf_aranges_section /**< `Accessor` for `DWARF` address ranges section */
#define dwarf_str_section       s1->dwarf_str_section /**< `Accessor` for `DWARF` string table section */
#define dwarf_line_str_section  s1->dwarf_line_str_section /**< `Accessor` for `DWARF` line string table section */

#define DWARF_MAX_128	((8 * sizeof (int64_t) + 6) / 7) /**< `Maximum` byte length for encoded 128-bit `DWARF` leb128 values */
#define	dwarf_read_1(ln,end) \
	((ln) < (end) ? *(ln)++ : 0)
/** `Read` encoded `DWARF` data value (2 bytes) */
#define	dwarf_read_2(ln,end) \
	((ln) + 1 < (end) ? read16le(((ln)+=2) - 2) : 0)
/** `Read` encoded `DWARF` data value (4 bytes) */
#define	dwarf_read_4(ln,end) \
	((ln) + 3 < (end) ? read32le(((ln)+=4) - 4) : 0)
/** `Read` encoded `DWARF` data value (8 bytes) */
#define	dwarf_read_8(ln,end) \
	((ln) + 7 < (end) ? read64le(((ln)+=8) - 8) : 0)
static inline uint64_t
dwarf_read_uleb128(unsigned char **ln, unsigned char *end)
{
    unsigned char *cp = *ln;
    uint64_t retval = 0;
    int i;
    for (i = 0; i < DWARF_MAX_128; i++) {
	uint64_t byte = dwarf_read_1(cp, end);
        retval |= (byte & 0x7f) << (i * 7);
	if ((byte & 0x80) == 0)
	    break;
    }
    *ln = cp;
    return retval;
}
static inline int64_t
dwarf_read_sleb128(unsigned char **ln, unsigned char *end)
{
    unsigned char *cp = *ln;
    int64_t retval = 0;
    int i;
    for (i = 0; i < DWARF_MAX_128; i++) {
	uint64_t byte = dwarf_read_1(cp, end);
        retval |= (byte & 0x7f) << (i * 7);
	if ((byte & 0x80) == 0) {
	    if ((byte & 0x40) && (i + 1) * 7 < 64)
		retval |= (uint64_t)-1LL << ((i + 1) * 7);
	    break;
	}
    }
    *ln = cp;
    return retval;
}

/* default dwarf version for "-gdwarf" */
#ifdef TCC_TARGET_MACHO
# define DEFAULT_DWARF_VERSION 2
#else
# define DEFAULT_DWARF_VERSION 5
#endif

/* default dwarf version for "-g". use 0 to emit stab debug infos */
#ifndef CONFIG_DWARF_VERSION
# define CONFIG_DWARF_VERSION 0
#endif

#if defined TCC_TARGET_X86_64
# define R_DATA_32DW R_X86_64_32
#else
# define R_DATA_32DW R_DATA_32
#endif

/********************************************************/
#if CONFIG_TCC_SEMLOCK
#if defined _WIN32
/**
 * @brief Cross-platform counting semaphore used to serialize compiler state access.
 */
typedef struct { volatile LONG init; CRITICAL_SECTION cs; } TCCSem;
static inline void wait_sem(TCCSem *p) {
    if (InterlockedCompareExchange(&p->init, 1, 0) == 0) {
        InitializeCriticalSection(&p->cs);
        InterlockedExchange(&p->init, 2);
    } else {
        while (InterlockedCompareExchange(&p->init, 2, 2) != 2)
            Sleep(0);
    }
    EnterCriticalSection(&p->cs);
}
static inline void post_sem(TCCSem *p) {
    LeaveCriticalSection(&p->cs);
}
#elif defined __APPLE__
#include <dispatch/dispatch.h>
typedef struct { int init; dispatch_semaphore_t sem; } TCCSem;
static inline void wait_sem(TCCSem *p) {
    if (!p->init)
        p->sem = dispatch_semaphore_create(1), p->init = 1;
    dispatch_semaphore_wait(p->sem, DISPATCH_TIME_FOREVER);
}
static inline void post_sem(TCCSem *p) {
    dispatch_semaphore_signal(p->sem);
}
#else
#include <semaphore.h>
typedef struct { int init; sem_t sem; } TCCSem;
static inline void wait_sem(TCCSem *p) {
    if (!p->init)
        sem_init(&p->sem, 0, 1), p->init = 1;
    while (sem_wait(&p->sem) < 0 && errno == EINTR);
}
static inline void post_sem(TCCSem *p) {
    sem_post(&p->sem);
}
#endif
#define TCC_SEM(s) TCCSem s /**< `Thread` synchronization semaphore type definition */
#define WAIT_SEM wait_sem /**< `Wait` for thread synchronization semaphore */
#define POST_SEM post_sem /**< `Signal` thread synchronization semaphore */
#else
#define TCC_SEM(s) /**< `Thread` synchronization semaphore type definition */
#define WAIT_SEM(p) /**< `Wait` for thread synchronization semaphore */
#define POST_SEM(p) /**< `Signal` thread synchronization semaphore */
#endif

/********************************************************/
#undef ST_DATA
#if ONE_SOURCE
#define ST_DATA static TCC_TLS /**< `Storage` specifier macro for compiler state data */
#else
#define ST_DATA TCC_TLS /**< `Storage` specifier macro for compiler state data */
#endif
/********************************************************/

#define text_section        TCC_STATE_VAR(text_section) /**< `State` section accessor: text section */
#define data_section        TCC_STATE_VAR(data_section) /**< `State` section accessor: initialized data section */
#define rodata_section      TCC_STATE_VAR(rodata_section) /**< `State` section accessor: read-only data section */
#define bss_section         TCC_STATE_VAR(bss_section) /**< `State` section accessor: uninitialized data section */
#define common_section      TCC_STATE_VAR(common_section) /**< `State` section accessor: common symbols section */
#define cur_text_section    TCC_STATE_VAR(cur_text_section) /**< `State` section accessor: current active text section */
#define bounds_section      TCC_STATE_VAR(bounds_section) /**< `State` section accessor: bounds checking metadata section */
#define lbounds_section     TCC_STATE_VAR(lbounds_section) /**< `State` section accessor: local bounds section */
#define symtab_section      TCC_STATE_VAR(symtab_section) /**< `State` section accessor: symbol table section */
#define gnu_ext             TCC_STATE_VAR(gnu_ext) /**< `State` flag: `GNU` extensions enabled */
#define tcc_error_noabort   TCC_SET_STATE(_tcc_error_noabort) /**< `State` function accessor: non-aborting error handler */
#define tcc_error           TCC_SET_STATE(_tcc_error) /**< `State` function accessor: compilation error reporter */
#define tcc_warning         TCC_SET_STATE(_tcc_warning) /**< `State` function accessor: compilation warning reporter */

#define total_idents        TCC_STATE_VAR(total_idents) /**< `State` metric: total unique identifiers */
#define total_tokens        TCC_STATE_VAR(total_tokens) /**< `State` metric: total processed tokens */
#define total_lines         TCC_STATE_VAR(total_lines) /**< `State` metric: total compiled source lines */
#define total_bytes         TCC_STATE_VAR(total_bytes) /**< `State` metric: total compiled output bytes */

PUB_FUNC void tcc_enter_state(TCCState *s1);
PUB_FUNC void tcc_exit_state(TCCState *s1);

/* conditional warning depending on switch */
/** `Issue` conditional warning depending on compiler switch */
#define tcc_warning_c(sw) TCC_SET_STATE((\
    tcc_state->warn_num = offsetof(TCCState, sw) \
    - offsetof(TCCState, warn_none), _tcc_warning))

/********************************************************/
#endif /* `_TCC_H` */

#undef TCC_STATE_VAR
#undef TCC_SET_STATE

#ifdef USING_GLOBALS
# define TCC_STATE_VAR(sym) tcc_state->sym
# define TCC_SET_STATE(fn) fn
# undef USING_GLOBALS
# undef _tcc_error
#else
# define TCC_STATE_VAR(sym) s1->sym
# define TCC_SET_STATE(fn) (tcc_enter_state(s1),fn)
# define _tcc_error use_tcc_error_noabort
#endif
