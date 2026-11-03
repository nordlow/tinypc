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

#define USING_GLOBALS
#include "tcc.h"

/********************************************************/
/* global variables */

/* loc : local variable index
   ind : output code index
   rsym: return symbol
   `anon_sym`: anonymous symbol index
*/
ST_DATA int rsym, anon_sym, ind, loc;

ST_DATA Sym *global_stack;
ST_DATA Sym *local_stack;
ST_DATA Sym *define_stack;
ST_DATA Sym *global_label_stack;
ST_DATA Sym *local_label_stack;

static TCC_TLS Sym *sym_free_first;
static TCC_TLS void **sym_pools;
static TCC_TLS int nb_sym_pools;
static TCC_TLS TCCState *cur_tcc_state;


static TCC_TLS Sym *all_cleanups, *pending_gotos;
static TCC_TLS int local_scope;
/* function symbol whose K&R parameter list is being declared */
static TCC_TLS Sym *kr_params;
/* safety level of a compound block whose attributes were parsed by
   decl() before the block itself was seen */
static TCC_TLS FuncSafety pending_block_safety;
static TCC_TLS FuncPurity pending_block_pure;
ST_DATA char debug_modes;

ST_DATA SValue *vtop;
static TCC_TLS SValue _vstack[1 + VSTACK_SIZE];
#define vstack (_vstack + 1)

ST_DATA int nocode_wanted; /* no code generation wanted */
#define NODATA_WANTED (nocode_wanted > 0) /* no static data output wanted either */
#define DATA_ONLY_WANTED 0x80000000 /* `ON` outside of functions and for static initializers */

/* no code output after unconditional jumps such as with if (0) ... */
#define CODE_OFF_BIT 0x20000000
#define CODE_OFF() if(!nocode_wanted)(nocode_wanted |= CODE_OFF_BIT)
#define CODE_ON() (nocode_wanted &= ~CODE_OFF_BIT)

/* no code output when parsing sizeof()/typeof() etc. (using `nocode_wanted`++/--) */
#define NOEVAL_MASK 0x0000FFFF
#define NOEVAL_WANTED (nocode_wanted & NOEVAL_MASK)

/* no code output when parsing constant expressions */
#define CONST_WANTED_BIT  0x00010000
#define CONST_WANTED_MASK 0x0FFF0000
#define CONST_WANTED  (nocode_wanted & CONST_WANTED_MASK)

ST_DATA int global_expr;  /* true if compound literals must be allocated globally (used during initializers parsing */
ST_DATA CType int_type, func_old_type, char_type, char_pointer_type;
static TCC_TLS CString initstr;

/* `The` lexical scope stack.  `Slot` 0 is the file-scope sentinel so that
   `cur_scope` is never `NULL`.  */
static TCC_TLS Scope scope_stack[MAX_SCOPE_DEPTH + 1];
static TCC_TLS int scope_depth;
/* effective limit, `MAX_SCOPE_DEPTH` unless lowered by -fscope-depth=N */
static TCC_TLS int scope_depth_limit;
ST_DATA Scope *cur_scope, *loop_scope, *root_scope;

#if PTR_SIZE == 4
#define VT_SIZE_T (VT_INT | VT_UNSIGNED)
#define VT_PTRDIFF_T VT_INT
#elif LONG_SIZE == 4
#define VT_SIZE_T (VT_LLONG | VT_UNSIGNED)
#define VT_PTRDIFF_T VT_LLONG
#else
#define VT_SIZE_T (VT_LONG | VT_LLONG | VT_UNSIGNED)
#define VT_PTRDIFF_T (VT_LONG | VT_LLONG)
#endif

static TCC_TLS struct switch_t {
    struct case_t {
        int64_t v1, v2;
        int ind, line;
    } **p; int n; /* list of case ranges */
    int def_sym; /* default symbol */
    int nocode_wanted;
    int *bsym;
    Scope *scope;
    struct switch_t *prev;
    SValue sv;
} *cur_switch; /* current switch */

#define MAX_TEMP_LOCAL_VARIABLE_NUMBER 8
/*list of temporary local variables on the stack in current function. */
static TCC_TLS struct temp_local_variable {
	int location; //offset on stack. Svalue.c.i
	short size;
	short align;
} arr_temp_local_vars[MAX_TEMP_LOCAL_VARIABLE_NUMBER];
static TCC_TLS int nb_temp_local_vars;

/* '$' inside a subscript expression denotes the length of the
   subscripted array or slice.  */
struct dollar_ctx {
    CType type;
    SValue sv;
};
static TCC_TLS struct dollar_ctx dollar_ctx_stack[16];
static TCC_TLS int nb_dollar_ctx;

typedef struct {
    Section *sec;
    int local_offset;
    Sym *flex_array_ref;
} init_params;

static void init_prec(void);

static void block(int flags);
#define STMT_EXPR 1
#define STMT_COMPOUND 2

static void gen_cast(CType *type);
static void gen_cast_s(int t);
static inline CType *pointed_type(CType *type);
static void check_restrict_type(CType *type);
static int type_qualifiers(CType *type);
static void parse_btype_qualify(CType *type, int qualifiers);
static int is_compatible_types(CType *type1, CType *type2);
static int parse_btype(CType *type, AttrDef *ad, int ignore_label);
static CType *type_decl(CType *type, AttrDef *ad, int *v, int td);
static void parse_expr_type(CType *type);
static void init_putv(init_params *p, CType *type, unsigned long c);
static void decl_initializer(init_params *p, CType *type, unsigned long c, int flags);
static void decl_initializer_alloc(CType *type, AttrDef *ad, int r, int has_init, int v, int scope);
static int decl(int l);
static void expr_eq(void);
static void vpush_type_size(CType *type, int *a);
static int is_compatible_unqualified_types(CType *type1, CType *type2);
static inline int64_t expr_const64(void);
static void vpush64(int ty, unsigned long long v);
static void vpush(CType *type);
static int gvtst(int inv, int t);
static void gen_inline_functions(TCCState *s);
static void free_inline_functions(TCCState *s);
static void skip_or_save_block(TokenString **str);
static void gv_dup(void);
static int get_temp_local_var(int size,int align,int *r2);
static void cast_error(CType *st, CType *dt);
static int is_tuple_type(CType *type);
static Sym *tuple_field(CType *type, int idx, int *pofs);
static int tuple_arity(CType *type);
static Sym *tuple_begin(void);
static void tuple_add_field(Sym ***pplast, CType *elem, int idx);
static Sym *tuple_layout(Sym *s);
static void gen_members_from_type(CType type1);
static int type_is(CType *type, int kind);
static int type_has_indirections(CType *type);
static CType *type_value_of(Sym *s);
static Sym *make_string_sym(const char *s);
static void free_tuple_consts(void);
static Sym *parse_tuple_type_opt(CType *type, AttrDef *ad);
static void parse_tuple_type_rest(CType *type, AttrDef *ad);
static int is_slice_type(CType *type);
static Sym *make_slice_type(CType *elem);
static int convert_array_to_slice(CType *dt);
static int copy_slice_to_temp(CType *type);
static void d_array_suffix(CType *type, int storage);
static void end_switch(void);
static int check_d_static_assert(void);
static void do_Static_assert(void);
static void do_d_assert(void);
static int check_d_manifest_constant(int l);
static int check_d_conditional(int l, int flags);
static int check_d_dollar_if(int l, int flags);
static int check_d_dollar_foreach(int l, int flags);
static void gen_slice_assign(int assign_tok);
static void expr_cond(void);
static void gen_tuple_literal(CType *type);
static void call_arg_expr(Sym *sa);
static int try_tuple_assign(void);
static void type_to_str(char *buf, int buf_size, CType *type, const char *varstr);
static TCC_TLS int slice_range_target;
static TCC_TLS CType *tuple_expect;
/* `Automagical` code suppression */

/* `Clear` '`nocode_wanted`' at forward label if it was used */
ST_FUNC void gsym(int t)
{
  if (t) {
    gsym_addr(t, ind);
    CODE_ON();
  }
}

/* `Clear` '`nocode_wanted`' if current pc is a label */
static int gind()
{
  int t = ind;
  CODE_ON();
  if (debug_modes)
    tcc_tcov_block_begin(tcc_state);
  return t;
}

/* `Set` '`nocode_wanted`' after unconditional (backwards) jump */
static void gjmp_addr_acs(int t)
{
  gjmp_addr(t);
  CODE_OFF();
}

/* `Set` '`nocode_wanted`' after unconditional (forwards) jump */
static int gjmp_acs(int t)
{
  t = gjmp(t);
  CODE_OFF();
  return t;
}

/* `These` are #undef'd at the end of this file */
#define gjmp_addr gjmp_addr_acs
#define gjmp gjmp_acs
/* ------------------------------------------------------------------------- */

/* `Returns` true if type flags `t` (extracted basic type `bt` via `VT_BTYPE`)
   represents a floating-point type (`VT_LDOUBLE`, `VT_DOUBLE`, `VT_FLOAT`, `VT_QFLOAT`). */
ST_INLN int is_float(int t)
{
    int bt = t & VT_BTYPE;
    return bt == VT_LDOUBLE
        || bt == VT_DOUBLE
        || bt == VT_FLOAT
        || bt == VT_QFLOAT;
}

/* `Returns` true if basic type `bt` (extracted via `VT_BTYPE`) represents
   an integer type (`VT_BYTE`, `VT_BOOL`, `VT_SHORT`, `VT_INT`, `VT_LLONG`). */
static inline int is_integer_btype(int bt)
{
    return bt == VT_BYTE
        || bt == VT_BOOL
        || bt == VT_SHORT
        || bt == VT_INT
        || bt == VT_LLONG;
}

/* `Returns` the byte size of basic type `bt` (extracted via `VT_BTYPE`),
   supporting `VT_BYTE`, `VT_BOOL`, `VT_SHORT`, `VT_INT`, `VT_LLONG`, and `VT_PTR`. */
static int btype_size(int bt)
{
    return bt == VT_BYTE || bt == VT_BOOL ? 1 :
        bt == VT_SHORT ? 2 :
        bt == VT_INT ? 4 :
        bt == VT_LLONG ? 8 :
        bt == VT_PTR ? PTR_SIZE : 0;
}

/* `Returns` function primary return register for type flags `t`
   (masked by `VT_BTYPE`, e.g., `VT_LDOUBLE`). */
static int R_RET(int t)
{
    if (!is_float(t))
        return REG_IRET;
#ifdef TCC_TARGET_X86_64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return TREG_ST0;
#elif defined TCC_TARGET_RISCV64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return REG_IRET;
#endif
    return REG_FRET;
}

/* `Returns` 2nd function return register, if any, for type flags `t`
   (masked by `VT_BTYPE`, e.g., `VT_LLONG`, `VT_QLONG`, `VT_QFLOAT`, `VT_LDOUBLE`). */
static int R2_RET(int t)
{
    t &= VT_BTYPE;
#if PTR_SIZE == 4
    if (t == VT_LLONG)
        return REG_IRE2;
#elif defined TCC_TARGET_X86_64
    if (t == VT_QLONG)
        return REG_IRE2;
    if (t == VT_QFLOAT)
        return REG_FRE2;
#elif defined TCC_TARGET_RISCV64
    if (t == VT_LDOUBLE)
        return REG_IRE2;
#endif
    return VT_CONST;
}

/* `Returns` true for two-word types based on `R2_RET`(`t`). */
#define USING_TWO_WORDS(t) (R2_RET(t) != VT_CONST)

/* `Put` function return registers (`R_RET` and `R2_RET`) to stack value sv
   for type flags `t`. */
static void PUT_R_RET(SValue *sv, int t)
{
    sv->r = R_RET(t), sv->r2 = R2_RET(t);
}

/* `Returns` function return register class for type flags `t`. */
static int RC_RET(int t)
{
    return reg_classes[R_RET(t)] & ~(RC_FLOAT | RC_INT);
}

/* `Returns` generic register class for type flags `t`
   (handling `VT_LDOUBLE` and `VT_QFLOAT` target specifics). */
static int RC_TYPE(int t)
{
    if (!is_float(t))
        return RC_INT;
#ifdef TCC_TARGET_X86_64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return RC_ST0;
    if ((t & VT_BTYPE) == VT_QFLOAT)
        return RC_FRET;
#elif defined TCC_TARGET_RISCV64
    if ((t & VT_BTYPE) == VT_LDOUBLE)
        return RC_INT;
#endif
    return RC_FLOAT;
}

/* `Returns` 2nd register class corresponding to type flags `t` and register class `rc`. */
static int RC2_TYPE(int t, int rc)
{
    if (!USING_TWO_WORDS(t))
        return 0;
#ifdef RC_IRE2
    if (rc == RC_IRET)
        return RC_IRE2;
#endif
#ifdef RC_FRE2
    if (rc == RC_FRET)
        return RC_FRE2;
#endif
    if (rc & RC_FLOAT)
        return RC_FLOAT;
    return RC_INT;
}

/* we use our own 'finite' function to avoid potential problems with
   non standard math libs */
/* `XXX`: endianness dependent */
ST_FUNC int ieee_finite(double d)
{
    int p[4];
    memcpy(p, &d, sizeof(double));
    return ((unsigned)((p[1] | 0x800fffff) + 1)) >> 31;
}

ST_FUNC void test_lvalue(void)
{
    if (!(vtop->r & VT_LVAL))
        expect("lvalue");
}

/* dump 'b' vstack entries starting with 'a'  */
void pv (const char *lbl, int a, int b)
{
    int i;
    for (i = a; i < a + b; ++i) {
        SValue *p = &vtop[-i];
        char tbuf[256];
        const char *sym_name = NULL;
        type_to_str(tbuf, sizeof(tbuf), &p->type, NULL);
        if ((p->r & VT_SYM) && p->sym && p->sym->v)
            sym_name = get_tok_str(p->sym->v, 0);
        printf("%s vtop[-%d] : type.t:%04x (%s) r:%04x  r2:%04x  c.i:%d%s%s\n",
            lbl, i, p->type.t, tbuf, p->r, p->r2, (int)p->c.i,
            sym_name ? " sym:" : "", sym_name ? sym_name : "");
    }
}

/* print the whole vstack after a mutation, for -fdump-vstack */
static void dump_vstack(const char *op)
{
    if (cur_tcc_state && cur_tcc_state->dump_vstack) {
        int n = vtop - vstack + 1;
        printf("VSTACK %s (%d)\n", op, n);
        pv("VSTACK", 0, n);
    }
}

/* dump symbols on stack from s ... last */
static inline void psyms(const char *msg, Sym *s, Sym *last)
{
    printf("%-8s scope         v        c        r   type.t\n", msg);
    while (s && s != last) {
        printf("      %8x  %08x %08x %08x %08x %s\n",
            s->sym_scope, s->v, s->c, s->r, s->type.t, get_tok_str(s->v, 0));
        s = s->prev;
    }
}

static void type_to_str(char *buf, int buf_size, CType *type, const char *varstr);

/* print type */
static void ptype(const char *msg, CType *type, int v)
{
    char buf[500];
    type_to_str(buf, sizeof(buf), type,
        (v & ~SYM_FIELD) ? get_tok_str(v, NULL) : NULL);
    printf("%s : %s;\n", msg, buf);
}

ST_FUNC void check_vstack(void)
{
    dump_vstack("check");
    if (vtop != vstack - 1)
        tcc_error("internal compiler error: vstack leak (%d)",
                  (int)(vtop - vstack + 1));
}
/* ------------------------------------------------------------------------- */
/* initialize vstack and types.  `This` must be done also for tcc -E */
ST_FUNC void tccgen_init(TCCState *s1)
{
    cur_tcc_state = s1;
    vtop = vstack - 1;
    memset(vtop, 0, sizeof *vtop);
    nb_dollar_ctx = 0;

    /* reset the scope stack; slot 0 is the file-scope sentinel */
    scope_depth = 0;
    scope_depth_limit = s1->max_scope_depth;
    if (!scope_depth_limit)
        scope_depth_limit = MAX_SCOPE_DEPTH;
    cur_scope = root_scope = &scope_stack[0];
    loop_scope = NULL;
    memset(&scope_stack[0], 0, sizeof scope_stack[0]);
    scope_stack[0].fn.name = "";
    scope_stack[0].fn.ind = -1;

    /* define some often used types */
    int_type.t = VT_INT;

    char_type.t = VT_BYTE;
    if (s1->char_is_unsigned)
        char_type.t |= VT_UNSIGNED;
    char_pointer_type = char_type;
    mk_pointer(&char_pointer_type);

    func_old_type.t = VT_FUNC;
    func_old_type.ref = sym_push(SYM_FIELD, &int_type, 0, 0);
    func_old_type.ref->f.func_call = FUNC_CDECL;
    func_old_type.ref->f.func_type = FUNC_OLD;
    init_prec();
    cstr_new(&initstr);
}

ST_FUNC int tccgen_compile(TCCState *s1)
{
    cur_tcc_state = s1;
    funcname = "";
    func_ind = -1;
    anon_sym = SYM_FIRST_ANOM;
    free_tuple_consts();
    nocode_wanted = DATA_ONLY_WANTED; /* no code outside of functions */
    debug_modes = (s1->do_debug ? 1 : 0) | s1->test_coverage << 1;
    global_expr = 0;

    tcc_debug_start(s1);
    tcc_tcov_start (s1);
#ifdef TCC_TARGET_ARM
    arm_init(s1);
#endif
#ifdef INC_DEBUG
    printf("%s: **** new file\n", file->filename);
#endif
    parse_flags = PARSE_FLAG_PREPROCESS | PARSE_FLAG_TOK_NUM | PARSE_FLAG_TOK_STR;
    next();
    decl(VT_CONST);
    gen_inline_functions(s1);
    check_vstack();
    /* end of translation unit info */
#if TCC_EH_FRAME
    tcc_eh_frame_end(s1);
#endif
    tcc_debug_end(s1);
    tcc_tcov_end(s1);
    return 0;
}

ST_FUNC void tccgen_finish(TCCState *s1)
{
    tcc_debug_end(s1); /* just in case of errors: free memory */
    free_inline_functions(s1);
    sym_pop(&global_stack, NULL, 0);
    sym_pop(&local_stack, NULL, 0);
    /* free preprocessor macros */
    free_defines(NULL);
    /* free `sym_pools` */
    dynarray_reset(&sym_pools, &nb_sym_pools);
    cstr_free(&initstr);
    dynarray_reset(&stk_data, &nb_stk_data);
    while (cur_switch)
        end_switch();
    local_scope = 0;
    loop_scope = NULL;
    all_cleanups = NULL;
    pending_gotos = NULL;
    kr_params = NULL;
    pending_block_safety = 0;
    pending_block_pure = 0;
    scope_depth = 0;
    scope_depth_limit = 0;
    cur_scope = root_scope = &scope_stack[0];
    nb_temp_local_vars = 0;
    global_label_stack = NULL;
    local_label_stack = NULL;
    cur_text_section = NULL;
    sym_free_first = NULL;
}

/* ------------------------------------------------------------------------- */
ST_FUNC ElfSym *elfsym(Sym *s)
{
  if (!s || !s->c)
    return NULL;
  return &((ElfSym *)symtab_section->data)[s->c];
}

/* apply storage attributes to `Elf` symbol */
ST_FUNC void update_storage(Sym *sym)
{
    ElfSym *esym;
    int sym_bind, old_sym_bind;

    esym = elfsym(sym);
    if (!esym)
        return;

    if (sym->a.visibility)
        esym->st_other = (esym->st_other & ~ELFW(ST_VISIBILITY)(-1))
            | sym->a.visibility;

    if (sym->type.t & (VT_STORAGE_STATIC | VT_STORAGE_INLINE))
        sym_bind = STB_LOCAL;
    else if (sym->a.weak)
        sym_bind = STB_WEAK;
    else
        sym_bind = STB_GLOBAL;
    old_sym_bind = ELFW(ST_BIND)(esym->st_info);
    if (sym_bind != old_sym_bind) {
        esym->st_info = ELFW(ST_INFO)(sym_bind, ELFW(ST_TYPE)(esym->st_info));
    }

#ifdef TCC_TARGET_PE
    if (sym->a.dllimport)
        esym->st_other |= ST_PE_IMPORT;
    if (sym->a.dllexport)
        esym->st_other |= ST_PE_EXPORT;
#endif

#if 0
    printf("storage %s: bind=%c vis=%d exp=%d imp=%d\n",
        get_tok_str(sym->v, NULL),
        sym_bind == STB_WEAK ? 'w' : sym_bind == STB_LOCAL ? 'l' : 'g',
        sym->a.visibility,
        sym->a.dllexport,
        sym->a.dllimport
        );
#endif
}

/* ------------------------------------------------------------------------- */
/* update sym->c so that it points to an external symbol in section
   'section' with value 'value' */

ST_FUNC void put_extern_sym2(Sym *sym, int sh_num,
                            addr_t value, unsigned long size,
                            int can_add_underscore)
{
    int sym_type, sym_bind, info, other, t;
    ElfSym *esym;
    const char *name;
    char buf1[256];

    if (!sym->c) {
        name = get_tok_str(sym->v, NULL);
        t = sym->type.t;
        if ((t & VT_BTYPE) == VT_FUNC) {
            sym_type = STT_FUNC;
        } else if ((t & VT_BTYPE) == VT_VOID) {
            sym_type = STT_NOTYPE;
            if (IS_ASM_FUNC(t))
                sym_type = STT_FUNC;
        } else if (t & VT_STORAGE_TLS) {
            sym_type = STT_TLS;
        } else {
            sym_type = STT_OBJECT;
        }
        if (t & (VT_STORAGE_STATIC | VT_STORAGE_INLINE))
            sym_bind = STB_LOCAL;
        else
            sym_bind = STB_GLOBAL;
        other = 0;

#ifdef TCC_TARGET_PE
        if (sym_type == STT_FUNC && sym->type.ref) {
            Sym *ref = sym->type.ref;
            if (ref->a.nodecorate) {
                can_add_underscore = 0;
            }
            if (ref->f.func_call == FUNC_STDCALL && can_add_underscore) {
                sprintf(buf1, "_%s@%d", name, ref->f.func_args * PTR_SIZE);
                name = buf1;
                other |= ST_PE_STDCALL;
                can_add_underscore = 0;
            }
        }
#endif

        if (sym->asm_label) {
            name = get_tok_str(sym->asm_label, NULL);
            can_add_underscore = 0;
        }

        if (tcc_state->leading_underscore && can_add_underscore) {
            buf1[0] = '_';
            pstrcpy(buf1 + 1, sizeof(buf1) - 1, name);
            name = buf1;
        }

        info = ELFW(ST_INFO)(sym_bind, sym_type);
        sym->c = put_elf_sym(symtab_section, value, size, info, other, sh_num, name);

        if (debug_modes)
            tcc_debug_extern_sym(tcc_state, sym, sh_num, sym_bind, sym_type);

    } else {
        esym = elfsym(sym);
        esym->st_value = value;
        esym->st_size = size;
        esym->st_shndx = sh_num;
    }
    update_storage(sym);
}

ST_FUNC void put_extern_sym(Sym *sym, Section *s, addr_t value, unsigned long size)
{
    if (nocode_wanted && (NODATA_WANTED || (s && s == cur_text_section)))
        return;
    put_extern_sym2(sym, s ? s->sh_num : SHN_UNDEF, value, size, 1);
}

/* add a new relocation entry to symbol 'sym' in section 's' */
ST_FUNC void greloca(Section *s, Sym *sym, unsigned long offset, int type,
                     addr_t addend)
{
    int c = 0;

    if (nocode_wanted && s == cur_text_section)
        return;

    if (sym) {
        if (0 == sym->c) {
            put_extern_sym(sym, NULL, 0, 0);
            if (sym->sym_scope
                && (sym->type.t & (VT_STORAGE_STATIC|VT_STORAGE_EXTERN)) == (VT_STORAGE_STATIC|VT_STORAGE_EXTERN)) {
                /* when a local function declaraion redeclares a global static one
                   then tccelf would not resolve them to the same symbol. */
                Sym *s = sym;
                while (s->prev_tok)
                    s = s->prev_tok;
                s->c = sym->c;
            }
        }
        c = sym->c;
    }

    /* now we can add `ELF` relocation info */
    put_elf_reloca(symtab_section, s, offset, type, c, addend);
}

#if PTR_SIZE == 4
ST_FUNC void greloc(Section *s, Sym *sym, unsigned long offset, int type)
{
    greloca(s, sym, offset, type, 0);
}
#endif

/* ------------------------------------------------------------------------- */
/* symbol allocator */
static Sym *__sym_malloc(void)
{
    Sym *sym_pool, *sym, *last_sym;
    int i;

    sym_pool = tcc_malloc(SYM_POOL_NB * sizeof(Sym));
    dynarray_add(&sym_pools, &nb_sym_pools, sym_pool);

    last_sym = sym_free_first;
    sym = sym_pool;
    for(i = 0; i < SYM_POOL_NB; i++) {
        sym->next = last_sym;
        last_sym = sym;
        sym++;
    }
    sym_free_first = last_sym;
    return last_sym;
}

static inline Sym *sym_malloc(void)
{
    Sym *sym;
#ifndef SYM_DEBUG
    sym = sym_free_first;
    if (!sym)
        sym = __sym_malloc();
    sym_free_first = sym->next;
    return sym;
#else
    sym = tcc_malloc(sizeof(Sym));
    return sym;
#endif
}

static inline Sym *sym_calloc(void)
{
	/* TODO: Is there a more efficient way to doing this? */
    Sym *sym = sym_malloc();
    memset(sym, 0, sizeof *sym);
    return sym;
}

ST_INLN void sym_free(Sym *sym)
{
#ifndef SYM_DEBUG
    sym->next = sym_free_first;
    sym_free_first = sym;
#else
    tcc_free(sym);
#endif
}

/* push, without hashing */
ST_FUNC Sym *sym_push2(Sym **ps, int v, int t, int c)
{
    Sym *s;

    s = sym_calloc();
    s->v = v;
    s->type.t = t;
    s->c = c;
    /* add in stack */
    s->prev = *ps;
    *ps = s;
    return s;
}

/* find a symbol and return its associated structure. 's' is the top
   of the symbol stack */
ST_FUNC Sym *sym_find2(Sym *s, int v)
{
    while (s) {
        if (s->v == v)
            return s;
        s = s->prev;
    }
    return NULL;
}

/* structure lookup */
ST_INLN Sym *struct_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_struct;
}

/* find an identifier */
ST_INLN Sym *sym_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_identifier;
}

/* make sym in-/visible to the parser */
static inline void sym_link(Sym *s, int yes)
{
    TokenSym *ts = table_ident[(s->v & ~SYM_STRUCT) - TOK_IDENT];
    Sym **ps;
    if (s->v & SYM_STRUCT)
        ps = &ts->sym_struct;
    else
        ps = &ts->sym_identifier;
    if (yes) {
        s->prev_tok = *ps, *ps = s;
        s->sym_scope = local_scope;
    } else {
        *ps = s->prev_tok;
    }
}

static inline int sym_scope_ex(Sym *s)
{
    /* enums have '`sym_scope`' overwritten by '`enum_val`' */
    if (IS_ENUM_VAL (s->type.t))
        return s->type.ref ? s->type.ref->sym_scope : 0;
    return s->sym_scope;
}

/* push a given symbol on the symbol stack */
ST_FUNC Sym *sym_push(int v, CType *type, int r, int c)
{
    Sym *s, **ps;
    if (local_stack)
        ps = &local_stack;
    else
        ps = &global_stack;
    s = sym_push2(ps, v, type->t, c);
    s->type.ref = type->ref;
    s->r = r;
    /* don't record fields or anonymous symbols */
    if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
        /* record symbol in token array */
        sym_link(s, 1);
        if (s->prev_tok && sym_scope_ex(s->prev_tok) == local_scope)
            tcc_error("redeclaration of '%s'", get_tok_str(s->v, NULL));
    }
    if (cur_tcc_state && cur_tcc_state->dump_decls)
        psyms("DECL", s, s->prev);
    return s;
}

/* `Push` an anonymous enum reference symbol that carries the scope of an
   enum-value symbol: such a symbol stores its value where other symbols
   keep their scope, so the scope is read from the referenced type.  */
static Sym *push_enum_ref(void)
{
    CType type;
    Sym *ref;

    type.t = VT_ENUM;
    type.ref = NULL;
    ref = sym_push(anon_sym++ | SYM_STRUCT, &type, 0, 0);
    ref->sym_scope = local_scope;
    return ref;
}

/* push a global identifier */
ST_FUNC Sym *global_identifier_push(int v, int t, int c)
{
    Sym *s, **ps;
    s = sym_push2(&global_stack, v, t, c);
    s->r = VT_CONST | VT_SYM;
    /* don't record anonymous symbol */
    if (v < SYM_FIRST_ANOM) {
        ps = &table_ident[v - TOK_IDENT]->sym_identifier;
        /* modify the top most local identifier, so that `sym_identifier` will
           point to 's' when popped; happens when called from inline asm */
        while (*ps != NULL && (*ps)->sym_scope)
            ps = &(*ps)->prev_tok;
        s->prev_tok = *ps;
        *ps = s;
    }
    return s;
}

/* pop symbols until top reaches 'b'.  `If` `KEEP` is non-zero don't really
   pop them yet from the list, but do remove them from the token array.  */
ST_FUNC void sym_pop(Sym **ptop, Sym *b, int keep)
{
    Sym *s, *ss;
    int v;

    s = *ptop;
    while(s != b) {
        ss = s->prev;
        v = s->v;
        /* remove symbol in token array */
        if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
            sym_link(s, 0);
	if (!keep) {
            if (s->sym_scope > 0 && s->v < SYM_FIRST_ANOM
                && (s->type.t & VT_BTYPE) != VT_FUNC
                && !IS_ENUM_VAL(s->type.t)
                && !(s->type.t & (VT_STORAGE_STATIC | VT_STORAGE_EXTERN))
                && !s->a.used) {
                const char *name = get_tok_str(s->v, NULL);
                if (name[0] != '_') {
                    if (s->type.t & VT_STORAGE_TYPEDEF) {
                        tcc_warning_c(warn_unused_alias)("alias '%s' declared but not used", name);
                    } else if (s->a.is_param) {
                        tcc_warning_c(warn_unused_parameter)("parameter '%s' declared but not used (can be avoided by prefixing with '_')", name);
                    } else {
                        tcc_warning_c(warn_unused_variable)("variable '%s' declared but not used", name);
                    }
                }
            }
	    sym_free(s);
        }
        s = ss;
    }
    if (!keep)
	*ptop = b;
}

/* `Remove` the single symbol 's' from the symbol stack 'ptop', keeping the
   symbols that were pushed after it.  `$foreach` uses it to drop its loop
   variable without discarding the declarations of the replayed body.  */
static void sym_unlink(Sym **ptop, Sym *s)
{
    Sym **ps = ptop;

    while (*ps && *ps != s)
        ps = &(*ps)->prev;
    if (!*ps)
        return;
    *ps = s->prev;
    if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
        sym_link(s, 0);
    sym_free(s);
}

/* label lookup */
ST_FUNC Sym *label_find(int v)
{
    v -= TOK_IDENT;
    if ((unsigned)v >= (unsigned)(tok_ident - TOK_IDENT))
        return NULL;
    return table_ident[v]->sym_label;
}

ST_FUNC Sym *label_push(Sym **ptop, int v, int flags)
{
    Sym *s, **ps;
    s = sym_push2(ptop, v, VT_STORAGE_STATIC, 0);
    s->r = flags;
    ps = &table_ident[v - TOK_IDENT]->sym_label;
    if (ptop == &global_label_stack) {
        /* modify the top most local identifier, so that
           `sym_identifier` will point to 's' when popped */
        while (*ps != NULL)
            ps = &(*ps)->prev_tok;
    }
    s->prev_tok = *ps;
    *ps = s;
    return s;
}

/* pop labels until element last is reached. `Look` if any labels are
   undefined. `Define` symbols if '&&label' was used. */
ST_FUNC void label_pop(Sym **ptop, Sym *slast, int keep)
{
    Sym *s, *s1;
    for(s = *ptop; s != slast; s = s1) {
        s1 = s->prev;
        if (s->r == LABEL_DECLARED) {
            tcc_warning_c(warn_all)("label '%s' declared but not used", get_tok_str(s->v, NULL));
        } else if (s->r == LABEL_FORWARD) {
                tcc_error("label '%s' used but not defined",
                      get_tok_str(s->v, NULL));
        } else {
            if (s->c) {
                /* define corresponding symbol. A size of
                   1 is put. */
                put_extern_sym(s, cur_text_section, s->jind, 1);
            }
        }
        /* remove label */
        if (s->r != LABEL_GONE)
            table_ident[s->v - TOK_IDENT]->sym_label = s->prev_tok;
        if (!keep)
            sym_free(s);
        else
            s->r = LABEL_GONE;
    }
    if (!keep)
        *ptop = slast;
}

/* ------------------------------------------------------------------------- */
static void vcheck_cmp(void)
{
    /* cannot let cpu flags if other instruction are generated. `Also`
       avoid leaving `VT_JMP` anywhere except on the top of the stack
       because it would complicate the code generator.

       `Don`'t do this when `nocode_wanted`.  vtop might come from
       !`nocode_wanted` regions (see 88_codeopt.c) and transforming
       it to a register without actually generating code is wrong
       as their value might still be used for real.  `All` values
       we push under `nocode_wanted` will eventually be popped
       again, so that the `VT_CMP`/`VT_JMP` value will be in vtop
       when code is unsuppressed again. */

    /* `However` if it's just automatic suppression via `CODE_OFF`/`ON`()
       then it seems that we better let things work undisturbed.
       `How` can it work at all under `nocode_wanted`?  `Well`, gv() will
       actually clear it at the gsym() in load()/`VT_JMP` in the
       generator backends */

    if (vtop->r == VT_CMP && 0 == (nocode_wanted & ~CODE_OFF_BIT))
        gv(RC_INT);
}

/**
 * @brief Push a value descriptor onto the expression value stack.
 */
static void vsetc(CType *type, int r, CValue *vc)
{
    if (vtop >= vstack + (VSTACK_SIZE - 1))
        tcc_error("memory full (vstack)");
    vcheck_cmp();
    vtop++;
    vtop->type = *type;
    vtop->r = r;
    vtop->r2 = VT_CONST;
    vtop->c = *vc;
    vtop->sym = NULL;
    dump_vstack("push");
}

/**
 * @brief Swap the top two value stack entries.
 */
ST_FUNC void vswap(void)
{
    SValue tmp;

    vcheck_cmp();
    tmp = vtop[0];
    vtop[0] = vtop[-1];
    vtop[-1] = tmp;
    dump_vstack("swap");
}

/**
 * @brief Pop the top value stack entry, restoring CPU flags or jump state if needed.
 */
ST_FUNC void vpop(void)
{
    int v;
    v = vtop->r & VT_VALMASK;
#if defined(TCC_TARGET_I386) || defined(TCC_TARGET_X86_64)
    /* for x86, we need to pop the `FP` stack */
    if (v == TREG_ST0) {
        o(0xd8dd); /* fstp %st(0) */
    } else
#endif
    if (v == VT_CMP) {
        /* need to put correct jump if && or || without test */
        gsym(vtop->jtrue);
        gsym(vtop->jfalse);
    }
    vtop--;
    dump_vstack("pop");
}

/* push constant of type "type" with useless value */
/**
 * @brief Push a type descriptor with an undefined constant value.
 */
static void vpush(CType *type)
{
    vset(type, VT_CONST, 0);
}

/* push arbitrary 64bit constant */
/**
 * @brief Push a 64-bit integer constant onto the value stack.
 */
static void vpush64(int ty, unsigned long long v)
{
    CValue cval;
    CType ctype;
    ctype.t = ty;
    ctype.ref = NULL;
    cval.i = v;
    vsetc(&ctype, VT_CONST, &cval);
}

/* push integer constant */
/**
 * @brief Push an integer constant onto the value stack.
 */
ST_FUNC void vpushi(int v)
{
    vpush64(VT_INT, v);
}

/* push a pointer sized constant */
/**
 * @brief Push a pointer-sized constant onto the value stack.
 */
static void vpushs(addr_t v)
{
    vpush64(VT_SIZE_T, v);
}

/* push long long constant */
/**
 * @brief Push a 64-bit `long long` constant onto the value stack.
 */
static inline void vpushll(long long v)
{
    vpush64(VT_LLONG, v);
}

/**
 * @brief Push an integer value onto the value stack with an explicit type.
 */
ST_FUNC void vset(CType *type, int r, int v)
{
    CValue cval;
    cval.i = v;
    vsetc(type, r, &cval);
}

/**
 * @brief Push an integer value onto the value stack with an `int` type.
 */
static void vseti(int r, int v)
{
    CType type;
    type.t = VT_INT;
    type.ref = NULL;
    vset(&type, r, v);
}

/**
 * @brief Push a value descriptor onto the value stack.
 */
ST_FUNC void vpushv(SValue *v)
{
    if (vtop >= vstack + (VSTACK_SIZE - 1))
        tcc_error("memory full (vstack)");
    vtop++;
    *vtop = *v;
    dump_vstack("pushv");
}

/**
 * @brief Push a copy of an existing value stack entry.
 */
static void vdup(void)
{
    vpushv(vtop);
}

/* rotate the stack element at position n-1 to the top */
/**
 * @brief Rotate the stack element at position n-1 to the top.
 */
ST_FUNC void vrotb(int n)
{
    SValue tmp;
    if (--n < 1)
        return;
    vcheck_cmp();
    tmp = vtop[-n];
    memmove(vtop - n, vtop - n + 1, sizeof *vtop * n);
    vtop[0] = tmp;
}

/* rotate the top stack element into position n-1 */
/**
 * @brief Rotate the top stack element into position n-1.
 */
ST_FUNC void vrott(int n)
{
    SValue tmp;
    if (--n < 1)
        return;
    vcheck_cmp();
    tmp = vtop[0];
    memmove(vtop - n + 1, vtop - n, sizeof *vtop * n);
    vtop[-n] = tmp;
    dump_vstack("rott");
}

/* reverse order of the the first n stack elements */
/**
 * @brief Reverse the order of the first n stack elements.
 */
ST_FUNC void vrev(int n)
{
    int i;
    SValue tmp;
    vcheck_cmp();
    for (i = 0, n = -n; i > ++n; --i)
        tmp = vtop[i], vtop[i] = vtop[n], vtop[n] = tmp;
    dump_vstack("rev");
}

/* ------------------------------------------------------------------------- */
/* vtop->r = `VT_CMP` means `CPU`-flags have been set from comparison or test. */

/* called from generators to set the result from relational ops  */
/**
 * @brief Set the value stack top to hold a pending comparison result (`VT_CMP`).
 */
ST_FUNC void vset_VT_CMP(int op)
{
    vtop->r = VT_CMP;
    vtop->cmp_op = op;
    vtop->jfalse = 0;
    vtop->jtrue = 0;
}

/* called once before asking generators to load `VT_CMP` to a register */
/**
 * @brief Materialize the pending comparison (`VT_CMP`) into a jump decision.
 */
static void vset_VT_JMP(void)
{
    int op = vtop->cmp_op;

    if (vtop->jtrue || vtop->jfalse) {
        int origt = vtop->type.t;
        /* we need to jump to 'mov $0,%R' or 'mov $1,%R' */
        int inv = op & (op < 2); /* small optimization */
        vseti(VT_JMP+inv, gvtst(inv, 0));
        vtop->type.t |= origt & (VT_UNSIGNED | VT_DEFSIGN);
    } else {
        /* otherwise convert flags (rsp. 0/1) to register */
        vtop->c.i = op;
        if (op < 2) /* doesn't seem to happen */
            vtop->r = VT_CONST;
    }
}

/* `Set` `CPU` `Flags`, doesn't yet jump */
static void gvtst_set(int inv, int t)
{
    int *p;

    if (vtop->r != VT_CMP) {
        vpushi(0);
        gen_op(TOK_NE);
        if (vtop->r != VT_CMP) /* must be `VT_CONST` then */
            vset_VT_CMP(vtop->c.i != 0);
    }

    p = inv ? &vtop->jfalse : &vtop->jtrue;
    *p = gjmp_append(*p, t);
}

/* `Generate` value test
 *
 * `Generate` a test for any value (jump, comparison and integers) */
static int gvtst(int inv, int t)
{
    int op, x, u;

    gvtst_set(inv, t);
    t = vtop->jtrue, u = vtop->jfalse;
    if (inv)
        x = u, u = t, t = x;
    op = vtop->cmp_op;

    /* jump to the wanted target */
    if (op > 1)
        t = gjmp_cond(op ^ inv, t);
    else if (op != inv)
        t = gjmp(t);
    /* resolve complementary jumps to here */
    gsym(u);

    vtop--;
    return t;
}

/* generate a zero or nozero test */
static void gen_test_zero(int op)
{
    if (vtop->r == VT_CMP) {
        int j;
        if (op == TOK_EQ) {
            j = vtop->jfalse;
            vtop->jfalse = vtop->jtrue;
            vtop->jtrue = j;
            vtop->cmp_op ^= 1;
        }
    } else {
        vpushi(0);
        gen_op(op);
    }
}

/* ------------------------------------------------------------------------- */
/* push a symbol value of `TYPE` */
ST_FUNC void vpushsym(CType *type, Sym *sym)
{
    CValue cval;
    cval.i = 0;
    if (sym)
        sym->a.used = 1;
    if (sym && sym->v < SYM_FIRST_ANOM
        && (sym->sym_scope == 0 || (sym->type.t & (VT_STORAGE_STATIC | VT_STORAGE_EXTERN | VT_STORAGE_TLS)))
        && (sym->type.t & VT_BTYPE) != VT_FUNC && !IS_ENUM_VAL(sym->type.t) && !(sym->type.t & VT_STORAGE_TYPEDEF)) {
        if (IS_PURE(cur_scope->fn.pure)) {
            tcc_error("global variable '%s' not allowed in @pure scope", get_tok_str(sym->v, NULL));
        } else if (cur_scope->fn.pure == FUNC_PURITY_UNKNOWN) {
            cur_scope->fn.pure = FUNC_PURITY_INFERRED_FALSE;
        }
    }
    vsetc(type, VT_CONST | VT_SYM, &cval);
    vtop->sym = sym;
}

/* `Return` a static symbol pointing to a section */
ST_FUNC Sym *get_sym_ref(CType *type, Section *sec, unsigned long offset, unsigned long size)
{
    int v;
    Sym *sym;

    v = anon_sym++;
    sym = sym_push(v, type, VT_CONST | VT_SYM, 0);
    sym->type.t |= VT_STORAGE_STATIC;
    put_extern_sym(sym, sec, offset, size);
    return sym;
}

/* push a reference to a section offset by adding a dummy symbol */
static void vpush_ref(CType *type, Section *sec, unsigned long offset, unsigned long size)
{
    vpushsym(type, get_sym_ref(type, sec, offset, size));
}

/* define a new external reference to a symbol 'v' of type 'u' */
ST_FUNC Sym *external_global_sym(int v, CType *type)
{
    Sym *s;

    s = sym_find(v);
    if (!s) {
        /* push forward reference */
        s = global_identifier_push(v, type->t | VT_STORAGE_EXTERN, 0);
        s->type.ref = type->ref;
    } else if (IS_ASM_SYM(s)) {
        s->type.t = type->t | (s->type.t & VT_STORAGE_EXTERN);
        s->type.ref = type->ref;
        update_storage(s);
    }
    return s;
}

/* create an external reference with no specific type similar to asm labels.
   `This` avoids type conflicts if the symbol is used from C too */
ST_FUNC Sym *external_helper_sym(int v)
{
    CType ct = { { VT_ASM_FUNC }, NULL };
    return external_global_sym(v, &ct);
}

/* push a reference to an helper function (such as memmove) */
ST_FUNC void vpush_helper_func(int v)
{
    vpushsym(&func_old_type, external_helper_sym(v));
}

/* `Merge` symbol attributes.  */
static void merge_symattr(struct SymAttr *sa, struct SymAttr *sa1)
{
    if (sa1->aligned && !sa->aligned)
      sa->aligned = sa1->aligned;
    sa->packed |= sa1->packed;
    sa->weak |= sa1->weak;
    sa->nodebug |= sa1->nodebug;
    if (sa1->visibility != STV_DEFAULT) {
	int vis = sa->visibility;
	if (vis == STV_DEFAULT
	    || vis > sa1->visibility)
	  vis = sa1->visibility;
	sa->visibility = vis;
    }
    sa->dllexport |= sa1->dllexport;
    sa->nodecorate |= sa1->nodecorate;
    sa->dllimport |= sa1->dllimport;
}

/* `Merge` function attributes.  */
static void merge_funcattr(struct FuncAttr *fa, struct FuncAttr *fa1)
{
    if (fa1->func_call && !fa->func_call)
      fa->func_call = fa1->func_call;
    if (fa1->func_type && !fa->func_type)
      fa->func_type = fa1->func_type;
    if (fa1->func_args && !fa->func_args)
      fa->func_args = fa1->func_args;
    if (fa1->func_noreturn)
      fa->func_noreturn = 1;
    if (fa1->func_ctor)
      fa->func_ctor = 1;
    if (fa1->func_dtor)
      fa->func_dtor = 1;
    if (fa1->func_pure)
      fa->func_pure = fa1->func_pure;
    if (fa1->func_safety)
      fa->func_safety = fa1->func_safety;
}

/* `Merge` attributes.  */
static void merge_attr(AttrDef *ad, AttrDef *ad1)
{
    merge_symattr(&ad->a, &ad1->a);
    merge_funcattr(&ad->f, &ad1->f);

    if (ad1->section)
      ad->section = ad1->section;
    if (ad1->alias_target)
      ad->alias_target = ad1->alias_target;
    if (ad1->asm_label)
      ad->asm_label = ad1->asm_label;
    if (ad1->attr_mode)
      ad->attr_mode = ad1->attr_mode;
}

/* `Merge` some type attributes.  */
static void patch_type(Sym *sym, CType *type)
{
    if (!(type->t & VT_STORAGE_EXTERN) || IS_ENUM_VAL(sym->type.t)) {
        if (!(sym->type.t & VT_STORAGE_EXTERN))
            tcc_error("redefinition of '%s'", get_tok_str(sym->v, NULL));
        sym->type.t &= ~VT_STORAGE_EXTERN;
    }

    if (IS_ASM_SYM(sym)) {
        /* stay static if both are static */
        sym->type.t = type->t & (sym->type.t | ~VT_STORAGE_STATIC);
        sym->type.ref = type->ref;
        if ((type->t & VT_BTYPE) != VT_FUNC && !(type->t & VT_ARRAY))
            sym->r |= VT_LVAL;
    }

    if (!is_compatible_types(&sym->type, type)) {
        tcc_error("incompatible types for redefinition of '%s'",
                  get_tok_str(sym->v, NULL));

    } else if ((sym->type.t & VT_BTYPE) == VT_FUNC) {
        int static_proto = sym->type.t & VT_STORAGE_STATIC;
        int ft1 = sym->type.ref->f.func_type;
        int ft2 = type->ref->f.func_type;

        /* warn if static follows non-static function declaration */
        if ((type->t & VT_STORAGE_STATIC) && !static_proto
            /* `XXX` this test for inline shouldn't be here.  `Until` we
               implement gnu-inline mode again it silences a warning for
               mingw caused by our workarounds.  */
            && !((type->t | sym->type.t) & VT_STORAGE_INLINE))
            tcc_warning("static storage ignored for redefinition of '%s'",
                get_tok_str(sym->v, NULL));

        /* set 'inline' if both agree or if one has static */
        if ((type->t | sym->type.t) & VT_STORAGE_INLINE) {
            if (!((type->t ^ sym->type.t) & VT_STORAGE_INLINE)
             || ((type->t | sym->type.t) & VT_STORAGE_STATIC))
                static_proto |= VT_STORAGE_INLINE;
        }

        if (0 == (type->t & VT_STORAGE_EXTERN)) {
            struct FuncAttr f = sym->type.ref->f;
            /* put complete type, use static from prototype */
            sym->type.t = (type->t & ~(VT_STORAGE_STATIC|VT_STORAGE_INLINE)) | static_proto;
            if (ft1 != FUNC_OLD)
                type->ref->f.func_type = ft1;
            sym->type.ref = type->ref;
            merge_funcattr(&sym->type.ref->f, &f);
        } else {
            sym->type.t &= ~VT_STORAGE_INLINE | static_proto;
            if (ft1 == FUNC_OLD && ft2 != FUNC_OLD)
                sym->type.ref = type->ref;
        }

    } else {
        if ((sym->type.t & VT_ARRAY) && type->ref->c >= 0) {
            /* set array size if it was omitted in extern declaration */
            sym->type.ref->c = type->ref->c;
        }
        if ((type->t ^ sym->type.t) & VT_STORAGE_STATIC)
            tcc_warning("storage mismatch for redefinition of '%s'",
                get_tok_str(sym->v, NULL));
    }
}

/* `Merge` some storage attributes.  */
static void patch_storage(Sym *sym, AttrDef *ad, CType *type)
{
    if (type)
        patch_type(sym, type);

#ifdef TCC_TARGET_PE
    if (sym->a.dllimport != ad->a.dllimport)
        tcc_error("incompatible dll linkage for redefinition of '%s'",
            get_tok_str(sym->v, NULL));
#endif
    merge_symattr(&sym->a, &ad->a);
    if (ad->asm_label)
        sym->asm_label = ad->asm_label;
    update_storage(sym);
}

/* copy sym to other stack */
static Sym *sym_copy(Sym *s0, Sym **ps)
{
    Sym *s;
    s = sym_malloc(), *s = *s0;
    s->prev = *ps, *ps = s;
    if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
        sym_link(s, 1);
    return s;
}

/* `Symbol` 's' was locally declared 'extern' (or as function), and is
   on `global_stack`.  `Now` must copy its 'ref' to `global_stack` too */
static void move_ref_to_global(Sym *s)
{
    Sym *l, **lp;
    int n, bt;

    bt = s->type.t & VT_BTYPE;
    if (!(bt == VT_PTR
       || bt == VT_FUNC
       || bt == VT_STRUCT
       || IS_ENUM(s->type.t)))
        return;

    for (s = s->type.ref, n = 0; s; s = s->next) {
        for (lp = &local_stack; !!(l = *lp); lp = &l->prev) {
            if (l == s) {
                *lp = s->prev;
                s->prev = global_stack, global_stack = s;
                if (n || bt == VT_PTR || bt == VT_FUNC) {
                    move_ref_to_global(s);
                } else {
                    if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
                        /* copy struct/enum tag to local scope */
                        s->v |= SYM_FIELD;
                        l = sym_copy(s, lp);
                        l->v &= ~SYM_FIELD;
                    }
                }
                if (bt != VT_PTR)
                    n = 1;
                break;
            }
        }
        if (n == 0) /* no next (`VT_PTR`) or ref not on `local_stack` */
            break;
    }
}

/* define a new external reference to a symbol 'v' */
static Sym *external_sym(int v, CType *type, int r, AttrDef *ad)
{
    Sym *s;

    /* look for global symbol */
    s = sym_find(v);
    while (s && s->sym_scope)
        s = s->prev_tok;

    if (!s) {
        /* push forward reference */
        s = global_identifier_push(v, type->t, 0);
        s->r |= r;
        s->a = ad->a;
        s->asm_label = ad->asm_label;
        s->type.ref = type->ref;
    } else {
        patch_storage(s, ad, type);
    }
    if (local_stack) {
        /* make sure that type->ref is on global stack */
        move_ref_to_global(s);
        /* put into local scope */
        sym_copy(s, &local_stack);
    }
    return s;
}

/* save registers up to (vtop - n) stack entry */
ST_FUNC void save_regs(int n)
{
    SValue *p, *p1;
    for(p = vstack, p1 = vtop - n; p <= p1; p++)
        save_reg(p->r);
}

/* save r to the memory stack, and mark it as being free */
ST_FUNC void save_reg(int r)
{
    save_reg_upstack(r, 0);
}

/* save r to the memory stack, and mark it as being free,
   if seen up to (vtop - n) stack entry */
ST_FUNC void save_reg_upstack(int r, int n)
{
    int l, size, align, bt, r2;
    SValue *p, *p1, sv;

    if ((r &= VT_VALMASK) >= VT_CONST)
        return;
    if (nocode_wanted)
        return;
    l = r2 = 0;
    for(p = vstack, p1 = vtop - n; p <= p1; p++) {
        if ((p->r & VT_VALMASK) == r || p->r2 == r) {
            /* must save value on stack if not already done */
            if (!l) {
                bt = p->type.t & VT_BTYPE;
                if (bt == VT_VOID)
                    continue;
                if ((p->r & VT_LVAL) || bt == VT_FUNC)
                    bt = VT_PTR;
                sv.type.t = bt;
                size = type_size(&sv.type, &align);
                l = get_temp_local_var(size, align, &r2);
                sv.r = VT_LOCAL | VT_LVAL;
                sv.c.i = l;
		sv.sym = NULL;
                store(p->r & VT_VALMASK, &sv);
#if defined(TCC_TARGET_I386) || defined(TCC_TARGET_X86_64)
                /* x86 specific: need to pop fp register `ST0` if saved */
                if (r == TREG_ST0) {
                    o(0xd8dd); /* fstp %st(0) */
                }
#endif
                /* special long long case */
                if (p->r2 < VT_CONST && USING_TWO_WORDS(bt)) {
                    sv.c.i += PTR_SIZE;
                    store(p->r2, &sv);
                }
            }
            /* mark that stack entry as being saved on the stack */
            if (p->r & VT_LVAL) {
                /* also clear the bounded flag because the
                   relocation address of the function was stored in
                   p->c.i */
                p->r = (p->r & ~(VT_VALMASK | VT_BOUNDED)) | VT_LLOCAL;
            } else {
                p->r = VT_LVAL | VT_LOCAL;
                p->type.t &= ~VT_ARRAY; /* cannot combine `VT_LVAL` with `VT_ARRAY` */
            }
            p->sym = NULL;
            p->r2 = r2;
            p->c.i = l;
        }
    }
}

#ifdef TCC_TARGET_ARM
/* find a register of class 'rc2' with at most one reference on stack.
 * `If` none, call `get_reg`(rc) */
ST_FUNC int get_reg_ex(int rc, int rc2)
{
    int r;
    SValue *p;

    for(r=0;r<NB_REGS;r++) {
        if (reg_classes[r] & rc2) {
            int n;
            n=0;
            for(p = vstack; p <= vtop; p++) {
                if ((p->r & VT_VALMASK) == r ||
                    p->r2 == r)
                    n++;
            }
            if (n <= 1)
                return r;
        }
    }
    return get_reg(rc);
}
#endif

/* find a free register of class 'rc'. `If` none, save one register */
ST_FUNC int get_reg(int rc)
{
    int r;
    SValue *p;

    /* find a free register */
    for(r=0;r<NB_REGS;r++) {
        if (reg_classes[r] & rc) {
            if (nocode_wanted)
                return r;
            for(p=vstack;p<=vtop;p++) {
                if ((p->r & VT_VALMASK) == r ||
                    p->r2 == r)
                    goto notfound;
            }
            return r;
        }
    notfound: ;
    }

    /* no register left : free the first one on the stack (`VERY`
       `IMPORTANT` to start from the bottom to ensure that we don't
       spill registers used in `gen_opi`()) */
    for(p=vstack;p<=vtop;p++) {
        /* look at second register (if long long) */
        r = p->r2;
        if (r < VT_CONST && (reg_classes[r] & rc))
            goto save_found;
        r = p->r & VT_VALMASK;
        if (r < VT_CONST && (reg_classes[r] & rc)) {
        save_found:
            save_reg(r);
            return r;
        }
    }
    /* `Should` never comes here */
    return -1;
}

/* find a free temporary local variable (return the offset on stack) match
   size and align. `If` none, add new temporary stack variable */
static int get_temp_local_var(int size,int align, int *r2)
{
    int i;
    struct temp_local_variable *temp_var;
    SValue *p;
    int r;
    unsigned used = 0;

    /* mark locations that are still in use */
    for (p = vstack; p <= vtop; p++) {
	r = p->r & VT_VALMASK;
	if (r == VT_LOCAL || r == VT_LLOCAL) {
	    r = p->r2 - (VT_CONST + 1);
	    if (r >= 0 && r < MAX_TEMP_LOCAL_VARIABLE_NUMBER)
	        used |= 1<<r;
	}
    }
    for (i=0;i<nb_temp_local_vars;i++) {
	temp_var=&arr_temp_local_vars[i];
	if(!(used & 1<<i)
	 && temp_var->size>=size
	 && temp_var->align>=align) {
ret_tmp:
	    *r2 = (VT_CONST + 1) + i;
	    return temp_var->location;
	}
    }
    loc = (loc - size) & -align;
    if (nb_temp_local_vars<MAX_TEMP_LOCAL_VARIABLE_NUMBER) {
	temp_var=&arr_temp_local_vars[i];
	temp_var->location=loc;
	temp_var->size=size;
	temp_var->align=align;
	nb_temp_local_vars++;
	goto ret_tmp;
    }
    *r2 = VT_CONST;
    return loc;
}

/* move register 's' (of type 't') to 'r', and flush previous value of r to memory
   if needed */
static void move_reg(int r, int s, int t)
{
    SValue sv;

    if (r != s) {
        save_reg(r);
        sv.type.t = t;
        sv.type.ref = NULL;
        sv.r = s;
        sv.c.i = 0;
        load(r, &sv);
    }
}

/* get address of vtop (vtop `MUST` `BE` an lvalue) */
ST_FUNC void gaddrof(void)
{
    vtop->r &= ~VT_LVAL;
    /* tricky: if saved lvalue, then we can go back to lvalue */
    if ((vtop->r & VT_VALMASK) == VT_LLOCAL)
        vtop->r = (vtop->r & ~VT_VALMASK) | VT_LOCAL | VT_LVAL;
}

#ifdef CONFIG_TCC_BCHECK
/* generate a bounded pointer addition */
static void gen_bounded_ptr_add(void)
{
    int save = (vtop[-1].r & VT_VALMASK) == VT_LOCAL;
    if (save) {
      vpushv(&vtop[-1]);
      vrott(3);
    }
    vpush_helper_func(TOK___bound_ptr_add);
    vrott(3);
    gfunc_call(2);
    vtop -= save;
    vpushi(0);
    /* returned pointer is in `REG_IRET` */
    vtop->r = REG_IRET | VT_BOUNDED;
    if (nocode_wanted)
        return;
    /* relocation offset of the bounding function call point */
    vtop->c.i = (cur_text_section->reloc->data_offset - sizeof(ElfW_Rel));
}

/* patch pointer addition in vtop so that pointer dereferencing is
   also tested */
static void gen_bounded_ptr_deref(void)
{
    addr_t func;
    int size, align;
    ElfW_Rel *rel;
    Sym *sym;

    if (nocode_wanted)
        return;

    size = type_size(&vtop->type, &align);
    switch(size) {
    case  1: func = TOK___bound_ptr_indir1; break;
    case  2: func = TOK___bound_ptr_indir2; break;
    case  4: func = TOK___bound_ptr_indir4; break;
    case  8: func = TOK___bound_ptr_indir8; break;
    case 12: func = TOK___bound_ptr_indir12; break;
    case 16: func = TOK___bound_ptr_indir16; break;
    default:
        /* may happen with struct member access */
        return;
    }
    sym = external_helper_sym(func);
    if (!sym->c)
        put_extern_sym(sym, NULL, 0, 0);
    /* patch relocation */
    /* `XXX`: find a better solution ? */
    rel = (ElfW_Rel *)(cur_text_section->reloc->data + vtop->c.i);
    rel->r_info = ELFW(R_INFO)(sym->c, ELFW(R_TYPE)(rel->r_info));
}

/* generate lvalue bound code */
static void gbound(void)
{
    CType type1;

    vtop->r &= ~VT_MUSTBOUND;
    /* if lvalue, then use checking code before dereferencing */
    if (vtop->r & VT_LVAL) {
        /* if not `VT_BOUNDED` value, then make one */
        if (!(vtop->r & VT_BOUNDED)) {
            /* must save type because we must set it to int to get pointer */
            type1 = vtop->type;
            vtop->type.t = VT_PTR;
            gaddrof();
            vpushi(0);
            gen_bounded_ptr_add();
            vtop->r |= VT_LVAL;
            vtop->type = type1;
        }
        /* then check for dereferencing */
        gen_bounded_ptr_deref();
    }
}

/* we need to call `__bound_ptr_add` before we start to load function
   args into registers */
ST_FUNC void gbound_args(int nb_args)
{
    int i, v;
    SValue *sv;

    for (i = 1; i <= nb_args; ++i)
        if (vtop[1 - i].r & VT_MUSTBOUND) {
            vrotb(i);
            gbound();
            vrott(i);
        }

    sv = vtop - nb_args;
    if (sv->r & VT_SYM) {
        v = sv->sym->v;
        if (v == TOK_setjmp
          || v == TOK__setjmp
#ifndef TCC_TARGET_PE
          || v == TOK_sigsetjmp
          || v == TOK___sigsetjmp
#endif
          ) {
            vpush_helper_func(TOK___bound_setjmp);
            vpushv(sv + 1);
            gfunc_call(1);
            func_bound_add_epilog = 1;
        }
        if (v == TOK_alloca)
            func_bound_add_epilog = 1;
#if TARGETOS_NetBSD
        if (v == TOK_longjmp) /* undo rename to `__longjmp14` */
            sv->sym->asm_label = TOK___bound_longjmp;
#endif
    }
}

/* `Register` a bound-checked region for a local at 'addr' of 'size' bytes.  */
static void add_local_bound(int addr, int size)
{
    addr_t *bounds_ptr = section_ptr_add(lbounds_section,
                                         2 * sizeof(addr_t));
    bounds_ptr[0] = addr;
    bounds_ptr[1] = size;
}

/* `Add` bounds for local symbols from S to E (via ->prev) */
static void add_local_bounds(Sym *s, Sym *e)
{
    for (; s != e; s = s->prev) {
        if (!s->v || (s->r & VT_VALMASK) != VT_LOCAL)
          continue;
        /* `Add` arrays/structs/unions because we always take address */
        if ((s->type.t & VT_ARRAY)
            || (s->type.t & VT_BTYPE) == VT_STRUCT
            || s->a.addrtaken) {
            /* add local bound info */
            int align, size = type_size(&s->type, &align);
            add_local_bound(s->c, size);
        }
    }
}
#endif

/* add debug info for locals or function parameters, optionally
   register bounds */
static void tcc_debug_end_scope(Sym *b, int bounds)
{
#ifdef CONFIG_TCC_BCHECK
    if (tcc_state->do_bounds_check && bounds)
        add_local_bounds(local_stack, b);
#endif
    tcc_add_debug_info (tcc_state, local_stack, b);
}

/* increment an lvalue pointer */
static void incr_offset(int offset)
{
    int t = vtop->type.t;
    gaddrof(); /* remove `VT_LVAL` */
    vtop->type.t = VT_PTRDIFF_T; /* set scalar type */
    vpushs(offset);
    gen_op('+');
    vtop->r |= VT_LVAL;
    vtop->type.t = t;
}

static void incr_bf_adr(int o)
{
    vtop->type.t = VT_BYTE | VT_UNSIGNED;
    incr_offset(o);
}

/* single-byte load mode for packed or otherwise unaligned bitfields */
static void load_packed_bf(CType *type, int bit_pos, int bit_size)
{
    int n, o, bits;
    save_reg_upstack(vtop->r, 1);
    vpush64(type->t & VT_BTYPE, 0); // B X
    bits = 0, o = bit_pos >> 3, bit_pos &= 7;
    do {
        vswap(); // X B
        incr_bf_adr(o);
        vdup(); // X B B
        n = 8 - bit_pos;
        if (n > bit_size)
            n = bit_size;
        if (bit_pos)
            vpushi(bit_pos), gen_op(TOK_SHR), bit_pos = 0; // X B Y
        if (n < 8)
            vpushi((1 << n) - 1), gen_op('&');
        gen_cast(type);
        if (bits)
            vpushi(bits), gen_op(TOK_SHL);
        vrotb(3); // B Y X
        gen_op('|'); // B X
        bits += n, bit_size -= n, o = 1;
    } while (bit_size);
    vswap(), vpop();
    if (!(type->t & VT_UNSIGNED)) {
        n = ((type->t & VT_BTYPE) == VT_LLONG ? 64 : 32) - bits;
        vpushi(n), gen_op(TOK_SHL);
        vpushi(n), gen_op(TOK_SAR);
    }
}

/* single-byte store mode for packed or otherwise unaligned bitfields */
static void store_packed_bf(int bit_pos, int bit_size)
{
    int bits, n, o, m, c;
    c = (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    vswap(); // X B
    save_reg_upstack(vtop->r, 1);
    bits = 0, o = bit_pos >> 3, bit_pos &= 7;
    do {
        incr_bf_adr(o); // X B
        vswap(); //B X
        c ? vdup() : gv_dup(); // B V X
        vrott(3); // X B V
        if (bits)
            vpushi(bits), gen_op(TOK_SHR);
        if (bit_pos)
            vpushi(bit_pos), gen_op(TOK_SHL);
        n = 8 - bit_pos;
        if (n > bit_size)
            n = bit_size;
        if (n < 8) {
            m = ((1 << n) - 1) << bit_pos;
            vpushi(m), gen_op('&'); // X B V1
            vpushv(vtop-1); // X B V1 B
            vpushi(m & 0x80 ? ~m & 0x7f : ~m);
            gen_op('&'); // X B V1 B1
            gen_op('|'); // X B V2
        }
        vdup(), vtop[-1] = vtop[-2]; // X B B V2
        vstore(), vpop(); // X B
        bits += n, bit_size -= n, bit_pos = 0, o = 1;
    } while (bit_size);
    vpop(), vpop();
}

static int adjust_bf(SValue *sv, int bit_pos, int bit_size)
{
    int t;
    if (0 == sv->type.ref)
        return 0;
    t = sv->type.ref->auxtype;
    if (t != -1 && t != VT_STRUCT) {
        sv->type.t = (sv->type.t & ~(VT_BTYPE | VT_LONG)) | t;
        sv->r |= VT_LVAL;
    }
    return t;
}

/* store vtop a register belonging to class 'rc'. lvalues are
   converted to values. `Cannot` be used if cannot be converted to
   register value (such as structures). */
ST_FUNC int gv(int rc)
{
    int r, r2, r_ok, r2_ok, rc2, bt;
    int bit_pos, bit_size, size, align;

    /* `NOTE`: `get_reg` can modify vstack[] */
    if (vtop->type.t & VT_BITFIELD) {
        CType type;

        bit_pos = BIT_POS(vtop->type.t);
        bit_size = BIT_SIZE(vtop->type.t);
        /* remove bit field info to avoid loops */
        vtop->type.t &= ~VT_STRUCT_MASK;

        type.ref = NULL;
        type.t = vtop->type.t & VT_UNSIGNED;
        if ((vtop->type.t & VT_BTYPE) == VT_BOOL)
            type.t |= VT_UNSIGNED;

        r = adjust_bf(vtop, bit_pos, bit_size);

        if ((vtop->type.t & VT_BTYPE) == VT_LLONG)
            type.t |= VT_LLONG;
        else
            type.t |= VT_INT;

        if (r == VT_STRUCT) {
            load_packed_bf(&type, bit_pos, bit_size);
        } else {
            int bits = (type.t & VT_BTYPE) == VT_LLONG ? 64 : 32;
            /* cast to int to propagate signedness in following ops */
            gen_cast(&type);
            /* generate shifts */
            vpushi(bits - (bit_pos + bit_size));
            gen_op(TOK_SHL);
            vpushi(bits - bit_size);
            /* `NOTE`: transformed to `SHR` if unsigned */
            gen_op(TOK_SAR);
        }
        r = gv(rc);
    } else {
        if (is_float(vtop->type.t) &&
            (vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
            /* `CPUs` usually cannot use float constants, so we store them
               generically in data segment */
            init_params p = { rodata_section };
            unsigned long offset;
            size = type_size(&vtop->type, &align);
            if (NODATA_WANTED)
                size = 0, align = 1;
            offset = section_add(p.sec, size, align);
            vpush_ref(&vtop->type, p.sec, offset, size);
	    vswap();
	    init_putv(&p, &vtop->type, offset);
	    vtop->r |= VT_LVAL;
        }
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound();
#endif

        bt = vtop->type.t & VT_BTYPE;
        if (bt == VT_VOID || bt == VT_STRUCT) /* should not happen */
            return vtop->r;

#ifdef TCC_TARGET_RISCV64
        /* `XXX` mega hack */
        if (bt == VT_LDOUBLE && rc == RC_FLOAT)
          rc = RC_INT;
#endif
        rc2 = RC2_TYPE(bt, rc);

        /* need to reload if:
           - constant
           - lvalue (need to dereference pointer)
           - already a register, but not in the right class */
        r = vtop->r & VT_VALMASK;
        r_ok = !(vtop->r & VT_LVAL) && (r < VT_CONST) && (reg_classes[r] & rc);
        r2_ok = !rc2 || ((vtop->r2 < VT_CONST) && (reg_classes[vtop->r2] & rc2));

        if (!r_ok || !r2_ok) {

            if (!r_ok) {
                if (1 /* we can 'mov (r),r' in cases */
                    && r < VT_CONST
                    && (reg_classes[r] & rc)
                    && !rc2
                    )
                    save_reg_upstack(r, 1);
                else
                    r = get_reg(rc);
            }

            if (rc2) {
                int load_type = (bt == VT_QFLOAT) ? VT_DOUBLE : VT_PTRDIFF_T;
                int original_type = vtop->type.t;

                /* two register type load :
                   expand to two words temporarily */
                if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
                    /* load constant */
                    unsigned long long ll = vtop->c.i;
                    vtop->c.i = ll; /* first word */
                    load(r, vtop);
                    vtop->r = r; /* save register value */
                    vpushi(ll >> 32); /* second word */
                } else if (vtop->r & VT_LVAL) {
                    /* `We` do not want to modifier the long long pointer here.
                       `So` we save any other instances down the stack */
                    save_reg_upstack(vtop->r, 1);
                    /* load from memory */
                    vtop->type.t = load_type;
                    load(r, vtop);
                    vdup();
                    vtop[-1].r = r; /* save register value */
                    /* increment pointer to get second word */
                    incr_offset(PTR_SIZE);
                } else {
                    /* move registers */
                    if (!r_ok)
                        load(r, vtop);
                    if (r2_ok && vtop->r2 < VT_CONST)
                        goto done;
                    vdup();
                    vtop[-1].r = r; /* save register value */
                    vtop->r = vtop[-1].r2;
                }
                /* `Allocate` second register. `Here` we rely on the fact that
                   `get_reg`() tries first to free r2 of an `SValue`. */
                r2 = get_reg(rc2);
                load(r2, vtop);
                vpop();
                /* write second register */
                vtop->r2 = r2;
            done:
                vtop->type.t = original_type;
            } else {
                if (vtop->r == VT_CMP)
                    vset_VT_JMP();
                /* one register type load */
                load(r, vtop);
            }
        }
        vtop->r = r;
#ifdef TCC_TARGET_C67
        /* uses register pairs for doubles */
        if (bt == VT_DOUBLE)
            vtop->r2 = r+1;
#endif
    }
    return r;
}

/* generate vtop[-1] and vtop[0] in resp. classes rc1 and rc2 */
ST_FUNC void gv2(int rc1, int rc2)
{
    /* generate more generic register first. `But` `VT_JMP` or `VT_CMP`
       values must be generated first in all cases to avoid possible
       reload errors */
    if (vtop->r != VT_CMP && rc1 <= rc2) {
        vswap();
        gv(rc1);
        vswap();
        gv(rc2);
        /* test if reload is needed for first register */
        if ((vtop[-1].r & VT_VALMASK) >= VT_CONST) {
            vswap();
            gv(rc1);
            vswap();
        }
    } else {
        gv(rc2);
        vswap();
        gv(rc1);
        vswap();
        /* test if reload is needed for first register */
        if ((vtop[0].r & VT_VALMASK) >= VT_CONST) {
            gv(rc2);
        }
    }
}

#if PTR_SIZE == 4
/* expand 64bit on stack in two ints */
ST_FUNC void lexpand(void)
{
    int u, v;
    u = vtop->type.t & (VT_DEFSIGN | VT_UNSIGNED);
    v = vtop->r & (VT_VALMASK | VT_LVAL);
    if (v == VT_CONST) {
        vdup();
        vtop[0].c.i >>= 32;
    } else if (v == (VT_LVAL|VT_CONST) || v == (VT_LVAL|VT_LOCAL)) {
        vdup();
        vtop[0].c.i += 4;
    } else {
        gv(RC_INT);
        vdup();
        vtop[0].r = vtop[-1].r2;
        vtop[0].r2 = vtop[-1].r2 = VT_CONST;
    }
    vtop[0].type.t = vtop[-1].type.t = VT_INT | u;
}
#endif

#if PTR_SIZE == 4
/* build a long long from two ints */
static void lbuild(int t)
{
    gv2(RC_INT, RC_INT);
    vtop[-1].r2 = vtop[0].r;
    vtop[-1].type.t = t;
    vpop();
}
#endif

/* convert stack entry to register and duplicate its value in another
   register */
static void gv_dup(void)
{
    int t, rc, r;

    t = vtop->type.t;
#if PTR_SIZE == 4
    if ((t & VT_BTYPE) == VT_LLONG) {
        if (t & VT_BITFIELD) {
            gv(RC_INT);
            t = vtop->type.t;
        }
        lexpand();
        gv_dup();
        vswap();
        vrotb(3);
        gv_dup();
        vrotb(4);
        /* stack: H L `L1` `H1` */
        lbuild(t);
        vrotb(3);
        vrotb(3);
        vswap();
        lbuild(t);
        vswap();
        return;
    }
#endif
    /* duplicate value */
    rc = RC_TYPE(t);
    gv(rc);
    r = get_reg(rc);
    vdup();
    load(r, vtop);
    vtop->r = r;
}

#if PTR_SIZE == 4
/* generate `CPU` independent (unsigned) long long operations */
static void gen_opl(int op)
{
    int t, a, b, op1, c, i;
    int func;
    unsigned short reg_iret = REG_IRET;
    unsigned short reg_lret = REG_IRE2;
    SValue tmp;

    switch(op) {
    case '/':
    case TOK_PDIV:
        func = TOK___divdi3;
        goto gen_func;
    case TOK_UDIV:
        func = TOK___udivdi3;
        goto gen_func;
    case '%':
        func = TOK___moddi3;
        goto gen_mod_func;
    case TOK_UMOD:
        func = TOK___umoddi3;
    gen_mod_func:
#ifdef TCC_ARM_EABI
        reg_iret = TREG_R2;
        reg_lret = TREG_R3;
#endif
    gen_func:
        /* call generic long long function */
        vpush_helper_func(func);
        vrott(3);
        gfunc_call(2);
        vpushi(0);
        vtop->r = reg_iret;
        vtop->r2 = reg_lret;
        break;
    case '^':
    case '&':
    case '|':
    case '*':
    case '+':
    case '-':
        //pv("gen_opl A",0,2);
        t = vtop->type.t;
        vswap();
        lexpand();
        vrotb(3);
        lexpand();
        /* stack: `L1` `H1` `L2` `H2` */
        tmp = vtop[0];
        vtop[0] = vtop[-3];
        vtop[-3] = tmp;
        tmp = vtop[-2];
        vtop[-2] = vtop[-3];
        vtop[-3] = tmp;
        vswap();
        /* stack: `H1` `H2` `L1` `L2` */
        //pv("gen_opl B",0,4);
        if (op == '*') {
            vpushv(vtop - 1);
            vpushv(vtop - 1);
            gen_op(TOK_UMULL);
            lexpand();
            /* stack: `H1` `H2` `L1` `L2` `ML` `MH` */
            for(i=0;i<4;i++)
                vrotb(6);
            /* stack: `ML` `MH` `H1` `H2` `L1` `L2` */
            tmp = vtop[0];
            vtop[0] = vtop[-2];
            vtop[-2] = tmp;
            /* stack: `ML` `MH` `H1` `L2` `H2` `L1` */
            gen_op('*');
            vrotb(3);
            vrotb(3);
            gen_op('*');
            /* stack: `ML` `MH` `M1` `M2` */
            gen_op('+');
            gen_op('+');
        } else if (op == '+' || op == '-') {
            /* `XXX`: add non carry method too (for `MIPS` or alpha) */
            if (op == '+')
                op1 = TOK_ADDC1;
            else
                op1 = TOK_SUBC1;
            gen_op(op1);
            /* stack: `H1` `H2` (`L1` op `L2`) */
            vrotb(3);
            vrotb(3);
            gen_op(op1 + 1); /* `TOK_xxxC2` */
        } else {
            gen_op(op);
            /* stack: `H1` `H2` (`L1` op `L2`) */
            vrotb(3);
            vrotb(3);
            /* stack: (`L1` op `L2`) `H1` `H2` */
            gen_op(op);
            /* stack: (`L1` op `L2`) (`H1` op `H2`) */
        }
        /* stack: L H */
        lbuild(t);
        break;
    case TOK_SAR:
    case TOK_SHR:
    case TOK_SHL:
        if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
            t = vtop[-1].type.t;
            vswap();
            lexpand();
            vrotb(3);
            /* stack: L H shift */
            c = (int)vtop->c.i;
            /* constant: simpler */
            /* `NOTE`: all comments are for `SHL`. the other cases are
               done by swapping words */
            vpop();
            if (op != TOK_SHL)
                vswap();
            if (c >= 32) {
                /* stack: L H */
                vpop();
                if (c > 32) {
                    vpushi(c - 32);
                    gen_op(op);
                }
                if (op != TOK_SAR) {
                    vpushi(0);
                } else {
                    gv_dup();
                    vpushi(31);
                    gen_op(TOK_SAR);
                }
                vswap();
            } else {
                vswap();
                gv_dup();
                /* stack: H L L */
                vpushi(c);
                gen_op(op);
                vswap();
                vpushi(32 - c);
                if (op == TOK_SHL)
                    gen_op(TOK_SHR);
                else
                    gen_op(TOK_SHL);
                vrotb(3);
                /* stack: L L H */
                vpushi(c);
                if (op == TOK_SHL)
                    gen_op(TOK_SHL);
                else
                    gen_op(TOK_SHR);
                gen_op('|');
            }
            if (op != TOK_SHL)
                vswap();
            lbuild(t);
        } else {
            /* `XXX`: should provide a faster fallback on x86 ? */
            switch(op) {
            case TOK_SAR:
                func = TOK___ashrdi3;
                goto gen_func;
            case TOK_SHR:
                func = TOK___lshrdi3;
                goto gen_func;
            case TOK_SHL:
                func = TOK___ashldi3;
                goto gen_func;
            }
        }
        break;
    default:
        /* compare operations */
        t = vtop->type.t;
        vswap();
        lexpand();
        vrotb(3);
        lexpand();
        /* stack: `L1` `H1` `L2` `H2` */
        tmp = vtop[-1];
        vtop[-1] = vtop[-2];
        vtop[-2] = tmp;
        /* stack: `L1` `L2` `H1` `H2` */
        if (!cur_switch || cur_switch->bsym) {
            /* avoid differnt registers being saved in branches.
               `This` is not needed when comparing switch cases */
            save_regs(4);
        }
        /* compare high */
        op1 = op;
        /* when values are equal, we need to compare low words. since
           the jump is inverted, we invert the test too. */
        if (op1 == TOK_LT)
            op1 = TOK_LE;
        else if (op1 == TOK_GT)
            op1 = TOK_GE;
        else if (op1 == TOK_ULT)
            op1 = TOK_ULE;
        else if (op1 == TOK_UGT)
            op1 = TOK_UGE;
        a = 0;
        b = 0;
        gen_op(op1);
        if (op == TOK_NE) {
            b = gvtst(0, 0);
        } else {
            a = gvtst(1, 0);
            if (op != TOK_EQ) {
                /* generate non equal test */
                vpushi(0);
                vset_VT_CMP(TOK_NE);
                b = gvtst(0, 0);
            }
        }
        /* compare low. `Always` unsigned */
        op1 = op;
        if (op1 == TOK_LT)
            op1 = TOK_ULT;
        else if (op1 == TOK_LE)
            op1 = TOK_ULE;
        else if (op1 == TOK_GT)
            op1 = TOK_UGT;
        else if (op1 == TOK_GE)
            op1 = TOK_UGE;
        gen_op(op1);
#if 0//def TCC_TARGET_I386
        if (op == TOK_NE) { gsym(b); break; }
        if (op == TOK_EQ) { gsym(a); break; }
#endif
        gvtst_set(1, a);
        gvtst_set(0, b);
        break;
    }
}
#endif

/* normalize values */
static uint64_t value64(uint64_t l1, int t)
{
    if ((t & VT_BTYPE) == VT_LLONG
        || (PTR_SIZE == 8 && (t & VT_BTYPE) == VT_PTR))
        return l1;
    else if (t & VT_UNSIGNED)
        return (uint32_t)l1;
    else
        return (uint32_t)l1 | -(l1 & 0x80000000);
}

static uint64_t gen_opic_sdiv(uint64_t a, uint64_t b)
{
    uint64_t x = (a >> 63 ? -a : a) / (b >> 63 ? -b : b);
    return (a ^ b) >> 63 ? -x : x;
}

static int gen_opic_lt(uint64_t a, uint64_t b)
{
    return (a ^ (uint64_t)1 << 63) < (b ^ (uint64_t)1 << 63);
}

/* handle integer constant optimizations and various machine
   independent opt */
static void gen_opic(int op)
{
    SValue *v1 = vtop - 1;
    SValue *v2 = vtop;
    int t1 = v1->type.t & VT_BTYPE;
    int t2 = v2->type.t & VT_BTYPE;
    int c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    int c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    uint64_t l1 = c1 ? value64(v1->c.i, v1->type.t) : 0;
    uint64_t l2 = c2 ? value64(v2->c.i, v2->type.t) : 0;
    int shm = (t1 == VT_LLONG) ? 63 : 31;
    int r;

    if (c1 && c2) {
        switch(op) {
        case '+': l1 += l2; break;
        case '-': l1 -= l2; break;
        case '&': l1 &= l2; break;
        case '^': l1 ^= l2; break;
        case '|': l1 |= l2; break;
        case '*': l1 *= l2; break;

        case TOK_PDIV:
        case '/':
        case '%':
        case TOK_UDIV:
        case TOK_UMOD:
            /* if division by zero, generate explicit division */
            if (l2 == 0) {
                if (CONST_WANTED && !NOEVAL_WANTED)
                    tcc_error("division by zero in constant");
                goto general_case;
            }
            switch(op) {
            default: l1 = gen_opic_sdiv(l1, l2); break;
            case '%': l1 = l1 - l2 * gen_opic_sdiv(l1, l2); break;
            case TOK_UDIV: l1 = l1 / l2; break;
            case TOK_UMOD: l1 = l1 % l2; break;
            }
            break;
        case TOK_SHL: l1 <<= (l2 & shm); break;
        case TOK_SHR: l1 >>= (l2 & shm); break;
        case TOK_SAR:
            l1 = (l1 >> 63) ? ~(~l1 >> (l2 & shm)) : l1 >> (l2 & shm);
            break;
            /* tests */
        case TOK_ULT: l1 = l1 < l2; break;
        case TOK_UGE: l1 = l1 >= l2; break;
        case TOK_EQ: l1 = l1 == l2; break;
        case TOK_NE: l1 = l1 != l2; break;
        case TOK_ULE: l1 = l1 <= l2; break;
        case TOK_UGT: l1 = l1 > l2; break;
        case TOK_LT: l1 = gen_opic_lt(l1, l2); break;
        case TOK_GE: l1 = !gen_opic_lt(l1, l2); break;
        case TOK_LE: l1 = !gen_opic_lt(l2, l1); break;
        case TOK_GT: l1 = gen_opic_lt(l2, l1); break;
            /* logical */
        case TOK_LAND: l1 = l1 && l2; break;
        case TOK_LOR: l1 = l1 || l2; break;
        default:
            goto general_case;
        }
        v1->c.i = value64(l1, v1->type.t);
        v1->r |= v2->r & VT_NONCONST;
        vtop--;
    } else {
        /* if commutative ops, put c2 as constant */
        if (c1 && (op == '+' || op == '&' || op == '^' ||
                   op == '|' || op == '*' || op == TOK_EQ || op == TOK_NE)) {
            vswap();
            c2 = c1; //c = c1, c1 = c2, c2 = c;
            l2 = l1; //l = l1, l1 = l2, l2 = l;
        }
        if (c1 && ((l1 == 0 &&
                    (op == TOK_SHL || op == TOK_SHR || op == TOK_SAR)) ||
                   (l1 == -1 && op == TOK_SAR))) {
            /* treat (0 << x), (0 >> x) and (-1 >> x) as constant */
            vpop();
        } else if (c2 && ((l2 == 0 && (op == '&' || op == '*')) ||
                          (op == '|' &&
                            (l2 == -1 || (l2 == 0xFFFFFFFF && t2 != VT_LLONG))) ||
                          (l2 == 1 && (op == '%' || op == TOK_UMOD)))) {
            /* treat (x & 0), (x * 0), (x | -1) and (x % 1) as constant */
            if (l2 == 1)
                vtop->c.i = 0;
            vswap();
            vtop--;
        } else if (c2 && (((op == '*' || op == '/' || op == TOK_UDIV ||
                          op == TOK_PDIV) &&
                           l2 == 1) ||
                          ((op == '+' || op == '-' || op == '|' || op == '^' ||
                            op == TOK_SHL || op == TOK_SHR || op == TOK_SAR) &&
                           l2 == 0) ||
                          (op == '&' &&
                            (l2 == -1 || (l2 == 0xFFFFFFFF && t2 != VT_LLONG))))) {
            /* filter out `NOP` operations like x*1, x-0, x&-1... */
            vtop--;
        } else if (c2 && (op == '*' || op == TOK_PDIV || op == TOK_UDIV || op == TOK_UMOD)) {
            /* try to use shifts instead of muls or divs */
            if (l2 > 0 && (l2 & (l2 - 1)) == 0) {
                int n = -1;
                if (op == TOK_UMOD) {
                    vtop->c.i = l2 - 1;
                    op = '&';
                    goto general_case;
                }
                while (l2) {
                    l2 >>= 1;
                    n++;
                }
                vtop->c.i = n;
                if (op == '*')
                    op = TOK_SHL;
                else if (op == TOK_PDIV)
                    op = TOK_SAR;
                else
                    op = TOK_SHR;
            }
            goto general_case;
        } else if (c2 && (op == '+' || op == '-') &&
                   (r = vtop[-1].r & (VT_VALMASK | VT_LVAL | VT_SYM),
                    r == (VT_CONST | VT_SYM) || r == VT_LOCAL)) {
            /* symbol + constant case */
            if (op == '-')
                l2 = -l2;
	    l2 += vtop[-1].c.i;
	    /* `The` backends can't always deal with addends to symbols
	       larger than +-1<<31.  `Don`'t construct such.  */
	    if ((int)l2 != l2)
	        goto general_case;
            vtop--;
            vtop->c.i = l2;
        } else {
        general_case:
                /* call low level op generator */
                if (t1 == VT_LLONG || t2 == VT_LLONG ||
                    (PTR_SIZE == 8 && (t1 == VT_PTR || t2 == VT_PTR)))
                    gen_opl(op);
                else
                    gen_opi(op);
        }
        if (vtop->r == VT_CONST)
            vtop->r |= VT_NONCONST; /* is const, but only by optimization */
    }
}

#if defined TCC_TARGET_X86_64 || defined TCC_TARGET_I386 || defined TCC_TARGET_ARM64
# define gen_negf gen_opf
#elif defined TCC_TARGET_ARM
void gen_negf(int op)
{
    /* arm will detect 0-x and replace by vneg */
    vpushi(0), vswap(), gen_op('-');
}
#else
/* `XXX`: implement in `gen_opf`() for other backends too */
void gen_negf(int op)
{
    /* `In` `IEEE` negate(x) isn't subtract(0,x).  `Without` `NaNs` it's
       subtract(-0, x), but with them it's really a sign flip
       operation.  `We` implement this with bit manipulation and have
       to do some type reinterpretation for this, which `TPC` can do
       only via memory.  */
    int align, size, bt;
    size = type_size(&vtop->type, &align);
    bt = vtop->type.t & VT_BTYPE;
#if defined TCC_TARGET_X86_64 || defined TCC_TARGET_I386
    /* sizeof long double is 12 or 16 here, but it's really the 80bit
       extended float format.  */
    if (bt == VT_LDOUBLE)
        size = 10;
#endif
    if (nocode_wanted) /* `save_reg`() wont work */
        goto gv2;
    save_reg(gv(RC_TYPE(bt)));
    vdup();
    incr_bf_adr(size - 1);
    vdup();
    vpushi(0x80); /* flip sign */
    gen_op('^');
    vstore();
    vpop();
gv2:
    gv(RC_TYPE(bt)); /* -n is not a lvalue */
}
#endif

/* generate a floating point operation with constant propagation */
static void gen_opif(int op)
{
    int c1, c2, i, bt;
    SValue *v1, *v2;
#if defined _MSC_VER && defined __x86_64__
    /* avoid bad optimization with f1 -= f2 for f1:-0.0, f2:0.0 */
    volatile
#endif
    long double f1, f2;

    v1 = vtop - 1;
    v2 = vtop;
    if (op == TOK_NEG)
        v1 = v2;
    bt = v1->type.t & VT_BTYPE;

    /* currently, we cannot do computations with forward symbols */
    c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    if (c1 && c2) {
        if (bt == VT_FLOAT) {
            f1 = v1->c.f;
            f2 = v2->c.f;
        } else if (bt == VT_DOUBLE) {
            f1 = v1->c.d;
            f2 = v2->c.d;
        } else {
            f1 = v1->c.ld;
            f2 = v2->c.ld;
        }
        /* `NOTE`: we only do constant propagation if finite number (not
           `NaN` or infinity) (`ANSI` spec) */
        if (!(ieee_finite(f1) || !ieee_finite(f2)) && !CONST_WANTED)
            goto general_case;
        switch(op) {
        case '+': f1 += f2; break;
        case '-': f1 -= f2; break;
        case '*': f1 *= f2; break;
        case '/':
            if (f2 == 0.0) {
                union { float f; unsigned u; } x1, x2, y;
		/* `If` not in initializer we need to potentially generate
		   `FP` exceptions at runtime, otherwise we want to fold.  */
                if (!CONST_WANTED)
                    goto general_case;
                /* the run-time result of 0.0/0.0 on x87, also of other compilers
                   when used to compile the f1 /= f2 below, would be -nan */
                x1.f = f1, x2.f = f2;
                if (f1 == 0.0)
                    y.u = 0x7fc00000; /* nan */
                else
                    y.u = 0x7f800000; /* infinity */
                y.u |= (x1.u ^ x2.u) & 0x80000000; /* set sign */
                f1 = y.f;
                break;
            }
            f1 /= f2;
            break;
        case TOK_NEG:
            f1 = -f1;
            goto unary_result;
        case TOK_EQ:
            i = f1 == f2;
	make_int:
            vtop -= 2;
            vpushi(i);
            return;
        case TOK_NE:
            i = f1 != f2;
	    goto make_int;
        case TOK_LT:
            i = f1 < f2;
	    goto make_int;
        case TOK_GE:
            i = f1 >= f2;
	    goto make_int;
        case TOK_LE:
            i = f1 <= f2;
	    goto make_int;
        case TOK_GT:
            i = f1 > f2;
	    goto make_int;
        default:
            goto general_case;
        }
        vtop--;
    unary_result:
        /* `XXX`: overflow test ? */
        if (bt == VT_FLOAT) {
            v1->c.f = f1;
        } else if (bt == VT_DOUBLE) {
            v1->c.d = f1;
        } else {
            v1->c.ld = f1;
        }
    } else {
    general_case:
        if (op == TOK_NEG) {
            gen_negf(op);
        } else {
            gen_opf(op);
        }
    }
}

/* print a type. `If` 'varstr' is not `NULL`, then the variable is also
   printed in the type */
/* `XXX`: union */
/* `XXX`: add array and function pointers */
static void type_to_str(char *buf, int buf_size,
                 CType *type, const char *varstr)
{
    int bt, v, t;
    Sym *s, *sa;
    char buf1[256];
    const char *tstr;

    t = type->t;
    bt = t & VT_BTYPE;
    buf[0] = '\0';

    if (t & VT_STORAGE_EXTERN)
        pstrcat(buf, buf_size, "extern ");
    if (t & VT_STORAGE_STATIC)
        pstrcat(buf, buf_size, "static ");
    if (t & VT_STORAGE_TYPEDEF)
        pstrcat(buf, buf_size, "typedef ");
    if (t & VT_STORAGE_INLINE)
        pstrcat(buf, buf_size, "inline ");
    if (bt != VT_PTR) {
        if (t & VT_QUALIFIER_RESTRICT)
            pstrcat(buf, buf_size, "restrict ");
        if (t & VT_QUALIFIER_VOLATILE)
            pstrcat(buf, buf_size, "volatile ");
        if (t & VT_QUALIFIER_CONSTANT)
            pstrcat(buf, buf_size, "const ");
        if (t & VT_QUALIFIER_IMMUTABLE)
            pstrcat(buf, buf_size, "@immutable ");
    }
    if (((t & VT_DEFSIGN) && bt == VT_BYTE)
        || ((t & VT_UNSIGNED)
            && (bt == VT_SHORT || bt == VT_INT || bt == VT_LLONG)
            && !IS_ENUM(t)
            ))
        pstrcat(buf, buf_size, (t & VT_UNSIGNED) ? "unsigned " : "signed ");

    buf_size -= strlen(buf);
    buf += strlen(buf);

    switch(bt) {
    case VT_AUTO: {
        CType *tv = type_value_of(type->ref);
        if (tv) {
            type_to_str(buf, buf_size, tv, varstr);
            return;
        }
        tstr = "symbol";
        goto add_tstr;
    }
    case VT_VOID:
        tstr = "void";
        goto add_tstr;
    case VT_BOOL:
        tstr = "_Bool";
        goto add_tstr;
    case VT_BYTE:
        tstr = "char";
        goto add_tstr;
    case VT_SHORT:
        tstr = "short";
        goto add_tstr;
    case VT_INT:
        tstr = "int";
        goto maybe_long;
    case VT_LLONG:
        tstr = "long long";
    maybe_long:
        if (t & VT_LONG)
            tstr = "long";
        if (!IS_ENUM(t))
            goto add_tstr;
        tstr = "enum ";
        goto tstruct;
    case VT_FLOAT:
        tstr = "float";
        goto add_tstr;
    case VT_DOUBLE:
        tstr = "double";
        if (!(t & VT_LONG))
            goto add_tstr;
    case VT_LDOUBLE:
        tstr = "long double";
    add_tstr:
        pstrcat(buf, buf_size, tstr);
        break;
    case VT_STRUCT:
        if (is_tuple_type(type)) {
            Sym *f;
            pstrcat(buf, buf_size, "(");
            for (f = type->ref->next; f; f = f->next) {
                char buf2[256];
                type_to_str(buf2, sizeof(buf2), &f->type, NULL);
                pstrcat(buf, buf_size, buf2);
                if (f->next)
                    pstrcat(buf, buf_size, ", ");
            }
            if (type->ref->next && !type->ref->next->next)
                pstrcat(buf, buf_size, ",");
            pstrcat(buf, buf_size, ")");
            break;
        }
        if (is_slice_type(type) && type->ref->next && type->ref->next->next) {
            CType *elem = &type->ref->next->next->type.ref->type;
            if (varstr && '*' == *varstr)
                snprintf(buf1, sizeof(buf1), "(%s)[..]", varstr);
            else
                snprintf(buf1, sizeof(buf1), "%s[..]", varstr ? varstr : "");
            type_to_str(buf, buf_size, elem, buf1);
            goto no_var;
        }
        tstr = "struct ";
        if (IS_UNION(t))
            tstr = "union ";
    tstruct:
        pstrcat(buf, buf_size, tstr);
        v = type->ref->v & ~SYM_STRUCT;
        if (v >= SYM_FIRST_ANOM)
            pstrcat(buf, buf_size, "<anonymous>");
        else
            pstrcat(buf, buf_size, get_tok_str(v, NULL));
        break;
    case VT_FUNC:
        s = type->ref;
        buf1[0]=0;
        if (varstr && '*' == *varstr) {
            pstrcat(buf1, sizeof(buf1), "(");
            pstrcat(buf1, sizeof(buf1), varstr);
            pstrcat(buf1, sizeof(buf1), ")");
        }
        pstrcat(buf1, sizeof(buf1), "(");
        sa = s->next;
        while (sa != NULL) {
            char buf2[256];
            type_to_str(buf2, sizeof(buf2), &sa->type, NULL);
            pstrcat(buf1, sizeof(buf1), buf2);
            sa = sa->next;
            if (sa)
                pstrcat(buf1, sizeof(buf1), ", ");
        }
        if (s->f.func_type == FUNC_ELLIPSIS)
            pstrcat(buf1, sizeof(buf1), ", ...");
        pstrcat(buf1, sizeof(buf1), ")");
        type_to_str(buf, buf_size, &s->type, buf1);
        goto no_var;
    case VT_PTR:
        s = type->ref;
        if (t & (VT_ARRAY|VT_VLA)) {
            if (varstr && '*' == *varstr)
                snprintf(buf1, sizeof(buf1), "(%s)[%d]", varstr, s->c);
            else
                snprintf(buf1, sizeof(buf1), "%s[%d]", varstr ? varstr : "", s->c);
            type_to_str(buf, buf_size, &s->type, buf1);
            goto no_var;
        }
        pstrcpy(buf1, sizeof(buf1), "*");
        if (t & VT_QUALIFIER_RESTRICT)
            pstrcat(buf1, sizeof(buf1), "restrict ");
        if (t & VT_QUALIFIER_CONSTANT)
            pstrcat(buf1, sizeof(buf1), "const ");
        if (t & VT_QUALIFIER_IMMUTABLE)
            pstrcat(buf1, sizeof(buf1), "@immutable ");
        if (t & VT_QUALIFIER_VOLATILE)
            pstrcat(buf1, sizeof(buf1), "volatile ");
        if (varstr)
            pstrcat(buf1, sizeof(buf1), varstr);
        type_to_str(buf, buf_size, &s->type, buf1);
        goto no_var;
    }
    if (varstr) {
        pstrcat(buf, buf_size, " ");
        pstrcat(buf, buf_size, varstr);
    }
 no_var: ;
}

static void type_incompatibility_error(CType* st, CType* dt, const char* fmt)
{
    char buf1[256], buf2[256];
    type_to_str(buf1, sizeof(buf1), st, NULL);
    type_to_str(buf2, sizeof(buf2), dt, NULL);
    tcc_error(fmt, buf1, buf2);
}

static void type_incompatibility_warning(CType* st, CType* dt, const char* fmt)
{
    char buf1[256], buf2[256];
    type_to_str(buf1, sizeof(buf1), st, NULL);
    type_to_str(buf2, sizeof(buf2), dt, NULL);
    tcc_warning(fmt, buf1, buf2);
}

static int pointed_size(CType *type)
{
    int align;
    return type_size(pointed_type(type), &align);
}

static inline int is_null_pointer(SValue *p)
{
    if ((p->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_NONCONST)) != VT_CONST)
        return 0;
    return ((p->type.t & VT_BTYPE) == VT_INT && (uint32_t)p->c.i == 0) ||
        ((p->type.t & VT_BTYPE) == VT_LLONG && p->c.i == 0) ||
        ((p->type.t & VT_BTYPE) == VT_PTR &&
         (PTR_SIZE == 4 ? (uint32_t)p->c.i == 0 : p->c.i == 0) &&
         ((pointed_type(&p->type)->t & VT_BTYPE) == VT_VOID) &&
         0 == (pointed_type(&p->type)->t & VT_QUALIFIER)
         );
}

/* compare function types. `OLD` functions match any new functions */
static int is_compatible_func(CType *type1, CType *type2)
{
    Sym *s1, *s2;

    s1 = type1->ref;
    s2 = type2->ref;
    if (s1->f.func_call != s2->f.func_call)
        return 0;
    if (s1->f.func_type != s2->f.func_type
        && s1->f.func_type != FUNC_OLD
        && s2->f.func_type != FUNC_OLD)
        return 0;
    for (;;) {
        if (!is_compatible_unqualified_types(&s1->type, &s2->type))
            return 0;
        if (s1->f.func_type == FUNC_OLD || s2->f.func_type == FUNC_OLD )
            return 1;
        s1 = s1->next;
        s2 = s2->next;
        if (!s1)
            return !s2;
        if (!s2)
            return 0;
    }
}

/* return true if type1 and type2 are the same.  `If` unqualified is
   true, qualifiers on the types are ignored.
 */
static int compare_types(CType *type1, CType *type2, int unqualified)
{
    int bt1, t1, t2;

    if (IS_ENUM(type1->t)) {
        if (IS_ENUM(type2->t))
            return type1->ref == type2->ref;
        type1 = &type1->ref->type;
    } else if (IS_ENUM(type2->t))
        type2 = &type2->ref->type;

    t1 = type1->t & VT_TYPE;
    t2 = type2->t & VT_TYPE;
    if (unqualified) {
        /* strip qualifiers before comparing */
        t1 &= ~VT_QUALIFIER;
        t2 &= ~VT_QUALIFIER;
    }

    /* `Default` `Vs` explicit signedness only matters for char */
    if ((t1 & VT_BTYPE) != VT_BYTE) {
        t1 &= ~VT_DEFSIGN;
        t2 &= ~VT_DEFSIGN;
    }
    /* `XXX`: bitfields ? */
    if (t1 != t2)
        return 0;

    if ((t1 & VT_ARRAY)
        && !(type1->ref->c < 0
          || type2->ref->c < 0
          || type1->ref->c == type2->ref->c))
            return 0;

    /* test more complicated cases */
    bt1 = t1 & VT_BTYPE;
    if (bt1 == VT_PTR) {
        type1 = pointed_type(type1);
        type2 = pointed_type(type2);
        if (unqualified && (t1 & VT_ARRAY))
            return compare_types(type1, type2, 1);
        return is_compatible_types(type1, type2);
    } else if (bt1 == VT_STRUCT) {
        Sym *s1 = type1->ref, *s2 = type2->ref;
        if (s1 == s2)
            return 1;
        if (s1 && s2 && s1->a.is_tuple && s2->a.is_tuple) {
            Sym *f1 = s1->next, *f2 = s2->next;
            for (; f1 && f2; f1 = f1->next, f2 = f2->next)
                if (!compare_types(&f1->type, &f2->type, unqualified))
                    return 0;
            return f1 == NULL && f2 == NULL;
        }
        if (s1 && s2 && (s1->a.is_tuple || s2->a.is_tuple))
            return 0;
        /* `Slice` types are represented as anonymous structs.  `Compare` their
           element types instead of requiring the same struct symbol.  */
        if (s1 && s2 && s1->a.is_slice && s2->a.is_slice
            && s1->next && s1->next->next && s2->next && s2->next->next
            && (s1->next->next->type.t & VT_BTYPE) == VT_PTR
            && (s2->next->next->type.t & VT_BTYPE) == VT_PTR) {
            CType *pt1 = pointed_type(&s1->next->next->type);
            CType *pt2 = pointed_type(&s2->next->next->type);
            return compare_types(pt1, pt2, unqualified);
        }
        return 0;
    } else if (bt1 == VT_FUNC) {
        return is_compatible_func(type1, type2);
    } else {
        return 1;
    }
}
static int can_compare_array_or_slice_content(CType *type)
{
    if (is_slice_type(type))
        return 1;
    if (type->t & (VT_ARRAY | VT_VLA)) {
        if (type->ref && type->ref->c > 0)
            return 1;
    }
     return 0;
 }
 static void get_array_or_slice_elem(CType *type, CType *elem)
 {
    if (type->t & (VT_ARRAY | VT_VLA))
        *elem = type->ref->type;
    else if (is_slice_type(type))
        *elem = *pointed_type(&type->ref->next->next->type);
    else
        elem->t = VT_VOID;
}

#define CMP_OP 'C'
#define SHIFT_OP 'S'

/* `Check` if `OP1` and `OP2` can be "combined" with operation `OP`, the combined
   type is stored in `DEST` if non-null (except for pointer plus/minus) . */
static int combine_types(CType *dest, SValue *op1, SValue *op2, int op)
{
    CType *type1, *type2, type;
    int t1, t2, bt1, bt2;
    int ret = 1;

    /* for shifts, 'combine' only left operand */
    if (op == SHIFT_OP)
        op2 = op1;

    type1 = &op1->type, type2 = &op2->type;
    t1 = type1->t, t2 = type2->t;
    bt1 = t1 & VT_BTYPE, bt2 = t2 & VT_BTYPE;

    type.t = VT_VOID;
    type.ref = NULL;

     if (bt1 == VT_VOID || bt2 == VT_VOID) {
         if (op != '?')
             tcc_error("operation on void value");
         /* `NOTE`: as an extension, we accept void on only one side */
         type.t = VT_VOID;
     } else if (op == CMP_OP && (is_slice_type(type1) || is_slice_type(type2)) && can_compare_array_or_slice_content(type1) && can_compare_array_or_slice_content(type2)) {
         CType elem1, elem2;
         get_array_or_slice_elem(type1, &elem1);
         get_array_or_slice_elem(type2, &elem2);
         if (!is_compatible_unqualified_types(&elem1, &elem2))
             ret = 0;
         if ((type1->t & VT_ARRAY) && (type2->t & VT_ARRAY)
             && type1->ref->c > 0 && type2->ref->c > 0
             && type1->ref->c != type2->ref->c)
             ret = 0;
         type.t = VT_INT;
     } else if (bt1 == VT_PTR || bt2 == VT_PTR) {
        if (op == '+') {
          if (!is_integer_btype(bt1 == VT_PTR ? bt2 : bt1))
            ret = 0;
        }
        /* http://port70.net/~nsz/c/c99/n1256.html#6.5.15p6 */
        /* `If` one is a null ptr constant the result type is the other.  */
        else if (is_null_pointer (op2)) type = *type1;
        else if (is_null_pointer (op1)) type = *type2;
        else if (bt1 != bt2) {
            /* accept comparison or cond-expr between pointer and integer
               with a warning */
            if ((op == '?' || op == CMP_OP)
                && (is_integer_btype(bt1) || is_integer_btype(bt2)))
              tcc_warning("pointer/integer mismatch in %s",
                          op == '?' ? "conditional expression" : "comparison");
            else if (op != '-' || !is_integer_btype(bt2))
              ret = 0;
            type = *(bt1 == VT_PTR ? type1 : type2);
        } else {
            CType *pt1 = pointed_type(type1);
            CType *pt2 = pointed_type(type2);
            int pbt1 = pt1->t & VT_BTYPE;
            int pbt2 = pt2->t & VT_BTYPE;
            int newquals, copied = 0;
            if (pbt1 != VT_VOID && pbt2 != VT_VOID
                && !compare_types(pt1, pt2, 1/*unqualif*/)) {
                if (op != '?' && op != CMP_OP)
                  ret = 0;
                else
                  type_incompatibility_warning(type1, type2,
                    op == '?'
                     ? "pointer type mismatch in conditional expression ('%s' and '%s')"
                     : "pointer type mismatch in comparison('%s' and '%s')");
            }
            if (op == '?') {
                /* pointers to void get preferred, otherwise the
                   pointed to types minus qualifs should be compatible */
                type = *((pbt1 == VT_VOID) ? type1 : type2);
                /* combine qualifs */
                newquals = type_qualifiers(pt1) | type_qualifiers(pt2);
                if ((~type_qualifiers(pointed_type(&type))) & newquals)
                  {
                    /* copy the pointer target symbol */
                    type.ref = sym_push(SYM_FIELD, &type.ref->type,
                                        0, type.ref->c);
                    copied = 1;
                    parse_btype_qualify(pointed_type(&type), newquals);
                  }
                /* pointers to incomplete arrays get converted to
                   pointers to completed ones if possible */
                if (pt1->t & VT_ARRAY
                    && pt2->t & VT_ARRAY
                    && pointed_type(&type)->ref->c < 0
                    && (pt1->ref->c > 0 || pt2->ref->c > 0))
                  {
                    if (!copied)
                      type.ref = sym_push(SYM_FIELD, &type.ref->type,
                                          0, type.ref->c);
                    pointed_type(&type)->ref =
                        sym_push(SYM_FIELD, &pointed_type(&type)->ref->type,
                                 0, pointed_type(&type)->ref->c);
                    pointed_type(&type)->ref->c =
                        0 < pt1->ref->c ? pt1->ref->c : pt2->ref->c;
                  }
            }
        }
        if (op == CMP_OP)
          type.t = VT_SIZE_T;
    } else if (bt1 == VT_STRUCT || bt2 == VT_STRUCT) {
        if (op != '?' || !compare_types(type1, type2, 1))
          ret = 0;
        type = *type1;
    } else if (is_float(bt1) || is_float(bt2)) {
        if (bt1 == VT_LDOUBLE || bt2 == VT_LDOUBLE) {
            type.t = VT_LDOUBLE;
        } else if (bt1 == VT_DOUBLE || bt2 == VT_DOUBLE) {
            type.t = VT_DOUBLE;
        } else {
            type.t = VT_FLOAT;
        }
    } else if (bt1 == VT_LLONG || bt2 == VT_LLONG) {
        /* cast to biggest op */
        type.t = VT_LLONG | VT_LONG;
        if (bt1 == VT_LLONG)
          type.t &= t1;
        if (bt2 == VT_LLONG)
          type.t &= t2;
        /* convert to unsigned if it does not fit in a long long */
        if ((t1 & (VT_BTYPE | VT_UNSIGNED)) == (VT_LLONG | VT_UNSIGNED) ||
            (t2 & (VT_BTYPE | VT_UNSIGNED)) == (VT_LLONG | VT_UNSIGNED))
          type.t |= VT_UNSIGNED;
    } else {
        /* integer operations */
        type.t = VT_INT | (VT_LONG & (t1 | t2));
        /* convert to unsigned if it does not fit in an integer */
        if (((t1 & (VT_BTYPE | VT_UNSIGNED)) == (VT_INT | VT_UNSIGNED)
                && (!(t1 & VT_BITFIELD) || BIT_SIZE(t1) == 32))
         || ((t2 & (VT_BTYPE | VT_UNSIGNED)) == (VT_INT | VT_UNSIGNED)
                && (!(t2 & VT_BITFIELD) || BIT_SIZE(t2) == 32)))
          type.t |= VT_UNSIGNED;
    }
    if (dest)
      *dest = type;
    return ret;
}

/* generic `gen_op`: handles types problems */
ST_FUNC void gen_op(int op)
{
    int t1, t2, bt1, bt2, t;
    CType type1, combtype;
    int op_class = op;

    if (op == TOK_SHR || op == TOK_SAR || op == TOK_SHL)
        op_class = SHIFT_OP;
    else if (TOK_ISCOND(op)) /* == != > ... */
        op_class = CMP_OP;

redo:
    t1 = vtop[-1].type.t;
    t2 = vtop[0].type.t;
    bt1 = t1 & VT_BTYPE;
    bt2 = t2 & VT_BTYPE;

    if (bt1 == VT_FUNC || bt2 == VT_FUNC) {
	if (bt2 == VT_FUNC) {
	    mk_pointer(&vtop->type);
	    gaddrof();
	}
	if (bt1 == VT_FUNC) {
	    vswap();
	    mk_pointer(&vtop->type);
	    gaddrof();
	    vswap();
	}
	goto redo;
     } else if (!combine_types(&combtype, vtop - 1, vtop, op_class)) {
 op_err:
         tcc_error("invalid operand types for binary operation");
     } else if ((op == TOK_EQ || op == TOK_NE) &&
              (is_slice_type(&vtop[-1].type) || is_slice_type(&vtop[0].type)) &&
              can_compare_array_or_slice_content(&vtop[-1].type) &&
              can_compare_array_or_slice_content(&vtop[0].type) &&
              !is_null_pointer(vtop) && !is_null_pointer(vtop - 1)) {
         CType elem, slice_type;
         int esize, ealign, la, lb;

         get_array_or_slice_elem(&vtop[-1].type, &elem);
         slice_type.t = VT_STRUCT;
         slice_type.ref = make_slice_type(&elem);
         esize = type_size(&elem, &ealign);

         /* `Right` operand is at vtop */
         if (is_slice_type(&vtop->type)) {
             CType re;
             get_array_or_slice_elem(&vtop->type, &re);
             if (!is_compatible_unqualified_types(&re, &elem))
                 cast_error(&vtop->type, &slice_type);
             lb = copy_slice_to_temp(&vtop->type);
         } else if (vtop->type.t & (VT_ARRAY | VT_VLA)) {
             if (!is_compatible_unqualified_types(&vtop->type.ref->type, &elem))
                 cast_error(&vtop->type, &slice_type);
             convert_array_to_slice(&slice_type);
             lb = copy_slice_to_temp(&slice_type);
         } else {
             tcc_error("cannot compare array/slice with non-array/non-slice");
         }

         /* `Left` operand is now at vtop */
         if (is_slice_type(&vtop->type)) {
             CType le;
             get_array_or_slice_elem(&vtop->type, &le);
             if (!is_compatible_unqualified_types(&le, &elem))
                 cast_error(&vtop->type, &slice_type);
             la = copy_slice_to_temp(&vtop->type);
         } else if (vtop->type.t & (VT_ARRAY | VT_VLA)) {
             if (!is_compatible_unqualified_types(&vtop->type.ref->type, &elem))
                 cast_error(&vtop->type, &slice_type);
             convert_array_to_slice(&slice_type);
             la = copy_slice_to_temp(&slice_type);
         } else {
             tcc_error("cannot compare array/slice with non-array/non-slice");
         }

         vpush_helper_func(TOK___darray_eq);
         vset(&char_pointer_type, VT_LOCAL, la);
         vset(&char_pointer_type, VT_LOCAL, lb);
         vpushs(esize);
         gfunc_call(3);
         vpushi(0);
         vtop->type.t = VT_INT;
         vtop->r = REG_IRET;
         if (op == TOK_NE) {
             vpushi(0);
             gen_op(TOK_EQ);
         }
     } else if (op_class == CMP_OP &&
              is_slice_type(&vtop[-1].type) && is_slice_type(&vtop[0].type)) {
         tcc_error("relational operators not supported for arrays/slices");
     } else if (bt1 == VT_PTR || bt2 == VT_PTR) {
        /* relational op: must be both pointers */
        int align;
        if (op_class == CMP_OP)
            goto std_op;
        if (cur_scope->fn.safety == FUNC_SAFETY_SAFE)
            tcc_error("pointer arithmetic not allowed in @safe scope");
        else {
            if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
        }
        /* if both pointers, then it must be the '-' op */
        if (bt1 == VT_PTR && bt2 == VT_PTR) {
            if (op != '-')
                goto op_err;
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            vrott(3);
            gen_opic(op);
            vtop->type.t = VT_PTRDIFF_T;
            vswap();
            gen_op(TOK_PDIV);
        } else {
            /* exactly one pointer : must be '+' or '-'. */
            if (op != '-' && op != '+')
                goto op_err;
            /* `Put` pointer as first operand */
            if (bt2 == VT_PTR) {
                vswap();
                t = t1, t1 = t2, t2 = t;
                bt2 = bt1;
            }
#if PTR_SIZE == 4
            if (bt2 == VT_LLONG)
                /* `XXX`: truncate here because `gen_opl` can't handle ptr + long long */
                gen_cast_s(VT_INT);
#endif
            type1 = vtop[-1].type;
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            gen_op('*');
#ifdef CONFIG_TCC_BCHECK
            if (tcc_state->do_bounds_check && !CONST_WANTED) {
                /* if bounded pointers, we generate a special code to
                   test bounds */
                if (op == '-') {
                    vpushi(0);
                    vswap();
                    gen_op('-');
                }
                gen_bounded_ptr_add();
            } else
#endif
            {
                gen_opic(op);
            }
            type1.t &= ~(VT_ARRAY|VT_VLA);
            /* put again type if `gen_opic`() swaped operands */
            vtop->type = type1;
        }
    } else {
        /* floats can only be used for a few operations */
        if (is_float(combtype.t)
            && op != '+' && op != '-' && op != '*' && op != '/'
            && op_class != CMP_OP) {
            goto op_err;
        }
    std_op:
        t = t2 = combtype.t;
        /* special case for shifts and long long: we keep the shift as
           an integer */
        if (op_class == SHIFT_OP)
            t2 = VT_INT;
        /* `XXX`: currently, some unsigned operations are explicit, so
           we modify them here */
        if (t & VT_UNSIGNED) {
            if (op == TOK_SAR)
                op = TOK_SHR;
            else if (op == '/')
                op = TOK_UDIV;
            else if (op == '%')
                op = TOK_UMOD;
            else if (op == TOK_LT)
                op = TOK_ULT;
            else if (op == TOK_GT)
                op = TOK_UGT;
            else if (op == TOK_LE)
                op = TOK_ULE;
            else if (op == TOK_GE)
                op = TOK_UGE;
        }
        vswap();
        gen_cast_s(t);
        vswap();
        gen_cast_s(t2);
        if (is_float(t))
            gen_opif(op);
        else
            gen_opic(op);
        if (op_class == CMP_OP) {
            /* relational op: the result is an int */
            vtop->type.t = VT_INT;
        } else {
            vtop->type.t = t;
        }
    }
    // Make sure that we have converted to an rvalue:
    if (vtop->r & VT_LVAL)
        gv(RC_TYPE(vtop->type.t));
}

#if defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64 || defined TCC_TARGET_ARM
#define gen_cvt_itof1 gen_cvt_itof
#else
/* generic itof for unsigned long long case */
static void gen_cvt_itof1(int t)
{
    if ((vtop->type.t & (VT_BTYPE | VT_UNSIGNED)) ==
        (VT_LLONG | VT_UNSIGNED)) {

        if (t == VT_FLOAT)
            vpush_helper_func(TOK___floatundisf);
#if LDOUBLE_SIZE != 8
        else if (t == VT_LDOUBLE)
            vpush_helper_func(TOK___floatundixf);
#endif
        else
            vpush_helper_func(TOK___floatundidf);
        vrott(2);
        gfunc_call(1);
        vpushi(0);
        PUT_R_RET(vtop, t);
    } else {
        gen_cvt_itof(t);
    }
}
#endif

#if defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
#define gen_cvt_ftoi1 gen_cvt_ftoi
#else
/* generic ftoi for unsigned long long case */
static void gen_cvt_ftoi1(int t)
{
    int st;
    if (t == (VT_LLONG | VT_UNSIGNED)) {
        /* not handled natively */
        st = vtop->type.t & VT_BTYPE;
        if (st == VT_FLOAT)
            vpush_helper_func(TOK___fixunssfdi);
#if LDOUBLE_SIZE != 8
        else if (st == VT_LDOUBLE)
            vpush_helper_func(TOK___fixunsxfdi);
#endif
        else
            vpush_helper_func(TOK___fixunsdfdi);
        vrott(2);
        gfunc_call(1);
        vpushi(0);
        PUT_R_RET(vtop, t);
    } else {
        gen_cvt_ftoi(t);
    }
}
#endif

/* special delayed cast for char/short */
static void force_charshort_cast(void)
{
    int sbt = BFGET(vtop->r, VT_MUSTCAST) == 2 ? VT_LLONG : VT_INT;
    int dbt = vtop->type.t;
    vtop->r &= ~VT_MUSTCAST;
    vtop->type.t = sbt;
    gen_cast_s(dbt == VT_BOOL ? VT_BYTE|VT_UNSIGNED : dbt);
    vtop->type.t = dbt;
}

static void gen_cast_s(int t)
{
    CType type;
    type.t = t;
    type.ref = NULL;
    gen_cast(&type);
}

/* cast 'vtop' to 'type'. `Casting` to bitfields is forbidden. */
static void gen_cast(CType *type)
{
    int sbt, dbt, sf, df, c;
    int dbt_bt, sbt_bt, ds, ss, bits, trunc;

    /* special delayed cast for char/short */
    if (vtop->r & VT_MUSTCAST)
        force_charshort_cast();

    /* bitfields first get cast to ints */
    if (vtop->type.t & VT_BITFIELD)
        gv(RC_INT);

    if (IS_ENUM(type->t) && type->ref->c < 0)
        tcc_error("cast to incomplete type");

    dbt = type->t & (VT_BTYPE | VT_UNSIGNED);
    sbt = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
    if (sbt == VT_FUNC)
        sbt = VT_PTR;

again:
    if (sbt != dbt) {
        sf = is_float(sbt);
        df = is_float(dbt);
        dbt_bt = dbt & VT_BTYPE;
        sbt_bt = sbt & VT_BTYPE;
        if (dbt_bt == VT_VOID) {
            /* do not confuse backends with `VT_VOID` in registers */
            vpop(), vpushi(0);
            goto done;
        }
        if (sbt_bt == VT_VOID) {
error:
            cast_error(&vtop->type, type);
        }

        c = (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
        if (c) {
            /* constant case: we can do it now */
            /* `XXX`: in `ISOC`, cannot do it if error in convert */
            if (sbt == VT_FLOAT)
                vtop->c.ld = vtop->c.f;
            else if (sbt == VT_DOUBLE)
                vtop->c.ld = vtop->c.d;

            if (df) {
                if (sbt_bt == VT_LLONG) {
                    if ((sbt & VT_UNSIGNED) || !(vtop->c.i >> 63))
                        vtop->c.ld = vtop->c.i;
                    else
                        vtop->c.ld = -(long double)-vtop->c.i;
                } else if(!sf) {
                    if ((sbt & VT_UNSIGNED) || !(vtop->c.i >> 31))
                        vtop->c.ld = (uint32_t)vtop->c.i;
                    else
                        vtop->c.ld = -(long double)-(uint32_t)vtop->c.i;
                }

                if (dbt == VT_FLOAT)
                    vtop->c.f = (float)vtop->c.ld;
                else if (dbt == VT_DOUBLE)
                    vtop->c.d = (double)vtop->c.ld;
            } else if (sf && dbt == VT_BOOL) {
                vtop->c.i = (vtop->c.ld != 0);
            } else {
                if(sf) {
                    if (dbt & VT_UNSIGNED)
                        vtop->c.i = (uint64_t)vtop->c.ld;
                    else
                        vtop->c.i = (int64_t)vtop->c.ld;
                }
                else if (sbt_bt == VT_LLONG || (PTR_SIZE == 8 && sbt == VT_PTR))
                    ;
                else if (sbt & VT_UNSIGNED)
                    vtop->c.i = (uint32_t)vtop->c.i;
                else
                    vtop->c.i = ((uint32_t)vtop->c.i | -(vtop->c.i & 0x80000000));

                if (dbt_bt == VT_LLONG || (PTR_SIZE == 8 && dbt == VT_PTR))
                    ;
                else if (dbt == VT_BOOL)
                    vtop->c.i = (vtop->c.i != 0);
                else {
                    uint32_t m = dbt_bt == VT_BYTE ? 0xff :
                                 dbt_bt == VT_SHORT ? 0xffff :
                                  0xffffffff;
                    vtop->c.i &= m;
                    if (!(dbt & VT_UNSIGNED))
                        vtop->c.i |= -(vtop->c.i & ((m >> 1) + 1));
                }
            }
            goto done;

        } else if (dbt == VT_BOOL
            && (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM))
                == (VT_CONST | VT_SYM)) {
            /* addresses are considered non-zero (see tcctest.c:sinit23) */
            vtop->r = VT_CONST;
            vtop->c.i = 1;
            goto done;
        }

        /* cannot generate code for global or static initializers */
        if (nocode_wanted & DATA_ONLY_WANTED) {
            if (df)
                vtop->r = get_reg(RC_FLOAT); /* don't confuse backends */
            goto done;
        }

        /* non constant case: generate code */
        if (dbt == VT_BOOL) {
            gen_test_zero(TOK_NE);
            goto done;
        }

        if (sf || df) {
            if (sf && df) {
                /* convert from fp to fp */
                gen_cvt_ftof(dbt);
            } else if (df) {
                /* convert int to fp */
                gen_cvt_itof1(dbt);
            } else {
                /* convert fp to int */
                sbt = dbt;
                if (dbt_bt != VT_LLONG && dbt_bt != VT_INT)
                    sbt = VT_INT;
                gen_cvt_ftoi1(sbt);
                goto again; /* may need char/short cast */
            }
            goto done;
        }

        ds = btype_size(dbt_bt);
        ss = btype_size(sbt_bt);
        if (ds == 0 || ss == 0)
            goto error;

        /* same size and no sign conversion needed */
        if (ds == ss && ds >= 4)
            goto done;
        if (dbt_bt == VT_PTR || sbt_bt == VT_PTR) {
            tcc_warning("cast between pointer and integer of different size");
            if (sbt_bt == VT_PTR) {
                /* put integer type to allow logical operations below */
                vtop->type.t = (PTR_SIZE == 8 ? VT_LLONG : VT_INT);
            }
        }

        /* processor allows { int a = 0, b = *(char*)&a; }
           `That` means that if we cast to less width, we can just
           change the type and read it still later. */
        #define ALLOW_SUBTYPE_ACCESS 1

        if (ALLOW_SUBTYPE_ACCESS && (vtop->r & VT_LVAL)) {
            /* value still in memory */
            if (ds <= ss)
                goto done;
            /* ss <= 4 here */
            if (ds <= 4 && !(dbt == (VT_SHORT | VT_UNSIGNED) && sbt == VT_BYTE)) {
                gv(RC_INT);
                goto done; /* no 64bit envolved */
            }
        }
        gv(RC_INT);

        trunc = 0;
#if PTR_SIZE == 4
        if (ds == 8) {
            /* generate high word */
            if (sbt & VT_UNSIGNED) {
                vpushi(0);
                gv(RC_INT);
            } else {
                gv_dup();
                vpushi(31);
                gen_op(TOK_SAR);
            }
            lbuild(dbt);
        } else if (ss == 8) {
            /* from long long: just take low order word */
            lexpand();
            vpop();
        }
        ss = 4;

#elif PTR_SIZE == 8
        if (ds == 8) {
            /* need to convert from 32bit to 64bit */
            if (sbt & VT_UNSIGNED) {
#if defined(TCC_TARGET_RISCV64)
                /* `RISC`-V keeps 32bit vals in registers sign-extended.
                   `So` here we need a zero-extension.  */
                trunc = 32;
#else
                goto done;
#endif
            } else {
                gen_cvt_sxtw();
                goto done;
            }
            ss = ds, ds = 4, dbt = sbt;
        } else if (ss == 8) {
            /* `RISC`-V keeps 32bit vals in registers sign-extended.
               `So` here we need a sign-extension for signed types and
               zero-extension. for unsigned types. */
#if !defined(TCC_TARGET_RISCV64)
            trunc = 32; /* zero upper 32 bits for non `RISC`-V targets */
#endif
        } else {
            ss = 4;
        }
#endif

        if (ds >= ss)
            goto done;
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64 || defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
    if (ss == 4) {
        gen_cvt_csti(dbt);
        goto done;
    }
#endif
        bits = (ss - ds) * 8;
        /* for unsigned, `gen_op` will convert `SAR` to `SHR` */
        vtop->type.t = (ss == 8 ? VT_LLONG : VT_INT) | (dbt & VT_UNSIGNED);
        vpushi(bits);
        gen_op(TOK_SHL);
        vpushi(bits - trunc);
        gen_op(TOK_SAR);
        vpushi(trunc);
        gen_op(TOK_SHR);
    }
done:
    vtop->type = *type;
    vtop->type.t &= ~ ( VT_QUALIFIER | VT_ARRAY | VT_STORAGE_TLS );
}

/* return type size as known at compile time. `Put` alignment at 'a' */
ST_FUNC int type_size(CType *type, int *a)
{
    Sym *s;
    int bt;

    bt = type->t & VT_BTYPE;
    if (bt == VT_AUTO) {
        CType *tv = type_value_of(type->ref);
        if (tv) {
            int ts = type_size(tv, a);
            return ts;
        }
        *a = 1;
        return 1;
    }
    if (bt == VT_STRUCT) {
        /* struct/union */
        s = type->ref;
        *a = s->r;
        return s->c;
    } else if (bt == VT_PTR) {
        if (type->t & VT_ARRAY) {
            int ts;
            s = type->ref;
            ts = type_size(&s->type, a);
            if (s->c < 0)
                return s->c;
            return ts * s->c;
        } else {
            *a = PTR_SIZE;
            return PTR_SIZE;
        }
    } else if (IS_ENUM(type->t) && type->ref->c < 0) {
        *a = 0;
        return -1; /* incomplete enum */
    } else if (bt == VT_LDOUBLE) {
        *a = LDOUBLE_ALIGN;
        return LDOUBLE_SIZE;
    } else if (bt == VT_DOUBLE || bt == VT_LLONG) {
#if (defined TCC_TARGET_I386 && !defined TCC_TARGET_PE) \
 || (defined TCC_TARGET_ARM && !defined TCC_ARM_EABI)
        *a = 4;
#else
        *a = 8;
#endif
        return 8;
    } else if (bt == VT_INT || bt == VT_FLOAT) {
        *a = 4;
        return 4;
    } else if (bt == VT_SHORT) {
        *a = 2;
        return 2;
    } else if (bt == VT_QLONG || bt == VT_QFLOAT) {
        *a = 8;
        return 16;
    } else if (bt == VT_SLICE) {
        *a = PTR_SIZE;
        return 2 * PTR_SIZE;
    } else {
        /* char, void, function, `_Bool` */
        *a = 1;
        return 1;
    }
}

/* push type size as known at runtime time on top of value stack. `Put`
   alignment at 'a' */
static void vpush_type_size(CType *type, int *a)
{
    if (type->t & VT_VLA) {
        type_size(&type->ref->type, a);
        vset(&int_type, VT_LOCAL|VT_LVAL, type->ref->c);
    } else {
        int size = type_size(type, a);
        if (size < 0)
            tcc_error("unknown type size");
        vpushs(size);
    }
}

/* return the pointed type of t */
static inline CType *pointed_type(CType *type)
{
    return &type->ref->type;
}

/* return true if 'type' is a slice type (T[..]) */
static int is_slice_type(CType *type)
{
    return (type->t & VT_BTYPE) == VT_STRUCT
        && type->ref && type->ref->a.is_slice;
}

/* `Return` true if 'type' is a scalar type, i.e. a single arithmetic,
   pointer or boolean value that can be held in a temporary of its own
   type.  */
static int is_scalar_value_type(CType *type)
{
    switch (type->t & VT_BTYPE) {
    case VT_BYTE: case VT_SHORT: case VT_INT: case VT_LLONG:
    case VT_PTR: case VT_BOOL:
    case VT_FLOAT: case VT_DOUBLE: case VT_LDOUBLE:
        return 1;
    default:
        return 0;
    }
}

/* `Return` the number of elements an array of type 'type' has when it is
   viewed through a slice.  `Arrays` that hold a string literal store the
   terminating null character as well, which is not part of the slice.  */
static int array_slice_length(CType *type)
{
    int n = type->ref->c;
    if (type->ref->a.str_lit && n > 0)
        --n;
    return n;
}

/* return true if token 't' can start a type specifier list */
static int is_type_start(int t)
{
    Sym *s;

    if (t >= TOK_UIDENT && get_tok_str(t, NULL)[0] == '$')
        return 0;

    switch (t) {
    case TOK_VOID: case TOK_CHAR: case TOK_SHORT: case TOK_INT:
    case TOK_LONG: case TOK_FLOAT: case TOK_DOUBLE: case TOK_BOOL:
    case TOK_COMPLEX: case TOK_IMAGINARY:
    case TOK_STRUCT: case TOK_UNION: case TOK_ENUM:
    case TOK_SIGNED1: case TOK_SIGNED2: case TOK_SIGNED3:
    case TOK_UNSIGNED: case TOK__Atomic:
    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
    case TOK_TYPEOF1: case TOK_TYPEOF2: case TOK_TYPEOF3:
    case TOK_ALIGNAS: case TOK_EXTENSION:
    case TOK_IMMUTABLE:
        return 1;
    case '@':
        return tcc_state->lang_d;
    case TOK_D_CFLOAT: case TOK_D_CDOUBLE: case TOK_D_CREAL:
    case TOK_D_IFLOAT: case TOK_D_IDOUBLE: case TOK_D_IREAL:
        return tcc_state->lang_d;
    }
    s = sym_find(t);
    return s && (s->type.t & VT_STORAGE_TYPEDEF);
}

static void check_restrict_type(CType *type)
{
    while ((type->t & VT_BTYPE) == VT_PTR) {
        if ((type->t & VT_QUALIFIER_RESTRICT)
            && (pointed_type(type)->t & VT_BTYPE) == VT_FUNC)
            tcc_error("restrict-qualified type must be a pointer to object or incomplete type");
        type = pointed_type(type);
    }
    if (type->t & VT_QUALIFIER_RESTRICT)
        tcc_error("restrict-qualified type must be a pointer to object or incomplete type");
}

/* `Array` qualifiers are represented on the element type. */
static int type_qualifiers(CType *type)
{
    while (type->t & VT_ARRAY)
        type = pointed_type(type);
    return type->t & VT_QUALIFIER;
}

/* modify type so that its it is a pointer to type. */
ST_FUNC void mk_pointer(CType *type)
{
    Sym *s;
    s = sym_push(SYM_FIELD, type, 0, -1);
    type->t = VT_PTR | (type->t & VT_STORAGE);
    type->ref = s;
}

/* return true if type1 and type2 are exactly the same (including
   qualifiers).
*/
static int is_compatible_types(CType *type1, CType *type2)
{
    return compare_types(type1,type2,0);
}

/* return true if type1 and type2 are the same (ignoring qualifiers).
*/
static int is_compatible_unqualified_types(CType *type1, CType *type2)
{
    return compare_types(type1,type2,1);
}

static void cast_error(CType *st, CType *dt)
{
    type_incompatibility_error(st, dt, "cannot convert '%s' to '%s'");
}

/* `Convert` the array expression on top of the value stack into a slice
   value of type 'dt'.  `Return` non-zero if a conversion was performed.  */
static int convert_array_to_slice(CType *dt)
{
    CType st, len_type, ptr_type;
    Sym *s;
    int size, align, r2, l, elem_size;

    st = vtop->type;
    if (!(st.t & (VT_ARRAY | VT_VLA)) || !is_slice_type(dt))
        return 0;

    s = dt->ref;
    ptr_type = s->next->next->type;
    if (!is_compatible_unqualified_types(&st.ref->type,
                                         pointed_type(&ptr_type)))
        cast_error(&st, dt);

    size = type_size(dt, &align);
    l = get_temp_local_var(size, align, &r2);

    /* .length */
    if (st.t & VT_VLA) {
        elem_size = type_size(&st.ref->type, &align);
        vset(&int_type, VT_LOCAL | VT_LVAL, st.ref->c);
        if (elem_size != 1) {
            vpushi(elem_size);
            gen_op('/');
        }
    } else {
        vpushs(array_slice_length(&st));
    }
    len_type = s->next->type;
    vset(&len_type, VT_LOCAL | VT_LVAL, l);
    vswap();
    vstore();
    vpop();

    /* .ptr */
    vset(&ptr_type, VT_LOCAL | VT_LVAL, l + PTR_SIZE);
    vswap();
    vstore();
    vpop();

    vset(dt, VT_LOCAL | VT_LVAL, l);
    vtop->r2 = r2;
    return 1;
}

/* verify type compatibility to store vtop in 'dt' type */
static void verify_assign_cast(CType *dt)
{
    CType *st, *type1, *type2;
    int dbt, sbt, qualwarn, lvl;

    st = &vtop->type; /* source type */
    dbt = dt->t & VT_BTYPE;
    sbt = st->t & VT_BTYPE;
    if ((vtop->r & VT_LVAL) && vtop->sym && vtop->sym->a.nocopy)
        tcc_error("cannot copy @nocopy instance '%s'", get_tok_str(vtop->sym->v & ~SYM_FIELD, NULL));
    if (sbt == VT_STRUCT && st->ref && st->ref->a.nocopy)
        tcc_error("rhs struct '%s' is not copyable (or aliasable)", get_tok_str(st->ref->v & ~SYM_STRUCT, NULL));
    if (dt->t & VT_QUALIFIER_CONSTANT)
        tcc_warning("assignment of read-only location");
    switch(dbt) {
    case VT_VOID:
        if (sbt != dbt)
            tcc_error("assignment to void expression");
        break;
    case VT_PTR:
        /* special cases for pointers */
        /* '0' can also be a pointer */
        if (is_null_pointer(vtop))
            break;
        /* accept implicit pointer to integer cast with warning */
        if (is_integer_btype(sbt)) {
            tcc_warning("assignment makes pointer from integer without a cast");
            break;
        }
        type1 = pointed_type(dt);
        if (sbt == VT_PTR)
            type2 = pointed_type(st);
        else if (sbt == VT_FUNC)
            type2 = st; /* a function is implicitly a function pointer */
        else
            goto error;
        if (is_compatible_types(type1, type2))
            break;
        for (qualwarn = lvl = 0;; ++lvl) {
            int discards = (type2->t & ~type1->t) & VT_QUALIFIER;
            /* dropping 'immutable' is only allowed for 'const' */
            if ((discards & VT_QUALIFIER_IMMUTABLE) && !(type1->t & VT_QUALIFIER_CONSTANT))
                tcc_error("assignment discards 'immutable' qualifier");
            if (discards & ~VT_QUALIFIER_IMMUTABLE)
                qualwarn = 1;
            dbt = type1->t & (VT_BTYPE|VT_LONG);
            sbt = type2->t & (VT_BTYPE|VT_LONG);
            if (dbt != VT_PTR || sbt != VT_PTR)
                break;
            type1 = pointed_type(type1);
            type2 = pointed_type(type2);
        }
        if (!is_compatible_unqualified_types(type1, type2)) {
            if ((dbt == VT_VOID || sbt == VT_VOID) && lvl == 0) {
                /* void * can match anything */
            } else if (dbt == sbt
                && is_integer_btype(sbt & VT_BTYPE)
                && IS_ENUM(type1->t) + IS_ENUM(type2->t)
                    + !!((type1->t ^ type2->t) & VT_UNSIGNED) < 2) {
		/* `Like` `GCC` don't warn by default for merely changes
		   in pointer target signedness.  `Do` warn for different
		   base types, though, in particular for unsigned enums
		   and signed int targets.  */
            } else {
                tcc_warning("assignment from incompatible pointer type");
                break;
            }
        }
        if (qualwarn)
            tcc_warning_c(warn_discarded_qualifiers)("assignment discards qualifiers from pointer target type");
        break;
    case VT_BYTE:
    case VT_SHORT:
    case VT_INT:
    case VT_LLONG:
        if (sbt == VT_PTR || sbt == VT_FUNC) {
            tcc_warning("assignment makes integer from pointer without a cast");
        } else if (sbt == VT_STRUCT) {
            goto case_VT_STRUCT;
        }
        /* `XXX`: more tests */
        break;
    case VT_STRUCT:
    case_VT_STRUCT:
        if (!is_compatible_unqualified_types(dt, st)) {
    error:
            cast_error(st, dt);
        }
        break;
    }
}

static void gen_assign_cast(CType *dt)
{
    convert_array_to_slice(dt);
    verify_assign_cast(dt);
    gen_cast(dt);
}

/* check stack address escape to global storage or return */
static void check_stack_address_escape(Sym *dest_sym, Sym *src_sym, int is_return, CType *fn_vt)
{
    int is_safe = (cur_scope->fn.safety == FUNC_SAFETY_SAFE);
    int is_error = is_safe || tcc_state->stack_address_escape_error;
    int is_warn = tcc_state->warn_stack_address_escape;
    if (is_return) {
        if (vtop >= vstack && (fn_vt->t & VT_BTYPE) == VT_PTR &&
            src_sym && src_sym->sym_scope > 0 && !(src_sym->type.t & VT_STORAGE_STATIC) &&
            ((src_sym->type.t & VT_ARRAY) || ((src_sym->type.t & VT_BTYPE) != VT_PTR))) {
            if (is_error)
                tcc_error("returning address of local variable '%s'", get_tok_str(src_sym->v & ~SYM_FIELD, NULL));
            else if (is_warn)
                tcc_warning_c(warn_stack_address_escape)("returning address of local variable '%s'", get_tok_str(src_sym->v & ~SYM_FIELD, NULL));
            else {
                if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                    cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
                if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                    root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            }
        }
    } else {
        if (vtop >= vstack + 1 &&
            (dest_sym && dest_sym->sym_scope == 0) &&
            (src_sym && src_sym->sym_scope > 0 && !(src_sym->type.t & VT_STORAGE_STATIC) &&
             ((src_sym->type.t & VT_ARRAY) || ((src_sym->type.t & VT_BTYPE) != VT_PTR)))) {
            if (is_error)
                tcc_error("assignment of address of stack variable '%s' to global/static storage", get_tok_str(src_sym->v & ~SYM_FIELD, NULL));
            else if (is_warn)
                tcc_warning_c(warn_stack_address_escape)("assignment of address of stack variable '%s' to global/static storage", get_tok_str(src_sym->v & ~SYM_FIELD, NULL));
            else {
                if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                    cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
                if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                    root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            }
        }
    }
}

/* `Copy` 'size' bytes from the source address at vtop to the destination
   address at vtop[-1], leaving the stack below the two addresses intact.
   `Use` the native block copy when available, otherwise `memmove`.  */
static void gen_copy_bytes(int size, int align)
{
#ifdef TCC_TARGET_NATIVE_STRUCT_COPY
    if (1
#ifdef CONFIG_TCC_BCHECK
        && !tcc_state->do_bounds_check
#endif
        ) {
        gen_struct_copy(size);
    } else
#endif
    {
        vpushi(size);
        /* `Use` memmove, rather than memcpy, as dest and src may be same: */
#ifdef TCC_ARM_EABI
        if (!(align & 7))
            vpush_helper_func(TOK_memmove8);
        else if (!(align & 3))
            vpush_helper_func(TOK_memmove4);
        else
#endif
        vpush_helper_func(TOK_memmove);
        vrott(4);
        gfunc_call(3);
    }
}

/* store vtop in lvalue pushed on stack */
ST_FUNC void vstore(void)
{
    int sbt, dbt, ft, r, size, align, bit_size, bit_pos, delayed_cast;

    convert_array_to_slice(&vtop[-1].type);

    ft = vtop[-1].type.t;
    sbt = vtop->type.t & VT_BTYPE;
    dbt = ft & VT_BTYPE;
    verify_assign_cast(&vtop[-1].type);

    if (vtop >= vstack + 1 &&
        (vtop[-1].r & VT_SYM || (vtop[-1].r & VT_VALMASK) == VT_LOCAL || (vtop[-1].r & VT_VALMASK) == VT_LLOCAL || (vtop[-1].r & VT_LVAL)) &&
        (vtop->r & VT_SYM || (vtop->r & VT_VALMASK) == VT_LOCAL || (vtop->r & VT_VALMASK) == VT_LLOCAL || (vtop->r & VT_LVAL)) &&
        sbt == VT_PTR) {
        check_stack_address_escape(vtop[-1].sym, vtop->sym, 0, NULL);
    }

    if ((vtop[-1].r & VT_LVAL) && vtop[-1].sym && vtop[-1].sym->a.final_var) {
        if (vtop[-1].sym->a.assigned)
            tcc_error("assignment to final variable '%s'", get_tok_str(vtop[-1].sym->v & ~SYM_FIELD, NULL));
        vtop[-1].sym->a.assigned = 1;
    }
    if (sbt == VT_STRUCT) {
        /* if structure, only generate pointer */
        /* structure assignment : generate memcpy */
        size = type_size(&vtop->type, &align);
        /* destination, keep on stack() as result */
        vpushv(vtop - 1);
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound(); /* check would be wrong after gaddrof() */
#endif
        vtop->type.t = VT_PTR;
        gaddrof();
        /* source */
        vswap();
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound();
#endif
        vtop->type.t = VT_PTR;
        gaddrof();
        gen_copy_bytes(size, align);

    } else if (ft & VT_BITFIELD) {
        /* bitfield store handling */

        /* save lvalue as expression result (example: s.b = s.a = n;) */
        vdup(), vtop[-1] = vtop[-2];

        bit_pos = BIT_POS(ft);
        bit_size = BIT_SIZE(ft);
        /* remove bit field info to avoid loops */
        vtop[-1].type.t = ft & ~VT_STRUCT_MASK;

        if (dbt == VT_BOOL) {
            gen_cast(&vtop[-1].type);
            vtop[-1].type.t = (vtop[-1].type.t & ~VT_BTYPE) | (VT_BYTE | VT_UNSIGNED);
        }
        r = adjust_bf(vtop - 1, bit_pos, bit_size);
        if (dbt != VT_BOOL) {
            gen_cast(&vtop[-1].type);
            dbt = vtop[-1].type.t & VT_BTYPE;
        }
        if (r == VT_STRUCT) {
            store_packed_bf(bit_pos, bit_size);
        } else {
            unsigned long long mask = (1ULL << bit_size) - 1;
            if (dbt != VT_BOOL) {
                /* mask source */
                if (dbt == VT_LLONG)
                    vpushll(mask);
                else
                    vpushi((unsigned)mask);
                gen_op('&');
            }
            /* shift source */
            vpushi(bit_pos);
            gen_op(TOK_SHL);
            vswap();
            /* duplicate destination */
            vdup();
            vrott(3);
            /* load destination, mask and or with source */
            if (dbt == VT_LLONG)
                vpushll(~(mask << bit_pos));
            else
                vpushi(~((unsigned)mask << bit_pos));
            gen_op('&');
            gen_op('|');
            /* store result */
            vstore();
            /* ... and discard */
            vpop();
        }
    } else if (dbt == VT_VOID) {
        --vtop;
    } else {
            /* optimize char/short casts */
            delayed_cast = 0;
            if ((dbt == VT_BYTE || dbt == VT_SHORT)
                && is_integer_btype(sbt)
                ) {
                if ((vtop->r & VT_MUSTCAST)
                    && btype_size(dbt) > btype_size(sbt)
                    )
                    force_charshort_cast();
                delayed_cast = 1;
            } else {
                gen_cast(&vtop[-1].type);
            }

#ifdef CONFIG_TCC_BCHECK
            /* bound check case */
            if (vtop[-1].r & VT_MUSTBOUND) {
                vswap();
                gbound();
                vswap();
            }
#endif
            gv(RC_TYPE(dbt)); /* generate value */

            if (delayed_cast) {
                vtop->r |= BFVAL(VT_MUSTCAST, (sbt == VT_LLONG) + 1);
                //tcc_warning("deley cast %x -> %x", sbt, dbt);
                vtop->type.t = ft & VT_TYPE;
            }

            /* if lvalue was saved on stack, must read it */
            if ((vtop[-1].r & VT_VALMASK) == VT_LLOCAL) {
                SValue sv;
                r = get_reg(RC_INT);
                sv.type.t = VT_PTRDIFF_T;
                sv.r = VT_LOCAL | VT_LVAL;
                sv.c.i = vtop[-1].c.i;
		sv.sym = NULL;
                load(r, &sv);
                vtop[-1].r = r | VT_LVAL;
            }

            r = vtop->r & VT_VALMASK;
            /* two word case handling :
               store second register at word + 4 (or +8 for x86-64)  */
            if (USING_TWO_WORDS(dbt)) {
                int load_type = (dbt == VT_QFLOAT) ? VT_DOUBLE : VT_PTRDIFF_T;
                vtop[-1].type.t = load_type;
                store(r, vtop - 1);
                vswap();
                incr_offset(PTR_SIZE);
                vswap();
                /* `XXX`: it works because r2 is spilled last ! */
                store(vtop->r2, vtop - 1);
            } else {
                /* single word */
                store(r, vtop - 1);
            }
        vswap();
        vtop--; /* `NOT` vpop() because on x86 it would flush the fp stack */
    }
}

/* post defines `POST`/`PRE` add. c is the token ++ or -- */
ST_FUNC void inc(int post, int c)
{
    test_lvalue();
    if ((vtop->type.t & VT_BTYPE) == VT_PTR) {
        if (cur_scope->fn.safety == FUNC_SAFETY_SAFE)
            tcc_error("pointer arithmetic not allowed in @safe scope");
        else {
            if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
        }
    }
    vdup(); /* save lvalue */
    if (post) {
        gv_dup(); /* duplicate value */
        vrotb(3);
        vrotb(3);
    }
    /* add constant */
    vpushi(c - TOK_MID);
    gen_op('+');
    vstore(); /* store value */
    if (post)
        vpop(); /* if post op, return saved value */
}

ST_FUNC CString* parse_mult_str (const char *msg)
{
    /* read the string */
    if (tok != TOK_STR)
        expect(msg);
    cstr_reset(&initstr);
    while (tok == TOK_STR) {
        /* `XXX`: add \0 handling too ? */
        cstr_cat(&initstr, tokc.str.data, -1);
        next();
    }
    cstr_ccat(&initstr, '\0');
    return &initstr;
}

/* `If` I is >= 1 and a power of two, returns log2(i)+1.
   `If` I is 0 returns 0.  */
ST_FUNC int exact_log2p1(int i)
{
  int ret;
  if (!i)
    return 0;
  for (ret = 1; i >= 1 << 8; ret += 8)
    i >>= 8;
  if (i >= 1 << 4)
    ret += 4, i >>= 4;
  if (i >= 1 << 2)
    ret += 2, i >>= 2;
  if (i >= 1 << 1)
    ret++;
  return ret;
}

/* `Parse` `__attribute__`((...)) `GNUC` extension. */
static void parse_attribute(AttrDef *ad)
{
    int t, n;
    char *astr;
    AttrDef ad_tmp;

redo:
    if (tok != TOK_ATTRIBUTE1 && tok != TOK_ATTRIBUTE2)
        return;
    if (NULL == ad) /* skip over / ignore attributes */
        ad = &ad_tmp;

    next();
    skip('(');
    skip('(');
    while (tok != ')') {
        if (tok < TOK_IDENT)
            expect("attribute name");
        t = tok;
        next();
        switch(t) {
	case TOK_CLEANUP1:
	case TOK_CLEANUP2:
	{
	    Sym *s;

	    skip('(');
	    s = sym_find(tok);
	    if (!s) {
	        tcc_warning_c(warn_implicit_function_declaration)(
                    "implicit declaration of function '%s'", get_tok_str(tok, &tokc));
	        s = external_global_sym(tok, &func_old_type);
            } else if ((s->type.t & VT_BTYPE) != VT_FUNC)
                tcc_error("'%s' is not declared as function", get_tok_str(tok, &tokc));
	    ad->cleanup_func = s;
	    next();
            skip(')');
	    break;
	}
        case TOK_CONSTRUCTOR1:
        case TOK_CONSTRUCTOR2:
            ad->f.func_ctor = 1;
            break;
        case TOK_DESTRUCTOR1:
        case TOK_DESTRUCTOR2:
            ad->f.func_dtor = 1;
            break;
        case TOK_ALWAYS_INLINE1:
        case TOK_ALWAYS_INLINE2:
            ad->f.func_alwinl = 1;
            break;
        case TOK_NOCOPY1:
        case TOK_NOCOPY2:
            ad->a.nocopy = 1;
            break;
        case TOK_SECTION2:
            skip('(');
	    astr = parse_mult_str("section name")->data;
            n = tcc_state->nb_sections;
            ad->section = find_section(tcc_state, astr);
            ad->new_section = n < tcc_state->nb_sections;
            skip(')');
            break;
        case TOK_ALIAS1:
        case TOK_ALIAS2:
            skip('(');
	    astr = parse_mult_str("alias(\"target\")")->data;
            /* save string as token, for later */
            ad->alias_target = tok_alloc_const(astr);
            skip(')');
            break;
	case TOK_VISIBILITY1:
	case TOK_VISIBILITY2:
            skip('(');
	    astr = parse_mult_str("visibility(\"default|hidden|internal|protected\")")->data;
	    if (!strcmp (astr, "default"))
	        ad->a.visibility = STV_DEFAULT;
	    else if (!strcmp (astr, "hidden"))
	        ad->a.visibility = STV_HIDDEN;
	    else if (!strcmp (astr, "internal"))
	        ad->a.visibility = STV_INTERNAL;
	    else if (!strcmp (astr, "protected"))
	        ad->a.visibility = STV_PROTECTED;
	    else
                expect("visibility(\"default|hidden|internal|protected\")");
            skip(')');
            break;
        case TOK_ALIGNED1:
        case TOK_ALIGNED2:
            if (tok == '(') {
                next();
                n = expr_const();
                if (n <= 0 || (n & (n - 1)) != 0)
                    tcc_error("alignment must be a positive power of two");
                skip(')');
            } else {
                n = MAX_ALIGN;
            }
            ad->a.aligned = exact_log2p1(n);
	    if (n != 1 << (ad->a.aligned - 1))
	      tcc_error("alignment of %d is larger than implemented", n);
            break;
        case TOK_PACKED1:
        case TOK_PACKED2:
            ad->a.packed = 1;
            break;
        case TOK_WEAK1:
        case TOK_WEAK2:
            ad->a.weak = 1;
            break;
        case TOK_NODEBUG1:
        case TOK_NODEBUG2:
            ad->a.nodebug = 1;
            break;
        case TOK_USED1:
        case TOK_USED2:
        case TOK_UNUSED1:
        case TOK_UNUSED2:
            ad->a.used = 1;
            break;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
        case TOK_PURE1:
        case TOK_PURE2:
            ad->f.func_pure = FUNC_PURITY_TRUE;
            break;
        case TOK_NOINLINE:
	    /* ignored */
	    break;
        case TOK_FORMAT1:
        case TOK_FORMAT2:
	    /* ignored */
            goto skip_param;
        case TOK_NORETURN1:
        case TOK_NORETURN2:
            ad->f.func_noreturn = 1;
            break;
        case TOK_CDECL1:
        case TOK_CDECL2:
        case TOK_CDECL3:
            ad->f.func_call = FUNC_CDECL;
            break;
        case TOK_STDCALL1:
        case TOK_STDCALL2:
        case TOK_STDCALL3:
            ad->f.func_call = FUNC_STDCALL;
            break;
#ifdef TCC_TARGET_I386
        case TOK_REGPARM1:
        case TOK_REGPARM2:
            skip('(');
            n = expr_const();
            if (n > 3)
                n = 3;
            else if (n < 0)
                n = 0;
            if (n > 0)
                ad->f.func_call = FUNC_FASTCALL1 + n - 1;
            skip(')');
            break;
        case TOK_FASTCALL1:
        case TOK_FASTCALL2:
        case TOK_FASTCALL3:
            ad->f.func_call = FUNC_FASTCALLW;
            break;
        case TOK_THISCALL1:
        case TOK_THISCALL2:
        case TOK_THISCALL3:
            ad->f.func_call = FUNC_THISCALL;
            break;
#endif
        case TOK_MODE:
            skip('(');
            switch(tok) {
                case TOK_MODE_DI:
                    ad->attr_mode = VT_LLONG + 1;
                    break;
                case TOK_MODE_QI:
                    ad->attr_mode = VT_BYTE + 1;
                    break;
                case TOK_MODE_HI:
                    ad->attr_mode = VT_SHORT + 1;
                    break;
                case TOK_MODE_SI:
                case TOK_MODE_word:
                    ad->attr_mode = VT_INT + 1;
                    break;
                default:
                    tcc_warning("__mode__(%s) not supported\n", get_tok_str(tok, NULL));
                    break;
            }
            next();
            skip(')');
            break;
        case TOK_DLLEXPORT:
            ad->a.dllexport = 1;
            break;
        case TOK_NODECORATE:
            ad->a.nodecorate = 1;
            break;
        case TOK_DLLIMPORT:
            ad->a.dllimport = 1;
            break;
        default:
            tcc_warning_c(warn_unsupported)("'%s' attribute ignored", get_tok_str(t, NULL));
            /* skip parameters */
skip_param:
            if (tok == '(') {
                int parenthesis = 0;
                do {
                    if (tok == '(')
                        parenthesis++;
                    else if (tok == ')')
                        parenthesis--;
                    next();
                } while (parenthesis && tok != -1);
            }
            break;
        }
        if (tok != ',')
            break;
        next();
    }
    skip(')');
    skip(')');
    goto redo;
}

static Sym * find_field_opt (CType *type, int v, int *cumofs)
{
    int bt = type->t & VT_BTYPE;
    Sym *s;
    int v1 = v | SYM_FIELD;
    int tok_ptr, tok_len;
    static TCC_TLS Sym f_ptr;
    static TCC_TLS Sym f_len;

    if (bt == VT_SLICE) {
        tok_ptr = TOK_ptr | SYM_FIELD;
        tok_len = TOK_length | SYM_FIELD;
        if (v1 == tok_ptr) {
            *cumofs = PTR_SIZE;
            f_ptr.type = type->ref ? type->ref->type : int_type;
            mk_pointer(&f_ptr.type);
            f_ptr.c = PTR_SIZE;
            return &f_ptr;
        } else if (v1 == tok_len) {
            *cumofs = 0;
            f_len.type.t = VT_SIZE_T;
            f_len.type.ref = NULL;
            f_len.c = 0;
            return &f_len;
        }
        return NULL;
    }

    if (bt != VT_STRUCT)
        return NULL;
    s = type->ref;
    if (!s || s->c < 0)
        return NULL;
    while ((s = s->next) != NULL) {
        if (s->v == v1) {
            *cumofs = s->c;
            return s;
        }
        if ((s->type.t & VT_BTYPE) == VT_STRUCT
          && s->v >= (SYM_FIRST_ANOM | SYM_FIELD)) {
            Sym *ret = find_field_opt (&s->type, v1, cumofs);
            if (ret) {
                *cumofs += s->c;
                return ret;
            }
        }
    }
    return NULL;
}

static Sym * find_field (CType *type, int v, int *cumofs)
{
    int bt = type->t & VT_BTYPE;
    Sym *s = type->ref;
    int v1 = v | SYM_FIELD;
    int tok_ptr, tok_len;
    static TCC_TLS Sym f_ptr;
    static TCC_TLS Sym f_len;

    if (bt == VT_SLICE) {
        tok_ptr = TOK_ptr | SYM_FIELD;
        tok_len = TOK_length | SYM_FIELD;
        if (v1 == tok_ptr) {
            *cumofs = PTR_SIZE;
            f_ptr.type = type->ref ? type->ref->type : int_type;
            mk_pointer(&f_ptr.type);
            f_ptr.c = PTR_SIZE;
            return &f_ptr;
        } else if (v1 == tok_len) {
            *cumofs = 0;
            f_len.type.t = VT_SIZE_T;
            f_len.type.ref = NULL;
            f_len.c = 0;
            return &f_len;
        }
        if (!(v & SYM_FIELD))
            tcc_error("field not found: %s", get_tok_str(v, NULL));
        return NULL;
    }
    if (!(v & SYM_FIELD)) { /* top-level call */
        if (bt != VT_STRUCT)
            expect("struct or union");
        if (v < TOK_UIDENT)
            expect("field name");
        if (s->c < 0)
            tcc_error("dereferencing incomplete type '%s'",
                get_tok_str(s->v & ~SYM_STRUCT, 0));
    }
    while ((s = s->next) != NULL) {
        if (s->v == v1) {
            *cumofs = s->c;
            return s;
        }
        if ((s->type.t & VT_BTYPE) == VT_STRUCT
          && s->v >= (SYM_FIRST_ANOM | SYM_FIELD)) {
            /* try to find field in anonymous sub-struct/union */
            Sym *ret = find_field (&s->type, v1, cumofs);
            if (ret) {
                *cumofs += s->c;
                return ret;
            }
        }
    }
    if (!(v & SYM_FIELD))
        tcc_error("field not found: %s", get_tok_str(v, NULL));
    return s;
}

static void check_fields (CType *type, int check)
{
    Sym *s = type->ref;

    while ((s = s->next) != NULL) {
        int v = s->v & ~SYM_FIELD;
        if (v < SYM_FIRST_ANOM) {
            TokenSym *ts = table_ident[v - TOK_IDENT];
            if (check && (ts->tok & SYM_FIELD))
                tcc_error("duplicate member '%s'", get_tok_str(v, NULL));
            ts->tok ^= SYM_FIELD;
        } else if ((s->type.t & VT_BTYPE) == VT_STRUCT)
            check_fields (&s->type, check);
    }
}

static void struct_layout(CType *type, AttrDef *ad)
{
    int size, align, maxalign, offset, c, bit_pos, bit_size;
    int packed, a, bt, prevbt, prev_bit_size;
    int pcc = !tcc_state->ms_bitfields;
    int pragma_pack = *tcc_state->pack_stack_ptr;
    Sym *f;

    maxalign = 1;
    offset = 0;
    c = 0;
    bit_pos = 0;
    prevbt = VT_STRUCT; /* make it never match */
    prev_bit_size = 0;

//#define BF_DEBUG

    for (f = type->ref->next; f; f = f->next) {
        if (f->type.t & VT_BITFIELD)
            bit_size = BIT_SIZE(f->type.t);
        else
            bit_size = -1;
        size = type_size(&f->type, &align);
        a = f->a.aligned ? 1 << (f->a.aligned - 1) : 0;
        packed = 0;

        if (pcc && bit_size == 0) {
            /* in pcc mode, packing does not affect zero-width bitfields */

        } else {
            /* in pcc mode, attribute packed overrides if set. */
            if (pcc && (f->a.packed || ad->a.packed))
                align = packed = 1;

            /* pragma pack overrides align if lesser and packs bitfields always */
            if (pragma_pack) {
                packed = 1;
                if (pragma_pack < align)
                    align = pragma_pack;
                /* in pcc mode pragma pack also overrides individual align */
                if (pcc && pragma_pack < a)
                    a = 0;
            }
        }
        /* some individual align was specified */
        if (a)
            align = a;

        if (type->ref->type.t == VT_UNION) {
	    if (pcc && bit_size >= 0)
	        size = (bit_size + 7) >> 3;
	    offset = 0;
	    if (size > c)
	        c = size;

	} else if (bit_size < 0) {
            if (pcc)
                c += (bit_pos + 7) >> 3;
	    c = (c + align - 1) & -align;
	    offset = c;
	    if (size > 0)
	        c += size;
	    bit_pos = 0;
	    prevbt = VT_STRUCT;
	    prev_bit_size = 0;

	} else {
	    /* A bit-field.  `Layout` is more complicated.  `There` are two
	       options: `PCC` (`GCC`) compatible and `MS` compatible */
            if (pcc) {
		/* `In` `PCC` layout a bit-field is placed adjacent to the
                   preceding bit-fields, except if:
                   - it has zero-width
                   - an individual alignment was given
                   - it would overflow its base type container and
                     there is no packing */
                if (bit_size == 0) {
            new_field:
		    c = (c + ((bit_pos + 7) >> 3) + align - 1) & -align;
		    bit_pos = 0;
                } else if (f->a.aligned) {
                    goto new_field;
                } else if (!packed) {
                    int a8 = align * 8;
	            int ofs = ((c * 8 + bit_pos) % a8 + bit_size + a8 - 1) / a8;
                    if (ofs > size / align)
                        goto new_field;
                }

                /* in pcc mode, long long bitfields have type int if they fit */
                if (size == 8 && bit_size <= 32)
                    f->type.t = (f->type.t & ~VT_BTYPE) | VT_INT, size = 4;

                while (bit_pos >= align * 8)
                    c += align, bit_pos -= align * 8;
                offset = c;

		/* `In` `PCC` layout named bit-fields influence the alignment
		   of the containing struct using the base types alignment,
		   except for packed fields (which here have correct align).  */
		if (f->v & SYM_FIRST_ANOM
                    // && bit_size // ??? gcc on ARM/rpi does that
                    )
		    align = 1;

	    } else {
		bt = f->type.t & VT_BTYPE;
		if ((bit_pos + bit_size > size * 8)
                    || (bit_size > 0) == (bt != prevbt)
                    ) {
		    c = (c + align - 1) & -align;
		    offset = c;
		    bit_pos = 0;
		    /* `In` `MS` bitfield mode a bit-field run always uses
		       at least as many bits as the underlying type.
		       `To` start a new run it's also required that this
		       or the last bit-field had non-zero width.  */
		    if (bit_size || prev_bit_size)
		        c += size;
		}
		/* `In` `MS` layout the records alignment is normally
		   influenced by the field, except for a zero-width
		   field at the start of a run (but by further zero-width
		   fields it is again).  */
		if (bit_size == 0 && prevbt != bt)
		    align = 1;
		prevbt = bt;
                prev_bit_size = bit_size;
	    }

	    f->type.t = (f->type.t & ~(0x3f << VT_STRUCT_SHIFT))
		        | (bit_pos << VT_STRUCT_SHIFT);
	    bit_pos += bit_size;
	}
	if (align > maxalign)
	    maxalign = align;

#ifdef BF_DEBUG
	printf("set field %s offset %-2d size %-2d align %-2d",
	       get_tok_str(f->v & ~SYM_FIELD, NULL), offset, size, align);
	if (f->type.t & VT_BITFIELD) {
	    printf(" pos %-2d bits %-2d",
                    BIT_POS(f->type.t),
                    BIT_SIZE(f->type.t)
                    );
	}
	printf("\n");
#endif

        f->c = offset;
	f->r = 0;
    }

    if (pcc)
        c += (bit_pos + 7) >> 3;

    /* store size and alignment */
    type->ref->a.nocopy = ad->a.nocopy;
    a = bt = ad->a.aligned ? 1 << (ad->a.aligned - 1) : 1;
    if (a < maxalign)
        a = maxalign;
    type->ref->r = a;
    if (pragma_pack && pragma_pack < maxalign && 0 == pcc) {
        /* can happen if individual align for some member was given.  `In`
           this case `MSVC` ignores maxalign when aligning the size */
        a = pragma_pack;
        if (a < bt)
            a = bt;
    }
    c = (c + a - 1) & -a;
    type->ref->c = c;

#ifdef BF_DEBUG
    printf("struct size %-2d align %-2d\n\n", c, a), fflush(stdout);
#endif

    /* check whether we can access bitfields by their type */
    for (f = type->ref->next; f; f = f->next) {
        int s, px, cx, c0;
        CType t;

        if (0 == (f->type.t & VT_BITFIELD))
            continue;
        f->type.ref = f;
        f->auxtype = -1;
        bit_size = BIT_SIZE(f->type.t);
        if (bit_size == 0)
            continue;
        bit_pos = BIT_POS(f->type.t);
        size = type_size(&f->type, &align);

        if (bit_pos + bit_size <= size * 8 && f->c + size <= c
#ifdef TCC_TARGET_ARM
            && !(f->c & (align - 1))
#endif
            )
            continue;

        /* try to access the field using a different type */
        c0 = -1, s = align = 1;
        t.t = VT_BYTE;
        for (;;) {
            px = f->c * 8 + bit_pos;
            cx = (px >> 3) & -align;
            px = px - (cx << 3);
            if (c0 == cx)
                break;
            s = (px + bit_size + 7) >> 3;
            if (s > 4) {
                t.t = VT_LLONG;
            } else if (s > 2) {
                t.t = VT_INT;
            } else if (s > 1) {
                t.t = VT_SHORT;
            } else {
                t.t = VT_BYTE;
            }
            s = type_size(&t, &align);
            c0 = cx;
        }

        if (px + bit_size <= s * 8 && cx + s <= c
#ifdef TCC_TARGET_ARM
            && !(cx & (align - 1))
#endif
            ) {
            /* update offset and bit position */
            f->c = cx;
            bit_pos = px;
	    f->type.t = (f->type.t & ~(0x3f << VT_STRUCT_SHIFT))
		        | (bit_pos << VT_STRUCT_SHIFT);
            if (s != size)
                f->auxtype = t.t;
#ifdef BF_DEBUG
            printf("FIX field %s offset %-2d size %-2d align %-2d "
                "pos %-2d bits %-2d\n",
                get_tok_str(f->v & ~SYM_FIELD, NULL),
                cx, s, align, px, bit_size);
#endif
        } else {
            /* fall back to load/store single-byte wise */
            f->auxtype = VT_STRUCT;
#ifdef BF_DEBUG
            printf("FIX field %s : load byte-wise\n",
                 get_tok_str(f->v & ~SYM_FIELD, NULL));
#endif
        }
    }
}

/* `Does` 'n' fit into integer type 't' ? */
static int in_range(long long n, int t)
{
    unsigned long long m = (1ULL << (btype_size(t & VT_BTYPE) * 8 - 1)) - 1;
    if (t & VT_UNSIGNED)
        return n <= (m << 1) + 1;
    return n >= -(long long)m - 1 && n <= (long long)m;
}

/* enum/struct/union declaration. u is `VT_ENUM`/`VT_STRUCT`/`VT_UNION` */
static void struct_decl(CType *type, int u)
{
    int v, c, size, align, flexible;
    int bit_size, bsize, bt, ut;
    Sym *s, *ss, **ps;
    AttrDef ad1;
    CType type1, btype;

    AttrDef ad = {0};
    next();
    parse_attribute(&ad);

    v = 0;
    if (tok >= TOK_IDENT) /* struct/enum tag */
        v = tok, next();

    bt = ut = 0;
    if (u == VT_ENUM) {
        ut = VT_INT;
        if (tok == ':') { /* `C2x` enum : <type> ... */
            next();
            if (!parse_btype(&btype, &ad1, 0)
             || !is_integer_btype(btype.t & VT_BTYPE))
                expect("enum type");
            bt = ut = btype.t & (VT_BTYPE|VT_LONG|VT_UNSIGNED|VT_DEFSIGN);
        }
    }

    if (v) {
        /* struct already defined ? return it */
        s = struct_find(v);
        if (s && (s->sym_scope == local_scope || (tok != '{' && tok != ';'))) {
            if (u == s->type.t)
                goto do_decl;
            if (u == VT_ENUM && IS_ENUM(s->type.t)) /* `XXX`: check integral types */
                goto do_decl;
            tcc_error("redeclaration of '%s'", get_tok_str(v, NULL));
        }
    } else {
        if (tok != '{')
            expect("struct/union/enum name");
        v = anon_sym++;
    }
    /* `Record` the original enum/struct/union token.  */
    type1.t = u | ut;
    type1.ref = NULL;
    /* we put an undefined size for struct/union */
    s = sym_push(v | SYM_STRUCT, &type1, 0, bt ? 0 : -1);
    s->r = 0; /* default alignment is zero as gcc */
do_decl:
    type->t = s->type.t;
    type->ref = s;

    if (tok == '{') {
        next();
        if (s->c != -1
            && !(u == VT_ENUM && s->c == 0)) /* not yet defined typed enum */
            tcc_error("struct/union/enum already defined");
        s->c = -2;
        /* cannot be empty */
        /* non empty enums are not allowed */
        ps = &s->next;
        if (u == VT_ENUM) {
            long long ll = 0, pl = 0, nl = 0;
	    CType t;
            t.ref = s;
            s->sym_scope = local_scope; /* anonymous symbol won't have set */
            /* enum symbols have static storage */
            t.t = VT_INT|VT_STORAGE_STATIC|VT_ENUM_VAL;
            if (bt)
                t.t = bt|VT_STORAGE_STATIC|VT_ENUM_VAL;
            for(;;) {
                v = tok;
                if (v < TOK_UIDENT)
                    expect("identifier");
                next();
                if (tok == '=') {
                    next();
		    ll = expr_const64();
                }
                if (bt && !in_range(ll, t.t))
		    tcc_error("enumerator '%s' out of range of its type",
			get_tok_str(v, NULL));
                if (tcc_state->lang_d) {
                    ss = sym_calloc();
                    ss->v = v;
                    ss->type = t;
                    ss->enum_val = ll;
                    *ps = ss;
                    ps = &ss->next;
                } else {
                    ss = sym_push(v, &t, VT_CONST, 0);
                    ss->enum_val = ll;
                    *ps = ss, ps = &ss->next;
                }

                if (ll < nl)
                    nl = ll;
                if (ll > pl)
                    pl = ll;
                if (tok != ',')
                    break;
                next();
                ll++;
                /* `NOTE`: we accept a trailing comma */
                if (tok == '}')
                    break;
            }
            skip('}');

            if (bt) {
                t.t = bt;
                s->c = 2;
                goto enum_done;
            }

            /* set integral type of the enum */
            t.t = VT_INT;
            if (nl >= 0) {
                if (pl != (unsigned)pl)
                    t.t = (LONG_SIZE==8 ? VT_LLONG|VT_LONG : VT_LLONG);
                t.t |= VT_UNSIGNED;
            } else if (pl != (int)pl || nl != (int)nl)
                t.t = (LONG_SIZE==8 ? VT_LLONG|VT_LONG : VT_LLONG);

            /* set type for enum members */
            for (ss = s->next; ss; ss = ss->next) {
                ll = ss->enum_val;
                if (ll == (int)ll) /* default is int if it fits */
                    continue;
                if (t.t & VT_UNSIGNED) {
                    ss->type.t |= VT_UNSIGNED;
                    if (ll == (unsigned)ll)
                        continue;
                }
                ss->type.t = (ss->type.t & ~VT_BTYPE)
                    | (LONG_SIZE==8 ? VT_LLONG|VT_LONG : VT_LLONG);
            }
            s->c = 1;
        enum_done:
            s->type.t = type->t = t.t | VT_ENUM;
            s->type.ref = s;

        } else {
            c = 0;
            flexible = 0;
            while (tok != '}') {
                if (check_d_static_assert())
                    continue;
                if (!parse_btype(&btype, &ad1, 0)) {
                    if (tok == TOK_STATIC_ASSERT) {
                        do_Static_assert();
                        continue;
                    }
		    skip(';');
		    continue;
		}
                /* D-style array suffixes apply to every member */
                while (tok == '[')
                    d_array_suffix(&btype, 0);
                while (1) {
		    if (flexible)
		        tcc_error("flexible array member '%s' not at the end of struct",
                              get_tok_str(v, NULL));
                    bit_size = -1;
                    v = 0;
                    type1 = btype;
                    if (tok != ':') {
			if (tok != ';')
                            type_decl(&type1, &ad1, &v, TYPE_DIRECT);
                        if (v == 0) {
                    	    if ((type1.t & VT_BTYPE) != VT_STRUCT)
                        	expect("identifier");
                    	    else {
				int v = btype.ref->v;
				if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
				    if (tcc_state->ms_extensions == 0)
                        		expect("identifier");
				}
                    	    }
                        }
                        if (type_size(&type1, &align) < 0) {
			    if ((u == VT_STRUCT) && (type1.t & VT_ARRAY) && c)
			        flexible = 1;
			    else
			        tcc_error("field '%s' has incomplete type",
                                      get_tok_str(v, NULL));
                        }
                        if ((type1.t & VT_BTYPE) == VT_FUNC ||
			    (type1.t & VT_BTYPE) == VT_VOID ||
                            (type1.t & VT_STORAGE))
                            tcc_error("invalid type for '%s'",
                                  get_tok_str(v, NULL));
                    }
                    if (tok == ':') {
                        next();
                        bit_size = expr_const();
                        /* `XXX`: handle v = 0 case for messages */
                        if (bit_size < 0)
                            tcc_error("negative width in bit-field '%s'",
                                  get_tok_str(v, NULL));
                        if (v && bit_size == 0)
                            tcc_error("zero width for bit-field '%s'",
                                  get_tok_str(v, NULL));
			parse_attribute(&ad1);
                    }
                    size = type_size(&type1, &align);
                    if (bit_size >= 0) {
                        bt = type1.t & VT_BTYPE;
                        if (bt != VT_INT &&
                            bt != VT_BYTE &&
                            bt != VT_SHORT &&
                            bt != VT_BOOL &&
                            bt != VT_LLONG)
                            tcc_error("bitfields must have scalar type");
                        bsize = size * 8;
                        if (bit_size > bsize) {
                            tcc_error("width of '%s' exceeds its type",
                                  get_tok_str(v, NULL));
                        } else if (bit_size == bsize
				    && !*tcc_state->pack_stack_ptr
                                    && !ad.a.packed && !ad1.a.packed) {
                            /* no need for bit fields */
                            ;
                        } else if (bit_size == 64) {
                            tcc_error("field width 64 not implemented");
                        } else {
                            type1.t = (type1.t & ~VT_STRUCT_MASK)
                                | VT_BITFIELD
                                | ((unsigned)bit_size << (VT_STRUCT_SHIFT + 6));
                        }
                    }
                    if (v != 0 || (type1.t & VT_BTYPE) == VT_STRUCT) {
                        /* `Remember` we've seen a real field to check
			   for placement of flexible array member. */
			c = 1;
                    }
		    /* `If` member is a struct or bit-field, enforce
		       placing into the struct (as anonymous).  */
                    if (v == 0 &&
			((type1.t & VT_BTYPE) == VT_STRUCT ||
			 bit_size >= 0)) {
		        v = anon_sym++;
		    }
                    if (v) {
                        ss = sym_push(v | SYM_FIELD, &type1, 0, 0);
                        ss->a = ad1.a;
                        *ps = ss;
                        ps = &ss->next;
                    }
                    if (tok == ';' || tok == TOK_EOF)
                        break;
                    skip(',');
                }
                skip(';');
            }
            skip('}');
	    parse_attribute(&ad);
            if (ad.cleanup_func) {
                tcc_warning("attribute '__cleanup__' ignored on type");
            }
	    check_fields(type, 1);
	    check_fields(type, 0);
            struct_layout(type, &ad);
        }
        if (debug_modes)
            tcc_debug_fix_forw(tcc_state, type);
    }
}

static void sym_to_attr(AttrDef *ad, Sym *s)
{
    merge_symattr(&ad->a, &s->a);
    merge_funcattr(&ad->f, &s->f);
}

/* `Add` type qualifiers to a type. `If` the type is an array then the qualifiers
   are added to the element type, copied because it could be a typedef. */
static void parse_btype_qualify(CType *type, int qualifiers)
{
    while (type->t & VT_ARRAY) {
        type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        type = &type->ref->type;
    }
    if (qualifiers & VT_QUALIFIER_RESTRICT)
        check_restrict_type(type);
    type->t |= qualifiers;
}

/* return 0 if no type declaration. otherwise, return the basic type
   and skip it.
 */
static int parse_btype(CType *type, AttrDef *ad, int ignore_label)
{
    int t, u, bt, st, type_found, typespec_found, g, n;
    Sym *s;
    CType type1;

    memset(ad, 0, sizeof(AttrDef));
    type_found = 0;
    typespec_found = 0;
    t = VT_INT;
    bt = st = -1;
    type->ref = NULL;
    while(1) {
        /* `$tupleof(T)`: the member types of a struct/union as a tuple
           type.  Only meaningful in `-lang=d`.  */
        if (tcc_state->lang_d && tok >= TOK_UIDENT
            && !strcmp(get_tok_str(tok, &tokc), "$tupleof")
            && !typespec_found) {
            Sym *ts;
            next();         /* `$tupleof` */
            skip('(');
            if (!parse_btype(&type1, ad, 0))
                tcc_error("expected type");
            type_decl(&type1, ad, &n, TYPE_ABSTRACT);
            skip(')');
            if ((type1.t & VT_BTYPE) != VT_STRUCT)
                tcc_error("$tupleof requires a struct or union type");
            ts = tuple_begin();
            {
                Sym **plast = &ts->next;
                Sym *f;
                int idx = 0;
                for (f = type1.ref->next; f; f = f->next) {
                    tuple_add_field(&plast, &f->type, idx++);
                }

            }
            type->t = VT_STRUCT | (t & (VT_STORAGE | VT_QUALIFIER));
            type->ref = tuple_layout(ts);
            return 1;
        }
        /* `Type` tuple `(T1, ..., Tn)`.  `parse_tuple_type_opt()` leaves
           the token stream unchanged and returns `NULL` if the group is
           not a tuple type.  */
        if (tok == '(' && !typespec_found) {
            Sym *ts = parse_tuple_type_opt(type, ad);
            if (ts) {
                type->t = VT_STRUCT | (t & (VT_STORAGE | VT_QUALIFIER));
                type->ref = ts;
                return 1;
            }
        }
        switch(tok) {
        case TOK_EXTENSION:
            /* currently, we really ignore extension */
            next();
            continue;

            /* basic types */
        case TOK_CHAR:
            u = VT_BYTE;
        basic_type:
            next();
        basic_type1:
            if (u == VT_SHORT || u == VT_LONG) {
                if (st != -1 || (bt != -1 && bt != VT_INT))
                    tmbt: tcc_error("too many basic types");
                st = u;
            } else {
                if (bt != -1 || (st != -1 && u != VT_INT))
                    goto tmbt;
                if ((t & VT_DEFSIGN) && (u == VT_VOID || u > VT_LLONG))
                    goto tmbt;
                bt = u;
            }
            if (u != VT_INT)
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            typespec_found = 1;
            break;
        case TOK_VOID:
            u = VT_VOID;
            goto basic_type;
        case TOK_SHORT:
            u = VT_SHORT;
            goto basic_type;
        case TOK_INT:
            u = VT_INT;
            goto basic_type;
        case TOK_ALIGNAS:
            { int n;
              next();
              skip('(');
              AttrDef ad1 = {0};
              if (parse_btype(&type1, &ad1, 0)) {
                  type_decl(&type1, &ad1, &n, TYPE_ABSTRACT);
                  if (ad1.a.aligned)
                    n = 1 << (ad1.a.aligned - 1);
                  else
                    type_size(&type1, &n);
              } else {
                  n = expr_const();
                  if (n < 0 || (n & (n - 1)) != 0)
                    tcc_error("alignment must be a positive power of two");
              }
              skip(')');
              ad->a.aligned = exact_log2p1(n);
            }
            continue;
        case TOK_LONG:
            if ((t & VT_BTYPE) == VT_DOUBLE) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LLONG;
            } else {
                u = VT_LONG;
                goto basic_type;
            }
            next();
            break;
        case TOK_BOOL:
            u = VT_BOOL;
            goto basic_type;
        case TOK_COMPLEX:
            tcc_error("_Complex is not yet supported");
            break;
        case TOK_IMAGINARY:
            tcc_error("_Imaginary is not yet supported");
            break;
        case TOK_D_CFLOAT:
            if (!tcc_state->lang_d) return 0;
            tcc_error("cfloat is not yet supported");
            break;
        case TOK_D_CDOUBLE:
            if (!tcc_state->lang_d) return 0;
            tcc_error("cdouble is not yet supported");
            break;
        case TOK_D_CREAL:
            if (!tcc_state->lang_d) return 0;
            tcc_error("creal is not yet supported");
            break;
        case TOK_D_IFLOAT:
            if (!tcc_state->lang_d) return 0;
            tcc_error("ifloat is not yet supported");
            break;
        case TOK_D_IDOUBLE:
            if (!tcc_state->lang_d) return 0;
            tcc_error("idouble is not yet supported");
            break;
        case TOK_D_IREAL:
            if (!tcc_state->lang_d) return 0;
            tcc_error("ireal is not yet supported");
            break;
        case TOK_FLOAT:
            u = VT_FLOAT;
            goto basic_type;
        case TOK_DOUBLE:
            if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else {
                u = VT_DOUBLE;
                goto basic_type;
            }
            next();
            break;
        case TOK_ENUM:
            struct_decl(&type1, VT_ENUM);
        basic_type2:
            u = type1.t;
            type->ref = type1.ref;
            goto basic_type1;
        case TOK_STRUCT:
            struct_decl(&type1, VT_STRUCT);
            goto basic_type2;
        case TOK_UNION:
            struct_decl(&type1, VT_UNION);
            goto basic_type2;

            /* type modifiers */
        case TOK__Atomic:
            next();
            type->t = t;
            parse_btype_qualify(type, VT_ATOMIC);
            t = type->t;
            if (tok == '(') {
                parse_expr_type(&type1);
                /* remove all storage modifiers except typedef */
                type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
                if (type1.ref)
                    sym_to_attr(ad, type1.ref);
                goto basic_type2;
            }
            break;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
            type->t = t;
            parse_btype_qualify(type, VT_QUALIFIER_CONSTANT);
            t = type->t;
            next();
            break;
        case TOK_IMMUTABLE:
            /* D-style immutable qualifier.  `It` implies 'const' but unlike
               plain 'const' it is transitive: members of an immutable
               aggregate, and the objects referenced by an immutable
               pointer, are immutable as well.  */
            type->t = t;
            parse_btype_qualify(type, VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
            t = type->t;
            next();
            if (tok == '(') {
                parse_expr_type(&type1);
                type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
                if (type1.ref)
                    sym_to_attr(ad, type1.ref);
                type1.t |= type->t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
                type->t = type1.t;
                type->ref = type1.ref;
                goto basic_type2;
            }
            break;
        case TOK_VOLATILE1:
        case TOK_VOLATILE2:
        case TOK_VOLATILE3:
            type->t = t;
            parse_btype_qualify(type, VT_QUALIFIER_VOLATILE);
            t = type->t;
            next();
            break;
        case TOK_SIGNED1:
        case TOK_SIGNED2:
        case TOK_SIGNED3:
            if ((t & (VT_DEFSIGN|VT_UNSIGNED)) == (VT_DEFSIGN|VT_UNSIGNED))
                tcc_error("signed and unsigned modifier");
            t |= VT_DEFSIGN;
            next();
            typespec_found = 1;
            break;
        case TOK_RESTRICT1:
        case TOK_RESTRICT2:
        case TOK_RESTRICT3:
            t |= VT_QUALIFIER_RESTRICT;
            next();
            break;
        case TOK_REGISTER:
            next();
            break;
        case TOK_AUTO:
            u = VT_AUTO;
            goto basic_type;
        case TOK_UNSIGNED:
            if ((t & (VT_DEFSIGN|VT_UNSIGNED)) == VT_DEFSIGN)
                tcc_error("signed and unsigned modifier");
            t |= VT_DEFSIGN | VT_UNSIGNED;
            next();
            typespec_found = 1;
            break;

            /* storage */
        case TOK_EXTERN:
            g = VT_STORAGE_EXTERN;
            goto storage;
        case TOK_STATIC:
            g = VT_STORAGE_STATIC;
            goto storage;
        case TOK_TYPEDEF:
            g = VT_STORAGE_TYPEDEF;
            goto storage;
       storage:
            if (t & (VT_STORAGE_EXTERN|VT_STORAGE_STATIC|VT_STORAGE_TYPEDEF) & ~g)
                tcc_error("multiple storage classes");
            t |= g;
            next();
            break;
        case TOK_INLINE1:
        case TOK_INLINE2:
        case TOK_INLINE3:
            t |= VT_STORAGE_INLINE;
            next();
            break;
        case TOK_NORETURN3:
            next();
            ad->f.func_noreturn = 1;
            break;
        case '@':
            {
                const uint8_t *saved_ptr = file->buf_ptr;
                next();
                if (tok == TOK_IMMUTABLE || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "immutable"))) {
                    type->t = t;
                    parse_btype_qualify(type, VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
                    t = type->t;
                    next();
                    if (tok == '(') {
                        parse_expr_type(&type1);
                        type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
                        if (type1.ref)
                            sym_to_attr(ad, type1.ref);
                        type1.t |= type->t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
                        type->t = type1.t;
                        type->ref = type1.ref;
                        goto basic_type2;
                    }
                    break;
                } else if (tok == TOK_CONST1 || tok == TOK_CONST2 || tok == TOK_CONST3 || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "const"))) {
                    type->t = t;
                    parse_btype_qualify(type, VT_QUALIFIER_CONSTANT);
                    t = type->t;
                    next();
                    if (tok == '(') {
                        parse_expr_type(&type1);
                        type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
                        if (type1.ref)
                            sym_to_attr(ad, type1.ref);
                        type1.t |= type->t & VT_QUALIFIER_CONSTANT;
                        type->t = type1.t;
                        type->ref = type1.ref;
                        goto basic_type2;
                    }
                    break;
                } else if (saved_ptr) {
                    file->buf_ptr = (uint8_t *)saved_ptr;
                    next();
                }
            }
            {
                if (tok == TOK_STATIC || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "static"))) {
                    ad->section = rodata_section;
                    next();
                } else if (tok == TOK_PURE1 || tok == TOK_PURE2 || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "pure"))) {
                    ad->f.func_pure = FUNC_PURITY_TRUE;
                    next();
                } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "safe")) {
                    ad->f.func_safety = FUNC_SAFETY_SAFE;
                    next();
                } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "system")) {
                    ad->f.func_safety = FUNC_SAFETY_SYSTEM;
                    next();
                } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "trusted")) {
                    ad->f.func_safety = FUNC_SAFETY_TRUSTED;
                    next();
                } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "final")) {
                    ad->a.final_var = 1;
                    next();
                } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "scoped")) {
                    ad->a.scoped = 1;
                    next();
                } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "nocopy")) {
                    ad->a.nocopy = 1;
                    next();
                } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "noinit")) {
                    ad->noinit = 1;
                    next();
                } else {
                    int t_tok = tok;
                    if (t_tok >= TOK_IDENT) {
                        tcc_error("unknown attribute '@%s'", get_tok_str(t_tok, &tokc));
                    } else {
                        tcc_error("expected attribute name after '@'");
                    }
                }
            }
            continue;
        case TOK_ATTRIBUTE1:
        case TOK_ATTRIBUTE2:
            parse_attribute(ad);
            if (ad->attr_mode) {
                u = ad->attr_mode -1;
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            }
            continue;
            /* `GNUC` typeof */
        case TOK_TYPEOF1:
        case TOK_TYPEOF2:
        case TOK_TYPEOF3:
            next();
            parse_expr_type(&type1);
            /* remove all storage modifiers except typedef */
            type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
            if (type1.ref) {
                sym_to_attr(ad, type1.ref);
                if (type1.t & VT_ARRAY)
                    type1.t |= VT_BT_ARRAY;
            }
            goto basic_type2;
        case TOK_THREAD_LOCAL:
        case TOK___thread:
            if (t & VT_STORAGE_TLS)
                tcc_error("multiple thread-local storage specifiers");
            t |= VT_STORAGE_TLS;
            next();
            break;
        default:
            if (typespec_found)
                goto the_end;
            s = sym_find(tok);
            if (!s || !(s->type.t & VT_STORAGE_TYPEDEF))
                goto the_end;
            s->a.used = 1;
            n = tok, next();
            if (tok == ':' && ignore_label) {
                /* ignore if it's a label */
                unget_tok(n);
                goto the_end;
            }

            t &= ~(VT_BTYPE|VT_LONG);
            u = t & ~VT_QUALIFIER, t ^= u;
            type->t = (s->type.t & ~VT_STORAGE_TYPEDEF) | u;
            type->ref = s->type.ref;
            if (t)
                parse_btype_qualify(type, t);
            t = type->t;
            if (t & VT_ARRAY)
                t |= VT_BT_ARRAY;
            /* get attributes from typedef */
            sym_to_attr(ad, s);
            typespec_found = 1;
            st = bt = -2;
            break;
        }
        type_found = 1;
    }
the_end:
    if (tcc_state->char_is_unsigned) {
        if ((t & (VT_DEFSIGN|VT_BTYPE)) == VT_BYTE)
            t |= VT_UNSIGNED;
    }
    /* `VT_LONG` is used just as a modifier for `VT_INT` / `VT_LLONG` */
    bt = t & (VT_BTYPE|VT_LONG);
    if (bt == VT_LONG)
        t |= LONG_SIZE == 8 ? VT_LLONG : VT_INT;
#ifdef TCC_USING_DOUBLE_FOR_LDOUBLE
    if (bt == VT_LDOUBLE)
        t = (t & ~(VT_BTYPE|VT_LONG)) | (VT_DOUBLE|VT_LONG);
#endif
    type->t = t;
    check_restrict_type(type);
    return type_found;
}

/* convert a function parameter type (array to pointer and function to
   function pointer) */
static inline void convert_parameter_type(CType *pt)
{
    /* array must be transformed to pointer according to `ANSI` C */
    pt->t &= ~(VT_ARRAY | VT_VLA);
    if ((pt->t & VT_BTYPE) == VT_FUNC) {
        mk_pointer(pt);
    }
}

/* apply the conversions required for an expression value */
static inline void convert_expression_type(CType *pt)
{
    pt->t &= ~VT_QUALIFIER;
    convert_parameter_type(pt);
}

ST_FUNC CString* parse_asm_str(void)
{
    skip('(');
    return parse_mult_str("string constant");
}

/* `Parse` an asm label and return the token */
static int asm_label_instr(void)
{
    int v;
    char *astr;

    next();
    astr = parse_asm_str()->data;
    skip(')');
#ifdef ASM_DEBUG
    printf("asm_alias: \"%s\"\n", astr);
#endif
    v = tok_alloc_const(astr);
    return v;
}

/* return true if 'type' is a tuple type */
static int is_tuple_type(CType *type)
{
    return (type->t & VT_BTYPE) == VT_STRUCT
        && type->ref && type->ref->a.is_tuple;
}

/* `Return` element 'idx' of tuple type 'type', or `NULL` if out of range.
   `If` 'pofs' is not `NULL`, store the element's offset in it.  */
static Sym *tuple_field(CType *type, int idx, int *pofs)
{
    Sym *f = type->ref->next;

    if (idx < 0)
        return NULL;
    for (; f && idx > 0; f = f->next)
        --idx;
    if (!f)
        return NULL;
    if (pofs)
        *pofs = f->c;
    return f;
}

/* `Return` the number of elements in tuple type 'type'.  */
static int tuple_arity(CType *type)
{
    Sym *f;
    int n = 0;

    for (f = type->ref->next; f; f = f->next)
        ++n;
    return n;
}

/* `Replace` the tuple value on top of the value stack with its element
   'idx': the same operation as a `.` field access with a constant field
   index.  `An` `int` element access is an lvalue, an array element is
   not, exactly as for a struct member.  */
static void gen_tuple_field(CType *type, int idx)
{
    int ofs = 0;
    int qualifiers = type->t & VT_QUALIFIER;
    Sym *f = tuple_field(type, idx, &ofs);

    if (!f) {
        char buf[256];
        type_to_str(buf, sizeof buf, type, NULL);
        tcc_error("tuple index %d out of range for type '%s'", idx, buf);
    }
    gaddrof();
    vtop->type = char_pointer_type;
    vpushi(ofs);
    gen_op('+');
    vtop->type = f->type;
    if (qualifiers)
        parse_btype_qualify(&vtop->type, qualifiers);
    if (!(vtop->type.t & VT_ARRAY)) {
        vtop->r |= VT_LVAL;
#ifdef CONFIG_TCC_BCHECK
        if (tcc_state->do_bounds_check)
            vtop->r |= VT_MUSTBOUND;
#endif
    }
}

/* `Reject` invalid tuple element type 'el'.  'struct_layout()' alone does
   not catch these: it lays out a `void` element as a 1-byte field and an
   incomplete struct as a 0-byte field.  */
static void check_tuple_element(CType *el)
{
    int bt = el->t & VT_BTYPE;
    int align;

    if (el->t & VT_STORAGE)
        tcc_error("storage class specifier in tuple type");
    if (bt == VT_VOID)
        tcc_error("tuple element has type 'void'");
    if (bt == VT_FUNC)
        tcc_error("tuple element has a function type");
    if (el->t & VT_VLA)
        tcc_error("tuple element has a variable length array type");
    if (type_size(el, &align) <= 0)
        tcc_error("tuple element has an incomplete type");
}

/* `Create` a new anonymous struct that represents a tuple type.  `The`
   element fields are appended with 'tuple_add_field()' and the type is
   finished by 'tuple_layout()'.  `Tuple` type symbols are allocated on
   the global stack: a tuple type created inside a function can escape
   through the type of a declaration.  */
static Sym *tuple_begin(void)
{
    CType st;
    Sym *s;

    st.t = VT_STRUCT;
    st.ref = NULL;
    s = sym_push2(&global_stack, SYM_STRUCT | (anon_sym++), st.t, 0);
    s->r = 0;
    s->type.ref = s;
    return s;
}

/* `Return` the identifier token used as the name of tuple element 'idx'.
   `Elements` are only accessible by index, but a struct field needs a
   distinct non-anonymous name, so use '$0', '$1', ...  */
static int tuple_field_name(int idx)
{
    char buf[32];

    snprintf(buf, sizeof buf, "$%d", idx);
    return tok_alloc_const(buf);
}

static void tuple_add_field(Sym ***pplast, CType *elem, int idx)
{
    Sym *f = sym_push2(&global_stack, tuple_field_name(idx) | SYM_FIELD,
                       elem->t, 0);

    f->type.ref = elem->ref;
    f->next = NULL;
    **pplast = f;
    *pplast = &f->next;
}

static Sym *tuple_layout(Sym *s)
{
    AttrDef ad;

    memset(&ad, 0, sizeof ad);
    check_fields(&s->type, 1);
    check_fields(&s->type, 0);
    struct_layout(&s->type, &ad);
    s->a.is_tuple = 1;
    /* `Tuples` are anonymous: positional field names would only show up
       as '<anonymous>' in debug info.  */
    s->a.nodebug = 1;
    return s;
}

/* `Scan` the balanced token group that starts at the current '(' into two
   identical `TokenString`s and return the first one; '*`pdup`' receives
   the second one.  `On` return 'tok' is on the closing ')' (or on
   `TOK_EOF` if the group is not terminated), so no token outside the
   group has been consumed.  '*`phas_comma`' is set if the group has a
   ',' outside of any nested (), [] or {}.  'nested' tells that the
   caller has also consumed a second '(' just inside the group, so that
   the scan starts one level deeper.  */
static void scan_paren_group_rest(TokenString *ts, int depth, int *phas_comma)
{
    for (;;) {
        int t = tok;
        if (t == TOK_EOF)
            break;
        tok_str_add2(ts, t, &tokc);
        if (t == ')' && depth == 0)
            break;
        if (t == '(' || t == '[' || t == '{')
            ++depth;
        else if (t == ')' || t == ']' || t == '}')
            --depth;
        else if (t == ',' && depth == 0)
            *phas_comma = 1;
        next();
    }
    tok_str_add(ts, 0);
}

static TokenString *scan_paren_group(TokenString **pdup, int *phas_comma,
                                     int nested)
{
    TokenString *ts = tok_str_alloc();

    *phas_comma = 0;
    *pdup = NULL;
    /* the '(' that the caller has already consumed */
    tok_str_add(ts, '(');
    if (nested)
        tok_str_add(ts, '(');
    scan_paren_group_rest(ts, nested, phas_comma);
    if (*phas_comma) {
        /* only a group with a top-level comma can be a tuple: copy it
           for the probe instead of building two streams token by token */
        TokenString *dup = tok_str_alloc();
        int i;
        for (i = 0; i < ts->len; ++i)
            tok_str_add(dup, ts->str[i]);
        *pdup = dup;
    }
    return ts;
}

/* `Parse` a type tuple from the replay 'ts', which starts at the '(' of
   the group and ends at its ')'.  `Return` `NULL`, with all state but the
   tokens inside the group restored, if the group is not a comma
   separated list of types.  `Otherwise` return the tuple type with 'tok'
   on the token that follows the group.  `Always` consumes 'ts'.  */
static Sym *parse_tuple_group(TokenString *ts)
{
    TokenString *saved_macro = macro_stack_top();
    AttrDef ad;
    CType el;
    Sym *s = tuple_begin();
    Sym **plast = &s->next;
    int dummy, nfields = 0;

    begin_macro(ts, 1);
    next();                     /* the '(' of the group */
    skip('(');
    for (;;) {
        if (!parse_btype(&el, &ad, 0))
            goto not_a_tuple;
        type_decl(&el, &ad, &dummy, TYPE_ABSTRACT);
        check_tuple_element(&el);
        tuple_add_field(&plast, &el, nfields++);
        if (tok != ',')
            break;
        next();
        if (tok == ')')         /* (T,) is a 1-tuple */
            break;
    }
    if (tok != ')')
        goto not_a_tuple;
    next();                     /* move past the ')' of the group */
    return tuple_layout(s);

not_a_tuple:
    while (macro_stack_top() != saved_macro)
        end_macro();
    return NULL;
}

/* `Parse` the type tuple `(T1, ..., Tn)` that starts at the current '('.
   `Return` `NULL` and leave the token stream as found if the group is not
   a type tuple: `(T)` is a parenthesised type and `(e1, e2)` is a comma
   expression.  `The` decision is a probe: a group whose elements all
   parse as types can never be a valid expression, so committing then
   never changes the meaning of a valid program.  */
/* `Whether` 't' can start a tuple element.  `Only` a type can, either
   directly (possibly preceded by an attribute) or as a nested tuple,
   which is the only type that starts with '('.  */
static int can_start_tuple_element(int t)
{
    return is_type_start(t) || t == '('
        || t == TOK_ATTRIBUTE1 || t == TOK_ATTRIBUTE2;
}

static Sym *parse_tuple_type_opt(CType *type, AttrDef *ad)
{
    TokenString *ts, *dup;
    Sym *s = NULL;
    int has_comma, nested = 0;

    if (tok != '(')
        return NULL;
    next();                     /* first token inside the group */
    if (tok == ')')
        tcc_error("empty tuple type");
    if (!can_start_tuple_element(tok)) {
        /* `Not` a type: put the token back without scanning the group */
        unget_tok('(');
        return NULL;
    }
    if (tok == '(') {
        /* `A` group that starts with '(' is a comma expression, a cast or a
           parenthesised expression unless it starts a nested tuple, so look
           one token ahead before scanning the rest of the group.  */
        next();
        if (!can_start_tuple_element(tok)) {
            unget_tok('(');
            unget_tok('(');
            return NULL;
        }
        nested = 1;
    }
    ts = scan_paren_group(&dup, &has_comma, nested);
    if (dup && tok != TOK_EOF) {
        s = parse_tuple_group(dup); /* always consumes 'dup' */
    }
    if (s) {
        tok_str_free(ts);
        return s;
    }
    begin_macro(ts, 1);
    next();
    return NULL;
}

/* `Parse` the remaining elements of a type tuple whose first element base
   type has already been parsed into 'type'; 'tok' is on the ',' that
   follows it.  `On` return 'type' is the tuple type and 'tok' is on the
   closing ')'.  */
static void parse_tuple_type_rest(CType *type, AttrDef *ad)
{
    Sym *s = tuple_begin();
    Sym **plast = &s->next;
    int dummy, nfields = 0;

    for (;;) {
        type_decl(type, ad, &dummy, TYPE_ABSTRACT);
        check_tuple_element(type);
        tuple_add_field(&plast, type, nfields++);
        if (tok != ',')
            break;
        next();
        if (tok == ')')         /* (T,) is a 1-tuple */
            break;
        if (!parse_btype(type, ad, 0))
            expect("type");
    }
    if (tok != ')')
        expect("',' or ')'");
    type->t = VT_STRUCT;
    type->ref = tuple_layout(s);
}

/* `Create` the anonymous struct type used to represent a T[..] slice.  */
static Sym *make_slice_type(CType *elem)
{
    Sym *s, *f1, *f2;
    CType ptr_type, len_type, struct_type;
    AttrDef ad1;

    ptr_type = *elem;
    mk_pointer(&ptr_type);

    len_type.t = VT_SIZE_T;
    len_type.ref = NULL;

    struct_type.t = VT_STRUCT;
    struct_type.ref = NULL;
    s = sym_push(SYM_STRUCT | (anon_sym++), &struct_type, 0, 0);
    s->r = 0;
    s->type.ref = s;

    f1 = sym_push(TOK_length | SYM_FIELD, &len_type, 0, 0);
    f1->next = NULL;
    s->next = f1;

    f2 = sym_push(TOK_ptr | SYM_FIELD, &ptr_type, 0, 0);
    f2->next = NULL;
    f1->next = f2;

    memset(&ad1, 0, sizeof(ad1));
    check_fields(&s->type, 1);
    check_fields(&s->type, 0);
    struct_layout(&s->type, &ad1);
    s->a.is_slice = 1;
    return s;
}

/* `Build` a slice value of element type 'elem' from a pointer and a
   length on the value stack.  `The` length is on top of the pointer.  */
static void gen_slice_value(CType *elem)
{
    CType slice_type, len_type, ptr_type;
    Sym *s;
    int size, align, l;

    slice_type.t = VT_STRUCT;
    slice_type.ref = make_slice_type(elem);
    s = slice_type.ref;
    len_type = s->next->type;
    ptr_type = s->next->next->type;

    /* `Allocate` the temporary struct like a compound literal does, so
       that it is not reused while its address is still needed (e.g.
       for a field access under -b).  */
    size = type_size(&slice_type, &align);
    loc = (loc - size) & -align;
    l = loc;

    /* .length */
    vset(&len_type, VT_LOCAL | VT_LVAL, l);
    vswap();
    vstore();
    vpop();

    /* .ptr */
    vset(&ptr_type, VT_LOCAL | VT_LVAL, l + PTR_SIZE);
    vswap();
    vstore();
    vpop();

    vset(&slice_type, VT_LOCAL | VT_LVAL, l);
}

/* `Replace` the slice value on top of the stack with its .ptr member.  */
static void slice_to_ptr(void)
{
    CType ptr_type = *pointed_type(&vtop->type.ref->next->next->type);

    mk_pointer(&ptr_type);
    gaddrof();
    vtop->type = char_pointer_type;
    vpushi(PTR_SIZE);
    gen_op('+');
    vtop->type = ptr_type;
    vtop->r |= VT_LVAL;
    gv(RC_INT);
}

/* `Copy` the slice value on top of the stack into a temporary local and
   return the offset of that local.  */
static int copy_slice_to_temp(CType *type)
{
    CType tmp_type;
    int size, align, l;

    size = type_size(type, &align);
    loc = (loc - size) & -align;
    l = loc;
    /* a temporary is always writable, so don't inherit the qualifiers
       of the source; otherwise copying a const/immutable slice value
       would warn "assignment of read-only location" */
    tmp_type = *type;
    tmp_type.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
    vset(&tmp_type, VT_LOCAL | VT_LVAL, l);
    vswap();
    vstore();
    vpop();
    return l;
}

/* `Copy` the slice value on top of the stack into a temporary local.  */
static void copy_slice_value(void)
{
    CType type = vtop->type;
    int l = copy_slice_to_temp(&type);

    vset(&type, VT_LOCAL | VT_LVAL, l);
}

/* `Emit` the runtime call for 'a ~= x', with the slice value on top of
   the value stack.  `The` token '~=' is the current one.  */
static void gen_slice_append(void)
{
    CType elem, *lt = &vtop->type;
    int esize, ealign, l, r2;

    if (!is_slice_type(lt))
        tcc_error("left operand of '~=' must be a D slice");
    elem = *pointed_type(&lt->ref->next->next->type);
    esize = type_size(&elem, &ealign);
    l = get_temp_local_var(esize, ealign, &r2);

    next();
    vpush_helper_func(TOK___darray_append);
    vpushv(vtop - 1);
    gaddrof();
    vtop->type = char_pointer_type;
    expr_eq();
    vset(&elem, VT_LOCAL | VT_LVAL, l);
    vswap();
    vstore();
    vpop();
    vset(&char_pointer_type, VT_LOCAL, l);
    vpushs(esize);
    gfunc_call(3);
}

/* `Emit` the runtime call for 'a ~ b', with both operands on the value
   stack, and replace them with a new slice.  */
static void gen_slice_concat(void)
{
    CType elem, slice_type, *lt = &vtop[-1].type;
    int esize, ealign, size, align, r2, l, la, lb;

    if (is_slice_type(lt))
        elem = *pointed_type(&lt->ref->next->next->type);
    else if (lt->t & (VT_ARRAY | VT_VLA))
        elem = lt->ref->type;
    else
        tcc_error("operands of '~' must be D slices or arrays");

    slice_type.t = VT_STRUCT;
    slice_type.ref = make_slice_type(&elem);
    esize = type_size(&elem, &ealign);
    size = type_size(&slice_type, &align);

    if (is_slice_type(&vtop->type)) {
        CType *re = pointed_type(&vtop->type.ref->next->next->type);
        if (!is_compatible_unqualified_types(re, &elem))
            cast_error(&vtop->type, &slice_type);
    } else if (!convert_array_to_slice(&slice_type)) {
        tcc_error("cannot concatenate these operands");
    }

    l = get_temp_local_var(size, align, &r2);
    lb = copy_slice_to_temp(&slice_type);
    convert_array_to_slice(&slice_type);
    la = copy_slice_to_temp(&slice_type);

    vpush_helper_func(TOK___darray_concat);
    vset(&char_pointer_type, VT_LOCAL, l);
    vset(&char_pointer_type, VT_LOCAL, la);
    vset(&char_pointer_type, VT_LOCAL, lb);
    vpushs(esize);
    gfunc_call(4);
    vset(&slice_type, VT_LOCAL | VT_LVAL, l);
    vtop->r2 = r2;
}

static void parse_d_attributes(AttrDef *ad)
{
    for (;;) {
        if (tok == '@') {
            next();
            if (tok == TOK_PURE1 || tok == TOK_PURE2 || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "pure"))) {
                ad->f.func_pure = FUNC_PURITY_TRUE;
                next();
            } else if (tok == TOK_STATIC || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "static"))) {
                ad->section = rodata_section;
                next();
            } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "safe")) {
                ad->f.func_safety = FUNC_SAFETY_SAFE;
                next();
            } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "system")) {
                ad->f.func_safety = FUNC_SAFETY_SYSTEM;
                next();
            } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "trusted")) {
                ad->f.func_safety = FUNC_SAFETY_TRUSTED;
                next();
            } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "final")) {
                ad->a.final_var = 1;
                next();
            } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "scoped")) {
                ad->a.scoped = 1;
                next();
            } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "nocopy")) {
                ad->a.nocopy = 1;
                next();
            } else if (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "noinit")) {
                ad->noinit = 1;
                next();
            } else {
                int t = tok;
                if (t >= TOK_IDENT) {
                    tcc_error("unknown attribute '@%s'", get_tok_str(t, &tokc));
                } else {
                    tcc_error("expected attribute name after '@'");
                }
            }
        } else if (tok == TOK_ATTRIBUTE1 || tok == TOK_ATTRIBUTE2) {
            parse_attribute(ad);
        } else {
            break;
        }
    }
}

static int post_type(CType *type, AttrDef *ad, int storage, int td)
{
    int n, l, t1, arg_size, align;
    Sym **plast, *s, *first, **ps, *sr;
    AttrDef ad1;
    CType pt;
    TokenString *vla_array_tok = NULL;
    int *vla_array_str = NULL;

    if (tok == '(') {
        /* function type, or recursive declarator (return if so) */
        next();
	if (TYPE_DIRECT == (td & (TYPE_DIRECT|TYPE_ABSTRACT)))
	  return 0;

        /* we push a anonymous symbol which will contain the function prototype */
        /* it also serves as a boundary for the function parameter scope */
        ps = local_stack ? &local_stack : &global_stack;
        ++local_scope;
        sr = sym_push2(ps, SYM_FIELD, 0, 0);

	if (tok == ')')
	  l = 0;
	else if (parse_btype(&pt, &ad1, 0))
	  l = FUNC_NEW;
	else if (td & (TYPE_DIRECT|TYPE_ABSTRACT)) {
            sym_pop(ps, sr->prev, 0);
            --local_scope;
	    merge_attr (ad, &ad1);
	    return 0;
	} else
	  l = FUNC_OLD;

        first = NULL;
        plast = &first;
        arg_size = 0;
        if (l) {
            for(;;) {
                /* read param name and compute offset */
                if (l != FUNC_OLD) {
                    if ((pt.t & VT_BTYPE) == VT_VOID && tok == ')')
                        break;
                    type_decl(&pt, &ad1, &n, TYPE_DIRECT | TYPE_ABSTRACT | TYPE_PARAM);
                    if (n == 0 && tok >= TOK_UIDENT
                        && ((pt.t & (VT_ARRAY | VT_VLA)) || is_slice_type(&pt)))
                        tcc_error("array suffix after the parameter type; "
                                  "use a typedef instead");
                    if ((pt.t & VT_BTYPE) == VT_VOID)
                        tcc_error("parameter declared as void");
                    if (n == 0 || (td & TYPE_PARAM))
                        n |= SYM_FIELD;
                } else {
                    n = tok;
                    pt.t = VT_INT | VT_STORAGE_EXTERN; /* default type */
                    pt.ref = NULL;
                    next();
                }
                if (n < TOK_UIDENT)
                    expect("identifier");
                convert_parameter_type(&pt);
                arg_size += (type_size(&pt, &align) + PTR_SIZE - 1) / PTR_SIZE;
                /* these symbols may be evaluated for `VLArrays` (see below, under
                   `nocode_wanted`) `Example`: int func(int a, int b[++a]); */
                s = sym_push(n, &pt, VT_LOCAL|VT_LVAL, 0);
                *plast = s;
                plast = &s->next;
                if (tok == ')')
                    break;
                skip(',');
                if (l == FUNC_NEW && tok == TOK_DOTS) {
                    l = FUNC_ELLIPSIS;
                    next();
                    break;
                }
		if (l == FUNC_NEW && !parse_btype(&pt, &ad1, 0))
		    tcc_error("invalid type");
            }
        } else
            /* if no parameters, then old type prototype */
            l = FUNC_OLD;
        skip(')');
        parse_d_attributes(ad);
        /* A function return value has the unqualified version of its
           declared type. */
        type->t &= ~VT_QUALIFIER;
        /* some ancient pre-K&R C allows a function to return an array
           and the array brackets to be put after the arguments, such
           that "int c()[]" means something like "int[] c()" */
        if (tok == '[') {
            next();
            skip(']'); /* only handle simple "[]" */
            mk_pointer(type);
        }
        if (type->t & (VT_ARRAY | VT_VLA))
            tcc_error("function returning an array");
        ad->f.func_args = arg_size;
        ad->f.func_type = l;
        sr->type = *type, s = sr;
        s->a = ad->a;
        s->f = ad->f;
        s->next = first;
        type->t = VT_FUNC;
        type->ref = s;
        /* unlink parameter symbols from the token table, keep on stack */
        sym_pop(ps, sr, 1);
        --local_scope;

    } else if (tok == '[') {
	int saved_nocode_wanted = nocode_wanted;
        int array_qualifiers = 0;
        /* array definition */
        next();
        if (tok == TOK_TWODOTS || tok == TOK_DOTS) {
            next();
            skip(']');
            /* parse next post type */
            post_type(type, ad, storage, (td & ~(TYPE_DIRECT|TYPE_ABSTRACT)) | TYPE_NEST);

            /* `Slice` type: C[..] -> struct { `size_t` length; T *ptr; } */
            type->ref = make_slice_type(type);
            type->t = VT_STRUCT;
            nocode_wanted = saved_nocode_wanted;
            return 1;
        }
        n = -1;
        t1 = 0;
        if (td & TYPE_PARAM) while (1) {
	    /* `XXX` `The` optional type-quals and static should only be accepted
	       in parameter decls.  `The` '*' as well, and then even only
	       in prototypes (not function defs).  */
	    switch (tok) {
	    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
		array_qualifiers |= VT_QUALIFIER_CONSTANT;
		next();
		continue;
	    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
		array_qualifiers |= VT_QUALIFIER_VOLATILE;
		next();
		continue;
	    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
		array_qualifiers |= VT_QUALIFIER_RESTRICT;
		next();
		continue;
	    case TOK_STATIC:
	    case '*':
		next();
		continue;
	    default:
		break;
	    }
            if (tok != ']') {
		/* `Code` generation is not done now but has to be done
		   at start of function. `Save` code here for later use. */
	        nocode_wanted = 1;
		skip_or_save_block(&vla_array_tok);
		unget_tok(0);
		vla_array_str = vla_array_tok->str;
		begin_macro(vla_array_tok, 2);
		next();
	        gexpr();
		end_macro();
		next();
		goto check;
            }
            break;

	} else if (tok != ']') {
            if (!local_stack || (storage & VT_STORAGE_STATIC))
                vpushi(expr_const());
            else {
		/* `VLAs` (which can only happen with `local_stack` && !`VT_STORAGE_STATIC`)
		   length must always be evaluated, even under `nocode_wanted`,
		   so that its size slot is initialized (e.g. under sizeof
		   or typeof).  */
		nocode_wanted = 0;
		gexpr();
	    }
check:
            if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
                n = vtop->c.i;
                if (n < 0)
                    tcc_error("invalid array size");
            } else {
                if (!is_integer_btype(vtop->type.t & VT_BTYPE))
                    tcc_error("size of variable length array should be an integer");
                n = 0;
                t1 = VT_VLA;
            }
        }
        skip(']');
        /* parse next post type */
        post_type(type, ad, storage, (td & ~(TYPE_DIRECT|TYPE_ABSTRACT)) | TYPE_NEST);

        if ((type->t & VT_BTYPE) == VT_FUNC)
            tcc_error("declaration of an array of functions");
        if ((type->t & VT_BTYPE) == VT_VOID
            || type_size(type, &align) < 0)
            tcc_error("declaration of an array of incomplete type elements");

        t1 |= type->t & VT_VLA;

        if (t1 & VT_VLA) {
            if (n < 0) {
		if  (td & TYPE_NEST)
                    tcc_error("need explicit inner array size in VLAs");
	    }
	    else {
                loc -= type_size(&int_type, &align);
                loc &= -align;
                n = loc;

                vpush_type_size(type, &align);
                gen_op('*');
                vset(&int_type, VT_LOCAL|VT_LVAL, n);
                vswap();
                vstore();
	    }
        }
        if (n != -1)
            vpop();
	nocode_wanted = saved_nocode_wanted;

        /* we push an anonymous symbol which will contain the array
           element type */
        s = sym_push(SYM_FIELD, type, 0, n);
        type->t = (t1 ? VT_VLA : VT_ARRAY) | VT_PTR;
        type->ref = s;
        type->t |= array_qualifiers;

        if (vla_array_str) {
            /* for function args, the top dimension is converted to pointer */
	    if ((t1 & VT_VLA) && (td & TYPE_NEST))
	        s->vla_array_str = vla_array_str;
	    else
	        tok_str_free_str(vla_array_str);
	}
    }
    return 1;
}

/* `Parse` one D-style array suffix that follows the type name, as in
   'int[3] a' or 'int[..] s'.  `The` suffix is applied to the type parsed
   so far, so that 'int[3][4] m' declares an array of 4 arrays of 3
   ints.  */
static void d_array_suffix(CType *type, int storage)
{
    int n, t1, align, saved_nocode_wanted;
    Sym *s;

    type->t &= ~VT_STORAGE;
    saved_nocode_wanted = nocode_wanted;
    next();
    if (tok == TOK_TWODOTS || tok == TOK_DOTS) {
        next();
        skip(']');
        /* `Slice` type: C[..] -> struct { `size_t` length; T *ptr; } */
        type->ref = make_slice_type(type);
        type->t = VT_STRUCT | storage;
        nocode_wanted = saved_nocode_wanted;
        return;
    }
    n = -1;
    t1 = 0;
    if (tok != ']') {
        if (!local_stack || (storage & VT_STORAGE_STATIC))
            vpushi(expr_const());
        else {
            /* `VLAs` (which can only happen with `local_stack` && !`VT_STORAGE_STATIC`)
               length must always be evaluated, even under `nocode_wanted`,
               so that its size slot is initialized (e.g. under sizeof
               or typeof).  */
            nocode_wanted = 0;
            gexpr();
        }
        if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
            n = vtop->c.i;
            if (n < 0)
                tcc_error("invalid array size");
        } else {
            if (!is_integer_btype(vtop->type.t & VT_BTYPE))
                tcc_error("size of variable length array should be an integer");
            n = 0;
            t1 = VT_VLA;
        }
    }
    skip(']');

    if ((type->t & VT_BTYPE) == VT_FUNC)
        tcc_error("declaration of an array of functions");
    if ((type->t & VT_BTYPE) == VT_VOID
        || type_size(type, &align) < 0)
        tcc_error("declaration of an array of incomplete type elements");

    t1 |= type->t & VT_VLA;

    if (t1 & VT_VLA) {
        if (n < 0)
            tcc_error("need explicit inner array size in VLAs");
        loc -= type_size(&int_type, &align);
        loc &= -align;
        n = loc;

        vpush_type_size(type, &align);
        gen_op('*');
        vset(&int_type, VT_LOCAL|VT_LVAL, n);
        vswap();
        vstore();
    }
    if (n != -1)
        vpop();
    nocode_wanted = saved_nocode_wanted;

    /* we push an anonymous symbol which will contain the array
       element type */
    s = sym_push(SYM_FIELD, type, 0, n);
    type->t = (t1 ? VT_VLA : VT_ARRAY) | VT_PTR;
    type->ref = s;
    type->t |= storage;
}

/* `Parse` a type declarator (except basic type), and return the type
   in 'type'. 'td' is a bitmask indicating which kind of type decl is
   expected. 'type' should contain the basic type. 'ad' is the
   attribute definition of the basic type. `It` can be modified by
   `type_decl`().  `If` this (possibly abstract) declarator is a pointer chain
   it returns the innermost pointed to type (equals *type, but is a different
   pointer), otherwise returns type itself, that's used for recursive calls.  */
static int has_auto_type(CType *t)
{
    if ((t->t & VT_BTYPE) == VT_AUTO)
        return 1;
    if ((t->t & VT_BTYPE) == VT_PTR || (t->t & VT_ARRAY))
        return has_auto_type(pointed_type(t));
    return 0;
}

static void subst_auto_type(CType *t, CType *deduced)
{
    if ((t->t & VT_BTYPE) == VT_AUTO) {
        int quals = t->t & VT_QUALIFIER;
        int storage = t->t & VT_STORAGE;
        t->t = (deduced->t & ~VT_QUALIFIER) | quals | storage;
        t->ref = deduced->ref;
    } else if ((t->t & VT_BTYPE) == VT_PTR || (t->t & VT_ARRAY)) {
        subst_auto_type(pointed_type(t), deduced);
    }
}

static CType *type_decl(CType *type, AttrDef *ad, int *v, int td)
{
    CType *post, *ret;
    int qualifiers, storage;

    /* recursive type, remove storage bits first, apply them later again */
    storage = type->t & VT_STORAGE;
    type->t &= ~VT_STORAGE;
    post = ret = type;

    /* A type taken from a string literal, as in 'typeof("ab")', no longer
       describes that literal, so give it its own array reference and stop
       treating it as one when it is converted to a slice.  */
    if ((type->t & VT_ARRAY) && type->ref && type->ref->a.str_lit)
        type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);

    while (tok == '*' || (tok == '[' && (td & TYPE_DIRECT) && !(td & TYPE_PARAM))) {
        if (tok == '[') {
            /* D-style array suffix before the declarator name.  `Parameter`
               declarators are excluded, as there '[' starts a C array
               declarator.  */
            d_array_suffix(type, storage);
            continue;
        }
        qualifiers = 0;
    redo:
        next();
        switch(tok) {
        case TOK__Atomic:
            qualifiers |= VT_ATOMIC;
            goto redo;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
            qualifiers |= VT_QUALIFIER_CONSTANT;
            goto redo;
        case TOK_IMMUTABLE:
            qualifiers |= VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE;
            goto redo;
        case TOK_VOLATILE1:
        case TOK_VOLATILE2:
        case TOK_VOLATILE3:
            qualifiers |= VT_QUALIFIER_VOLATILE;
            goto redo;
        case TOK_RESTRICT1:
        case TOK_RESTRICT2:
        case TOK_RESTRICT3:
            qualifiers |= VT_QUALIFIER_RESTRICT;
            goto redo;
	/* `XXX`: clarify attribute handling */
	case TOK_ATTRIBUTE1:
	case TOK_ATTRIBUTE2:
	    parse_attribute(ad);
	    break;
	case '@':
	    parse_d_attributes(ad);
	    break;
        }
        mk_pointer(type);
        type->t |= qualifiers;
	if (ret == type)
	    /* innermost pointed to type is the one for the first derivation */
	    ret = pointed_type(type);
    }

    if (tok == '(') {
	/* `This` is possibly a parameter type list for abstract declarators
	   ('int ()'), use `post_type` for testing this.  */
	if (!post_type(type, ad, 0, td)) {
	    /* `It`'s not, so it's a nested declarator, and the post operations
	       apply to the innermost pointed to type (if any).  */
	    /* `XXX`: this is not correct to modify 'ad' at this point, but
	       the syntax is not clear */
	    parse_attribute(ad);
	    post = type_decl(type, ad, v, td);
	    skip(')');
	} else
	  goto abstract;
    } else if (tok >= TOK_IDENT && (td & TYPE_DIRECT)) {
	/* type identifier */
	*v = tok;
	next();
    } else {
  abstract:
	if (!(td & TYPE_ABSTRACT))
	  expect("identifier");
	*v = 0;
    }
    post_type(post, ad, post != ret ? 0 : storage,
              td & ~(TYPE_DIRECT|TYPE_ABSTRACT));
    check_restrict_type(type);
    parse_attribute(ad);
    if (tok == '@')
        parse_d_attributes(ad);
    type->t |= storage;
    return ret;
}

/* indirection with full error checking and bound check */
ST_FUNC void indir(void)
{
    int qualifiers;

    if ((vtop->type.t & VT_BTYPE) != VT_PTR) {
        if ((vtop->type.t & VT_BTYPE) == VT_FUNC)
            return;
        expect("pointer");
    }
    if (vtop->r & VT_LVAL)
        gv(RC_INT);
    /* immutability is transitive: whatever an immutable pointer refers
       to is immutable as well */
    qualifiers = vtop->type.t & VT_QUALIFIER_IMMUTABLE;
    vtop->type = *pointed_type(&vtop->type);
    if (qualifiers)
        parse_btype_qualify(&vtop->type, VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
    /* `Arrays` and functions are never lvalues */
    if (!(vtop->type.t & (VT_ARRAY | VT_VLA))
        && (vtop->type.t & VT_BTYPE) != VT_FUNC) {
        vtop->r |= VT_LVAL;
        /* if bound checking, the referenced pointer must be checked */
#ifdef CONFIG_TCC_BCHECK
        if (tcc_state->do_bounds_check)
            vtop->r |= VT_MUSTBOUND;
#endif
    }
}

/* pass a parameter to a function and do type checking and casting */
static void gfunc_param_typed(Sym *func, Sym *arg)
{
    int func_type;
    CType type;

    if ((vtop->r & VT_LVAL) && vtop->sym && vtop->sym->a.nocopy)
        tcc_error("cannot copy @nocopy instance '%s'", get_tok_str(vtop->sym->v & ~SYM_FIELD, NULL));
    if ((vtop->type.t & VT_BTYPE) == VT_STRUCT && vtop->type.ref && vtop->type.ref->a.nocopy)
        tcc_error("struct '%s' is non-copyable", get_tok_str(vtop->type.ref->v & ~SYM_STRUCT, NULL));
    func_type = func->f.func_type;
    if (func_type == FUNC_OLD ||
        (func_type == FUNC_ELLIPSIS && arg == NULL)) {
        /* default casting : only need to convert float to double */
        if ((vtop->type.t & VT_BTYPE) == VT_FLOAT) {
            gen_cast_s(VT_DOUBLE);
        } else if (vtop->type.t & VT_BITFIELD) {
            type.t = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
	    type.ref = vtop->type.ref;
            gen_cast(&type);
        } else if (vtop->r & VT_MUSTCAST) {
            force_charshort_cast();
        }
    } else if (arg == NULL) {
        tcc_error("too many arguments to function");
    } else {
        type = arg->type;
        /* need to do that to avoid false warning */
        type.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
        gen_assign_cast(&type);
    }
}

/* parse an expression and return its type without any side effect. */
static void expr_type(CType *type, void (*expr_fn)(void))
{
    nocode_wanted++;
    expr_fn();
    *type = vtop->type;
    vpop();
    nocode_wanted--;
}

/* parse an expression of the form '(type)' or '(expr)' and return its
   type */
static void parse_expr_type(CType *type)
{
    int n;
    AttrDef ad;

    skip('(');
    if (parse_btype(type, &ad, 0)) {
        type_decl(type, &ad, &n, TYPE_ABSTRACT);
    } else {
        expr_type(type, gexpr);
    }
    skip(')');
}

static void parse_type(CType *type)
{
    AttrDef ad;
    int n;

    if (!parse_btype(type, &ad, 0)) {
        expect("type");
    }
    type_decl(type, &ad, &n, TYPE_ABSTRACT);
}

static void parse_builtin_params(int nc, const char *args)
{
    char c, sep = '(';
    CType type;
    if (nc)
        nocode_wanted++;
    next();
    if (*args == 0)
	skip(sep);
    while ((c = *args++)) {
	skip(sep);
	sep = ',';
        if (c == 't') {
            parse_type(&type);
	    vpush(&type);
	    continue;
        }
        expr_eq();
        type.ref = NULL;
        type.t = 0;
	switch (c) {
	    case 'e':
		continue;
	    case 'V':
                type.t = VT_QUALIFIER_CONSTANT;
	    case 'v':
                type.t |= VT_VOID;
                mk_pointer (&type);
                break;
	    case 'S':
                type.t = VT_QUALIFIER_CONSTANT;
	    case 's':
                type.t |= char_type.t;
                mk_pointer (&type);
                break;
	    case 'i':
                type.t = VT_INT;
                break;
	    case 'l':
                type.t = VT_SIZE_T;
                break;
	    default:
                break;
	}
        gen_assign_cast(&type);
    }
    skip(')');
    if (nc)
        nocode_wanted--;
}

static void parse_atomic(int atok)
{
    int size, align, arg, t, save = 0;
    CType *atom, *atom_ptr, ct = {0};
    SValue store;
    char buf[40];
    static const char *const templates[] = {
        /*
         * `Each` entry consists of callback and function template.
         * `The` template represents argument types and return type.
         *
         * ? void (return-only)
         * b bool
         * a atomic
         * A read-only atomic
         * p pointer to memory
         * v value
         * l load pointer
         * s save pointer
         * m memory model
         */

        /* keep in order of appearance in tcctok.h: */
        /* `__atomic_store` */            "alm.?",
        /* `__atomic_load` */             "Asm.v",
        /* `__atomic_exchange` */         "alsm.v",
        /* `__atomic_compare_exchange` */ "aplbmm.b",
        /* `__atomic_fetch_add` */        "avm.v",
        /* `__atomic_fetch_sub` */        "avm.v",
        /* `__atomic_fetch_or` */         "avm.v",
        /* `__atomic_fetch_xor` */        "avm.v",
        /* `__atomic_fetch_and` */        "avm.v",
        /* `__atomic_fetch_nand` */       "avm.v",
        /* `__atomic_and_fetch` */        "avm.v",
        /* `__atomic_sub_fetch` */        "avm.v",
        /* `__atomic_or_fetch` */         "avm.v",
        /* `__atomic_xor_fetch` */        "avm.v",
        /* `__atomic_and_fetch` */        "avm.v",
        /* `__atomic_nand_fetch` */       "avm.v"
    };
    const char *template = templates[(atok - TOK___atomic_store)];

    atom = atom_ptr = NULL;
    size = 0; /* pacify compiler */
    next();
    skip('(');
    for (arg = 0;;) {
        expr_eq();
        switch (template[arg]) {
        case 'a':
        case 'A':
            atom_ptr = &vtop->type;
            if ((atom_ptr->t & VT_BTYPE) != VT_PTR)
                expect("pointer");
            atom = pointed_type(atom_ptr);
            size = type_size(atom, &align);
            if (size > 8
                || (size & (size - 1))
                || (atok > TOK___atomic_compare_exchange
                    && (0 == btype_size(atom->t & VT_BTYPE)
                        || (atom->t & VT_BTYPE) == VT_PTR)))
                expect("integral or integer-sized pointer target type");
            /* `GCC` does not care either: */
            /* if (!(atom->t & `VT_ATOMIC`))
                `tcc_warning`("pointer target declaration is missing '`_Atomic`'"); */
            break;

        case 'p':
            if ((vtop->type.t & VT_BTYPE) != VT_PTR
             || type_size(pointed_type(&vtop->type), &align) != size)
                tcc_error("pointer target type mismatch in argument %d", arg + 1);
            gen_assign_cast(atom_ptr);
            break;
        case 'v':
            gen_assign_cast(atom);
            break;
        case 'l':
            indir();
            gen_assign_cast(atom);
            break;
        case 's':
            save = 1;
            indir();
            store = *vtop;
            vpop();
            break;
        case 'm':
            gen_assign_cast(&int_type);
            break;
        case 'b':
            ct.t = VT_BOOL;
            gen_assign_cast(&ct);
            break;
        }
        if ('.' == template[++arg])
            break;
        skip(',');
    }
    skip(')');

    ct.t = VT_VOID;
    switch (template[arg + 1]) {
    case 'b':
        ct.t = VT_BOOL;
        break;
    case 'v':
        ct = *atom;
        break;
    }

    sprintf(buf, "%s_%d", get_tok_str(atok, 0), size);
    vpush_helper_func(tok_alloc_const(buf));
    vrott(arg - save + 1);
    gfunc_call(arg - save);

    vpush(&ct);
    PUT_R_RET(vtop, ct.t);
    t = ct.t & VT_BTYPE;
    if (t == VT_BYTE || t == VT_SHORT || t == VT_BOOL) {
#ifdef PROMOTE_RET
        vtop->r |= BFVAL(VT_MUSTCAST, 1);
#else
        vtop->type.t = VT_INT;
#endif
    }
    gen_cast(&ct);
    if (save) {
        vpush(&ct);
        *vtop = store;
        vswap();
        vstore();
    }
}

/* '$' inside a subscript expression denotes the length of the
   subscripted array or slice.  */
static void gen_dollar(void)
{
    struct dollar_ctx *ctx;
    CType len_type;
    int align, elem_size;

    if (!nb_dollar_ctx)
        tcc_error("'$' is only allowed in a subscript expression");
    ctx = &dollar_ctx_stack[nb_dollar_ctx - 1];
    if (is_slice_type(&ctx->type)) {
        /* .length is the first member of the slice header */
        len_type = ctx->type.ref->next->type;
        vpushv(&ctx->sv);
        gaddrof();
        vtop->type = len_type;
        vtop->r |= VT_LVAL;
    } else if (ctx->type.t & VT_ARRAY) {
        vpush64(VT_SIZE_T, array_slice_length(&ctx->type));
    } else if (ctx->type.t & VT_VLA) {
        /* the total byte size is stored in the array reference */
        elem_size = type_size(&ctx->type.ref->type, &align);
        vset(&int_type, VT_LOCAL | VT_LVAL, ctx->type.ref->c);
        if (elem_size != 1) {
            vpushi(elem_size);
            gen_op('/');
        }
        gen_cast_s(VT_SIZE_T);
    } else {
        tcc_error("'$' requires an array or slice");
    }
}

/* ------------------------------------------------------------------------- */
/* Compile-time value tuples `$(...)`.  The value is a rodata blob laid out
   like the equivalent struct; `tuple_consts` remembers the element values so
   that a constant index folds without code generation.  */

typedef struct TupleConst {
    Sym *sym;                   /* blob symbol, also the lookup key */
    int n;                      /* number of elements */
    CType *types;               /* element types */
    CValue *vals;               /* scalar values or symbol addends */
    Sym **syms;                 /* element symbols, or NULL */
    struct TupleConst **nested; /* tuple-valued elements, or NULL */
} TupleConst;

static TCC_TLS TupleConst **tuple_consts;
static TCC_TLS int nb_tuple_consts;

/* (Removed unused TypeValue table; type_value_of() now always returns NULL.) */
static void free_tuple_consts(void)
{
    int i;

    for (i = 0; i < nb_tuple_consts; ++i) {
        TupleConst *tc = tuple_consts[i];
        tcc_free(tc->types);
        tcc_free(tc->vals);
        tcc_free(tc->syms);
        tcc_free(tc->nested);
        tcc_free(tc);
    }
    tcc_free(tuple_consts);
    tuple_consts = NULL;
    nb_tuple_consts = 0;
}

/* `Return` the constant tuple the value 'sv' refers to, or `NULL` if it is
   not one.  */
static TupleConst *tuple_const_of(SValue *sv)
{
    int i;

    if ((sv->r & (VT_VALMASK | VT_LVAL | VT_SYM)) != (VT_CONST | VT_SYM))
        return NULL;
    for (i = 0; i < nb_tuple_consts; ++i)
        if (tuple_consts[i]->sym == sv->sym)
            return tuple_consts[i];
    return NULL;
}

/* `Push` the compile-time element 'idx' of the constant tuple 'tc'.  */
static void tuple_const_push(TupleConst *tc, int idx)
{
    CType *type = &tc->types[idx];

    if (tc->nested && tc->nested[idx]) {
        vpushsym(type, tc->nested[idx]->sym);
    } else if (tc->syms && tc->syms[idx]) {
        vpushsym(type, tc->syms[idx]);
        vtop->c = tc->vals[idx];
        if ((type->t & VT_BTYPE) == VT_AUTO)
            vtop->type.ref = tc->syms[idx];
        if ((type->t & VT_BTYPE) == VT_STRUCT && !(type->t & VT_ARRAY))
            vtop->r |= VT_LVAL;
    } else {
        vsetc(type, VT_CONST, &tc->vals[idx]);
    }
}

/* `Add` an anonymous symbol of tuple type 'type' for the 'size' bytes at
   'offset' in 'sec'.  `The` symbol lives on the global stack, because the
   side table uses it as a key and a sym_pop() must not free it.  */
static Sym *tuple_const_sym(CType *type, Section *sec, unsigned long offset,
                            unsigned long size)
{
    Sym *sym = sym_push2(&global_stack, anon_sym++, type->t, 0);

    sym->type.ref = type->ref;
    sym->type.t |= VT_STORAGE_STATIC;
    sym->r = VT_CONST | VT_SYM;
    put_extern_sym(sym, sec, offset, size);
    return sym;
}

/* `Parse` the compile-time constructor `$(e1, ..., en)`, with 'tok' on the
   '(' of the `$(`.  `Push` the value of the single element, or a reference
   to a constant tuple, on the value stack.  */

/* `Return` the type carried by a type-value symbol 's', or `NULL`.
   (The TypeValue table was never populated, so this always returns NULL.)  */
static CType *type_value_of(Sym *s)
{
    (void)s;
    return NULL;
}

/* `Create` a rodata symbol for the C string 's' (including NUL).  */
static Sym *make_string_sym(const char *s)
{
    int len = strlen(s) + 1;
    unsigned long off = section_add(rodata_section, len, 1);
    memcpy(rodata_section->data + off, s, len);
    AttrDef ad = {0};
    ad.section = rodata_section;
    CType type;
    type.t = VT_BYTE | VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE;
    type.ref = NULL;
    mk_pointer(&type);       /* char * */
    type.t |= VT_ARRAY;      /* array of char*? matches TOK_STR */
    /* fix array size */
    if (type.ref)
        type.ref->c = len;
    Sym *sym = sym_push2(&global_stack, anon_sym++, type.t, 0);
    sym->type = type;
    sym->r = VT_CONST;
    put_extern_sym(sym, rodata_section, off, len);
    return sym;
}

static void parse_dollar_tuple(void)
{
    CType *types = NULL;
    CValue *vals = NULL;
    Sym **syms = NULL;
    TupleConst **nested = NULL;
    int n = 0, is_tuple = 0, i, saved_global_expr;
    CType ttype;

    next();                     /* the '(' of the `$(` */
    if (tok == ')')
        tcc_error("empty '$()' tuple");
    /* compound literal elements must live in rodata, not on the stack, so
       that their bytes are available while the compiler is running */
    saved_global_expr = global_expr;
    global_expr = 1;
    for (;;) {
        CType el;
        CValue ev;
        TupleConst *sub;
        int bt, aggregate;

        expr_eq();
        el = vtop->type;
        sub = tuple_const_of(vtop);
        bt = el.t & VT_BTYPE;
        aggregate = bt == VT_STRUCT || (el.t & VT_ARRAY);
        if (!sub) {
            if ((vtop->r & VT_VALMASK) != VT_CONST)
                goto not_constant;
            if (vtop->r & VT_SYM) {
                if (bt == VT_VOID || bt == VT_FUNC)
                    goto not_constant;
                if (aggregate && vtop->sym->v < SYM_FIRST_ANOM)
                    goto not_constant;
                if (!aggregate && bt != VT_PTR)
                    goto not_constant;
            } else if ((vtop->r & VT_LVAL) || aggregate
                       || bt == VT_VOID || bt == VT_FUNC) {
                goto not_constant;
            }
        }
        ev = vtop->c;
        syms = tcc_realloc(syms, (n + 1) * sizeof *syms);
        syms[n] = (!sub && (vtop->r & VT_SYM)) ? vtop->sym : NULL;
        vpop();
        types = tcc_realloc(types, (n + 1) * sizeof *types);
        vals = tcc_realloc(vals, (n + 1) * sizeof *vals);
        nested = tcc_realloc(nested, (n + 1) * sizeof *nested);
        types[n] = el;
        vals[n] = ev;
        nested[n] = sub;
        ++n;
        if (tok != ',')
            break;
        is_tuple = 1;
        next();
        if (tok == ')')         /* $(e,) is a 1-tuple */
            break;
    }
    skip(')');
    global_expr = saved_global_expr;
    if (!is_tuple) {
        /* a single element without a trailing comma is a plain value */
        if (nested[0]) {
            vpushsym(&types[0], nested[0]->sym);
        } else if (syms[0]) {
            vpushsym(&types[0], syms[0]);
            vtop->c = vals[0];
            if ((types[0].t & VT_BTYPE) == VT_STRUCT
                && !(types[0].t & VT_ARRAY))
                vtop->r |= VT_LVAL;
        } else {
            vsetc(&types[0], VT_CONST, &vals[0]);
        }
    } else {
        Sym *s = tuple_begin();
        Sym **plast = &s->next;
        TupleConst *tc;
        init_params p = { rodata_section };
        unsigned long offset;
        int size, align;

        for (i = 0; i < n; ++i)
            tuple_add_field(&plast, &types[i], i);
        ttype.t = VT_STRUCT;
        ttype.ref = tuple_layout(s);
        size = type_size(&ttype, &align);
        if (NODATA_WANTED)
            size = 0, align = 1;
        offset = section_add(p.sec, size, align);
        tc = tcc_mallocz(sizeof *tc);
        tc->sym = tuple_const_sym(&ttype, p.sec, offset, size);
        tc->n = n;
        tc->types = types;
        tc->vals = vals;
        tc->syms = syms;
        tc->nested = nested;
        for (i = 0; i < n; ++i) {
            int ofs = 0;
            tuple_field(&ttype, i, &ofs);
            tuple_const_push(tc, i);
            init_putv(&p, &types[i], offset + ofs);
        }
        tuple_consts = tcc_realloc(tuple_consts,
                                   (nb_tuple_consts + 1) * sizeof *tuple_consts);
        tuple_consts[nb_tuple_consts++] = tc;
        vpushsym(&ttype, tc->sym);
        /* the arrays now belong to 'tc' */
        types = NULL, vals = NULL, syms = NULL, nested = NULL;
    }
    tcc_free(types);
    tcc_free(vals);
    tcc_free(syms);
    tcc_free(nested);
    return;
not_constant:
    /* `Free` the elements collected so far: the leak checker in the
       -fsanitize=address test build would otherwise flag them.  */
    tcc_free(types);
    tcc_free(vals);
    tcc_free(syms);
    tcc_free(nested);
    global_expr = saved_global_expr;
    tcc_error("'$': element %d is not a compile-time constant", n + 1);
}

static void gen_tuple_literal(CType *type)
{
    int size, align, l, n = 0, arity = 0;
    Sym *f;

    for (f = type->ref->next; f; f = f->next)
        arity++;
    size = type_size(type, &align);
    loc = (loc - size) & -align;
    l = loc;
#ifdef CONFIG_TCC_BCHECK
    if (tcc_state->do_bounds_check)
        add_local_bound(l, size);
#endif

    f = type->ref->next;
    for (;;) {
        /* `A` tuple-typed element expects a nested tuple literal, so give
           the expression parser the element type as its tuple context.  */
        CType *saved_expect = tuple_expect;
        if (!f)
            tcc_error("tuple literal has more than %d element(s)", arity);
        if (is_tuple_type(&f->type))
            tuple_expect = &f->type;
        expr_eq();
        tuple_expect = saved_expect;
        if (f->type.t & VT_ARRAY) {
            int esize, ealign;

            /* `An` array element is not stored but copied, like a struct
               element: a pointer store would either truncate it or, in D
               mode, convert the destination to a slice.  */
            esize = type_size(&f->type, &ealign);
#ifdef CONFIG_TCC_BCHECK
            if (vtop->r & VT_MUSTBOUND)
                gbound(); /* check would be wrong after gaddrof() */
#endif
            gaddrof();  /* source address */
            vset(&char_pointer_type, VT_LOCAL | VT_LVAL, l + f->c);
            vswap();
            vpushv(vtop - 1);
            vtop->type.t = VT_PTR;
            gaddrof();  /* destination address */
            vswap();
            gen_copy_bytes(esize, ealign);
            vpop();
        } else {
            gen_cast(&f->type);
            gv(RC_TYPE(f->type.t));
            vset(&f->type, VT_LOCAL | VT_LVAL, l + f->c);
            vswap();
            vstore();
            vpop();
        }
        ++n;
        f = f->next;
        if (tok != ',')
            break;
        next();
        if (tok == ')')
            break;
    }
    if (n != arity)
        tcc_error("tuple literal has %d element(s), expected %d",
                  n, arity);
    skip(')');
    {
        CType tmp = *type;
        tmp.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
        vset(&tmp, VT_LOCAL | VT_LVAL, l);
    }
}

static void call_arg_expr(Sym *sa)
{
    CType *saved_expect = tuple_expect;
    if (sa && is_tuple_type(&sa->type))
        tuple_expect = &sa->type;
    expr_eq();
    tuple_expect = saved_expect;
}

/* `Parse` and act on a destructuring assignment
   `(lvalue1, ..., lvalueN) = init`, with 'tok' on the '(' of the pattern
   and the '=' already behind the group.  `The` pattern is only committed
   when the group has a top-level comma and is followed by '='; otherwise
   the group is replayed and zero is returned, so ordinary comma
   expressions keep their meaning.  `The` right side is evaluated into a
   tuple temporary before any destination is written, which makes
   `(a, b) = (b, a)` swap.  `Return` one with the tuple value as the
   value of the assignment expression on the value stack.  */
static int try_tuple_assign(void)
{
    TokenString *ts, *dup;
    AttrDef ad;
    CType ttype, res;
    CType *types = NULL;
    CType *saved_expect;
    Sym *s;
    Sym **plast;
    int has_comma, n = 0, i, base, addr, ofs, above;

    if (tok != '(')
        return 0;
    next();                     /* first token inside the group */
    /* `A` destination is an lvalue, so the group must start with an
       identifier that is not a type, a dereference or a nested group, and
       an identifier must be followed by a field, an index or the next
       destination.  `Skip` all other groups without scanning them, the
       same cheap probe `parse_tuple_type_opt()` uses.  */
    if (tok >= TOK_IDENT && !is_type_start(tok)) {
        int first = tok;
        next();                 /* token after the first destination name */
        if (tok != ',' && tok != '.' && tok != TOK_ARROW && tok != '[') {
            /* `Not` an lvalue destination: a plain expression group.  */
            unget_tok(first);
            unget_tok('(');
            return 0;
        }
        ts = tok_str_alloc();
        tok_str_add(ts, '(');
        tok_str_add(ts, first);
        tok_str_add2(ts, tok, &tokc);
        /* the manually copied ',' is already a top-level comma */
        has_comma = tok == ',';
        next();
        scan_paren_group_rest(ts, 0, &has_comma);
    } else if (tok == '*' || tok == '(') {
        ts = scan_paren_group(&dup, &has_comma, 0);
        if (dup)
            tok_str_free(dup);
    } else {
        unget_tok('(');
        return 0;
    }
    if (!has_comma) {
        /* `Not` a destructuring pattern: put the group back.  'tok' is on
           the closing ')' and the replay ends right behind it.  */
        begin_macro(ts, 1);
        next();
        return 0;
    }
    /* `The` token after the group decides.  It is consumed by the lookahead;
       on the commit path the replay below ends right at the right side.  */
    next();
    if (tok != '=') {
        /* `Restore` the group and the token that follows it.  */
        unget_tok('(');
        begin_macro(ts, 1);
        next();
        return 0;
    }

    saved_expect = tuple_expect;
    tuple_expect = NULL;
    begin_macro(ts, 1);         /* frees 'ts' at the end of the group */
    next();                     /* the '(' of the pattern */
    skip('(');
    base = vtop - vstack + 1;
    for (;;) {
        expr_eq();
        if (!(vtop->r & VT_LVAL) && !(vtop->type.t & VT_ARRAY))
            tcc_error("tuple assignment destination is not an lvalue");
        types = tcc_realloc(types, (n + 1) * sizeof *types);
        types[n++] = vtop->type;
        if (tok != ',')
            break;
        next();
        if (tok == ')')         /* (lvalue,) is a 1-element pattern */
            break;
    }
    skip(')');                  /* ends the replay: 'tok' is the right side */

    /* `Build` the tuple type the right side has to match.  */
    s = tuple_begin();
    plast = &s->next;
    for (i = 0; i < n; ++i)
        tuple_add_field(&plast, &types[i], i);
    ttype.t = VT_STRUCT;
    ttype.ref = tuple_layout(s);
    memset(&ad, 0, sizeof ad);
    decl_initializer_alloc(&ttype, &ad, VT_LOCAL | VT_LVAL, 1, 0, VT_LOCAL);
    addr = vtop->c.i;
    vpop();
#ifdef CONFIG_TCC_BCHECK
    if (tcc_state->do_bounds_check) {
        int align;
        /* the temporary has no symbol, so register its bound by hand like
           the destructuring declaration does */
        add_local_bound(addr, type_size(&ttype, &align));
    }
#endif

    for (i = 0; i < n; ++i) {
        Sym *f = tuple_field(&ttype, i, &ofs);
        vpushv(&vstack[base + i]);
        if (f->type.t & VT_ARRAY) {
            int esize, ealign;
            esize = type_size(&f->type, &ealign);
#ifdef CONFIG_TCC_BCHECK
            if (vtop->r & VT_MUSTBOUND)
                gbound();
#endif
            gaddrof();          /* destination address */
            vset(&f->type, VT_LOCAL | VT_LVAL, addr + ofs);
            gaddrof();          /* source address */
            gen_copy_bytes(esize, ealign);
        } else {
            vset(&f->type, VT_LOCAL | VT_LVAL, addr + ofs);
            vstore();
            vpop();
        }
    }

    /* `Keep` the right side as the value of the assignment expression and
       drop the saved destinations underneath it.  */
    res = ttype;
    res.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
    vset(&res, VT_LOCAL | VT_LVAL, addr);
    above = (vtop - vstack) - (base + n) + 1;
    memmove(&vstack[base], &vstack[base + n], above * sizeof *vstack);
    vtop -= n;

    tcc_free(types);
    tuple_expect = saved_expect;
    return 1;
}

static void gen_members_from_type(CType type1)
{
    AttrDef ad;
    memset(&ad, 0, sizeof ad);
    /* build a constant tuple of type values, one per field */
    {
    Sym *ts = tuple_begin();
    Sym **plast = &ts->next;
    Sym *f;
    CType ttype;
    TupleConst *tc;
    init_params p = { rodata_section };
    unsigned long offset;
    int size, align, nf = 0, i;
    CType *types = NULL;
    CValue *vals = NULL;
    Sym **syms = NULL;

    for (f = type1.ref->next; f; f = f->next, ++nf) {
    tuple_add_field(&plast, &f->type, nf);
    }
    ttype.t = VT_STRUCT;
    ttype.ref = tuple_layout(ts);
    size = type_size(&ttype, &align);
    if (NODATA_WANTED)
    size = 0, align = 1;
    offset = section_add(p.sec, size, align);
    tc = tcc_mallocz(sizeof *tc);
    tc->sym = tuple_const_sym(&ttype, p.sec, offset, size);
    tc->n = nf;
    tc->types = tcc_realloc(NULL, nf * sizeof *types);
    tc->vals = tcc_realloc(NULL, nf * sizeof *vals);
    tc->syms = tcc_realloc(NULL, nf * sizeof *syms);
    tc->nested = NULL;
    for (i = 0; i < nf; ++i) {
    tc->types[i].t = VT_AUTO;
    tc->types[i].ref = NULL;
    tc->vals[i].i = 0;
    tc->syms[i] = NULL;
    }
    {
    Sym *ff = type1.ref->next;
    for (i = 0; i < nf; ++i, ff = ff->next)
        tc->types[i] = ff->type;
    }
    {
    Sym *ff = type1.ref->next;
    for (i = 0; i < nf; ++i, ff = ff->next) {
    Sym *fsym = sym_push2(&global_stack,
    (ff->v & ~SYM_FIELD) | SYM_FIELD,
    ff->type.t, 0);
    fsym->type = ff->type;
    fsym->asm_label = ff->v & ~SYM_FIELD;
    if (IS_ENUM(type1.t))
    fsym->c = ff->enum_val;
    else
    fsym->c = ff->c;
    tc->syms[i] = fsym;
    }
    }
    for (i = 0; i < nf; ++i) {
    int ofs = 0;
    tuple_field(&ttype, i, &ofs);
    tuple_const_push(tc, i);
    if ((tc->types[i].t & VT_BTYPE) != VT_AUTO)
    init_putv(&p, &tc->types[i], offset + ofs);
    else
    vpop();
    }
    tuple_consts = tcc_realloc(tuple_consts,
    (nb_tuple_consts + 1) * sizeof *tuple_consts);
    tuple_consts[nb_tuple_consts++] = tc;
    vpushsym(&ttype, tc->sym);
    /* ownership transferred */
    }
}

/* `Type` trait helpers for `$is`/`$has*`.  */
static int type_is(CType *type, int kind)
{
    int bt = type->t & VT_BTYPE;
    switch (kind) {
    case 0: /* struct */
        return bt == VT_STRUCT && !IS_UNION(type->t) && !IS_ENUM(type->t);
    case 1: /* union */
        return IS_UNION(type->t);
    case 2: /* enum */
        return IS_ENUM(type->t);
    case 3: /* pointer */
        return bt == VT_PTR && !(type->t & VT_ARRAY);
    case 4: /* array */
        return (type->t & VT_ARRAY) != 0;
    case 5: /* slice */
        return bt == VT_SLICE;
    case 6: /* function */
        return bt == VT_FUNC;
    case 7: /* void */
        return bt == VT_VOID;
    case 8: /* integer */
        return bt == VT_BYTE || bt == VT_SHORT || bt == VT_INT
            || bt == VT_LLONG || bt == VT_BOOL;
    case 9: /* float */
        return bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE;
    }
    return 0;
}

static int type_is_signed(CType *type)
{
    int bt = type->t & VT_BTYPE;
    return (bt == VT_BYTE || bt == VT_SHORT || bt == VT_INT || bt == VT_LLONG)
        && !(type->t & VT_UNSIGNED) && bt != VT_BOOL;
}

static int type_is_unsigned(CType *type)
{
    int bt = type->t & VT_BTYPE;
    return (bt == VT_BYTE || bt == VT_SHORT || bt == VT_INT || bt == VT_LLONG || bt == VT_BOOL)
        && ((type->t & VT_UNSIGNED) || bt == VT_BOOL);
}

static int type_is_const(CType *type)
{
    return (type->t & VT_QUALIFIER_CONSTANT) != 0;
}

static int type_is_volatile(CType *type)
{
    return (type->t & VT_QUALIFIER_VOLATILE) != 0;
}

static int type_is_immutable(CType *type)
{
    return (type->t & VT_QUALIFIER_IMMUTABLE) != 0;
}

static int type_has_indirections(CType *type)
{
    int bt = type->t & VT_BTYPE;
    if (bt == VT_PTR || bt == VT_SLICE)
        return 1;
    if (bt == VT_STRUCT) {
        Sym *f;
        for (f = type->ref ? type->ref->next : NULL; f; f = f->next) {
            CType ft = f->type;
            if (type_has_indirections(&ft))
                return 1;
        }
    }
    if (type->t & VT_ARRAY) {
        Sym *s = type->ref;
        if (s && s->c > 0) {
            CType et = s->type;
            if (type_has_indirections(&et))
                return 1;
        }
    }
    return 0;
}
static int type_has_aliasing(CType *type)
{
    int bt = type->t & VT_BTYPE;
    if (bt == VT_PTR) {
        CType *pt = pointed_type(type);
        if (pt->t & VT_QUALIFIER_IMMUTABLE)
            return 0;
        return 1;
    }
    if (bt == VT_SLICE) {
        CType *pt = pointed_type(&type->ref->next->next->type);
        if (pt->t & VT_QUALIFIER_IMMUTABLE)
            return 0;
        return 1;
    }
    if (bt == VT_STRUCT) {
        Sym *f;
        for (f = type->ref ? type->ref->next : NULL; f; f = f->next) {
            CType ft = f->type;
            if (type_has_aliasing(&ft))
                return 1;
        }
    }
    if (type->t & VT_ARRAY) {
        Sym *s = type->ref;
        if (s && s->c > 0) {
            CType et = s->type;
            if (type_has_aliasing(&et))
                return 1;
        }
    }
    return 0;
}

ST_FUNC void unary(void)
{
    int n, t, align, size, r, str_lit = 0;
    CType type, type1;
    Sym *s;
    AttrDef ad;

    /* generate line number info */
    if (debug_modes)
        tcc_debug_line(tcc_state), tcc_tcov_check_line (tcc_state, 1);

    type.ref = NULL;
    if (tcc_state->lang_d && is_type_start(tok)) {
        parse_type(&type);
        if (tok == '.') {
            next();
            if (tok == TOK_SIZEOF) {
            next();
            vpush_type_size(&type, &align);
            gen_cast_s(VT_SIZE_T);
            return;
        } else if (tok >= TOK_UIDENT
                   && !strcmp(get_tok_str(tok, NULL), "$members")) {
            next(); /* consume $members */
            if ((type.t & VT_BTYPE) != VT_STRUCT && !IS_ENUM(type.t))
                tcc_error("$members requires a struct, union or enum type");
            gen_members_from_type(type);
            return;
        } else {
            tcc_error("expected 'sizeof' after type");
        }
        } else {
            tcc_error("expected '.' after type");
        }
    }
    /* `XXX`: `GCC` 2.95.3 does not generate a table although it should be
       better here */
 tok_next:
    switch(tok) {
    case TOK_EXTENSION:
        next();
        goto tok_next;
    case TOK_LCHAR:
#ifdef TCC_TARGET_PE
        t = VT_SHORT|VT_UNSIGNED;
        goto push_tokc;
#endif
    case TOK_CINT:
    case TOK_CCHAR:
	t = VT_INT;
 push_tokc:
	type.t = t;
	vsetc(&type, VT_CONST, &tokc);
        next();
        break;
    case TOK_CUINT:
        t = VT_INT | VT_UNSIGNED;
        goto push_tokc;
    case TOK_CLLONG:
        t = VT_LLONG;
	goto push_tokc;
    case TOK_CULLONG:
        t = VT_LLONG | VT_UNSIGNED;
	goto push_tokc;
    case TOK_CFLOAT:
        t = VT_FLOAT;
	goto push_tokc;
    case TOK_CDOUBLE:
        t = VT_DOUBLE;
	goto push_tokc;
    case TOK_CLDOUBLE:
#ifdef TCC_USING_DOUBLE_FOR_LDOUBLE
        t = VT_DOUBLE | VT_LONG;
        tokc.d = tokc.ld;
#else
        t = VT_LDOUBLE;
#endif
	goto push_tokc;
    case TOK_CLONG:
        t = (LONG_SIZE == 8 ? VT_LLONG : VT_INT) | VT_LONG;
	goto push_tokc;
    case TOK_CULONG:
        t = (LONG_SIZE == 8 ? VT_LLONG : VT_INT) | VT_LONG | VT_UNSIGNED;
	goto push_tokc;
    case TOK___FUNCTION__:
        if (!gnu_ext)
            goto tok_identifier;
        /* fall thru */
    case TOK___FUNC__:
        tok = TOK_STR;
        cstr_reset(&tokcstr);
        cstr_cat(&tokcstr, funcname, 0);
        tokc.str.size = tokcstr.size;
        tokc.str.data = tokcstr.data;
        str_lit = 0;
        goto case_TOK_STR;
    case TOK_LSTR:
#ifdef TCC_TARGET_PE
        t = VT_SHORT | VT_UNSIGNED;
#else
        t = VT_INT;
#endif
        str_lit = 1;
        goto str_init;
    case TOK_STR:
        str_lit = 1;
    case_TOK_STR:
        /* string parsing */
        t = char_type.t;
    str_init:
        if (tcc_state->warn_write_strings & WARN_ON || tcc_state->lang_d)
            t |= VT_QUALIFIER_CONSTANT;
        if (tcc_state->lang_d)
            t |= VT_QUALIFIER_IMMUTABLE;
        type.t = t;
        if (nocode_wanted > 1) {
            Sym *s = sym_push(SYM_FIELD, &type, 0, tokc.str.size);
            type.t = t | VT_PTR | VT_ARRAY;
            type.ref = s;
            vpush(&type);
            vtop->r = VT_CONST;
            next();
            break;
        }
        mk_pointer(&type);
        type.t |= VT_ARRAY;
        memset(&ad, 0, sizeof(AttrDef));
        ad.section = rodata_section;
        decl_initializer_alloc(&type, &ad, VT_CONST, 2, 0, 0);
        /* an array holding a string literal is stored with its
           terminating null character, which a slice does not expose */
        if (str_lit && (vtop->type.t & VT_ARRAY) && vtop->type.ref)
            vtop->type.ref->a.str_lit = 1;
        break;
    case TOK_SOTYPE:
    case '(':
        t = tok;
        next();
        /* cast ? */
        {
            const uint8_t *saved_ptr = NULL;
            if (tcc_state->lang_d && tok == '@') {
                saved_ptr = file->buf_ptr;
                next();
                if (tok == TOK_IMMUTABLE || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "immutable"))) {
                    tok = TOK_IMMUTABLE;
                } else if (tok == TOK_CONST1 || tok == TOK_CONST2 || tok == TOK_CONST3 || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "const"))) {
                    tok = TOK_CONST1;
                } else if (saved_ptr) {
                    file->buf_ptr = (uint8_t *)saved_ptr;
                    next();
                }
            }
        }
        if (parse_btype(&type, &ad, 0)) {
            /* a type tuple `(T1, T2, ...)` whose first element was parsed
               by `parse_btype()` above */
            if (tok == ',')
                parse_tuple_type_rest(&type, &ad);
            else
                type_decl(&type, &ad, &n, TYPE_ABSTRACT);
            skip(')');
            /* check `ISOC99` compound literal */
            if (tok == '{') {
                    /* data is allocated locally by default */
                if (global_expr)
                    r = VT_CONST;
                else
                    r = VT_LOCAL;
                /* all except arrays are lvalues */
                if (!(type.t & VT_ARRAY))
                    r |= VT_LVAL;
                memset(&ad, 0, sizeof(AttrDef));
                decl_initializer_alloc(&type, &ad, r, 1, 0, 0);
            } else if (t == TOK_SOTYPE) { /* from sizeof/alignof (...) */
                vpush(&type);
                return;
            } else if (tcc_state->lang_d && tok == '.') {
                next();
                if (tok == TOK_SIZEOF) {
                    next();
                    vpush_type_size(&type, &align);
                    gen_cast_s(VT_SIZE_T);
                    return;
                } else {
                    tcc_error("expected 'sizeof' after type");
                }
            } else {
                if (is_tuple_type(&type)) {
                    CType *saved_expect = tuple_expect;
                    tuple_expect = &type;
                    unary();
                    tuple_expect = saved_expect;
                    if (!is_tuple_type(&vtop->type))
                        tcc_error("cannot cast to tuple type");
                } else {
                    unary();
                }
                gen_cast(&type);
            }
        } else if (tok == '{') {
	    int saved_nocode_wanted = nocode_wanted;
            if (CONST_WANTED && !NOEVAL_WANTED)
                expect("constant");
            if (0 == local_scope)
                tcc_error("statement expression outside of function");
            /* save all registers */
            save_regs(0);
            /* statement expression : we do not accept break/continue
               inside as `GCC` does.  `We` do retain the `nocode_wanted` state,
	       as statement expressions can't ever be entered from the
	       outside, so any reactivation of code emission (from labels
	       or loop heads) can be disabled again after the end of it. */
            /* default return value is (void) */
            vpushi(0), vtop->type.t = VT_VOID;
            block(STMT_EXPR);
            /* `If` the statement expr can be entered, then we retain the current
               `nocode_wanted` state (from e.g. a 'return 0;' in the stmt-expr).
               `If` it can't be entered then the state is that from before the
               statement expression.  */
            if (saved_nocode_wanted)
              nocode_wanted = saved_nocode_wanted;
            skip(')');
        } else {
            if (tuple_expect) {
                CType *expect = tuple_expect;
                tuple_expect = NULL;
                gen_tuple_literal(expect);
            } else {
                gexpr();
                skip(')');
            }
        }
        break;
    case '*':
        next();
        unary();
        indir();
        break;
    case '&':
        next();
        unary();
        /* functions names must be treated as function pointers,
           except for unary '&' and sizeof. `Since` we consider that
           functions are not lvalues, we only have to handle it
           there and in function calls. */
        /* arrays can also be used although they are not lvalues */
        if ((vtop->type.t & VT_BTYPE) != VT_FUNC &&
            !(vtop->type.t & (VT_ARRAY | VT_VLA)))
            test_lvalue();
        if (vtop->sym)
          vtop->sym->a.addrtaken = 1;
        mk_pointer(&vtop->type);
        gaddrof();
        break;
    case '!':
        next();
        unary();
        gen_test_zero(TOK_EQ);
        break;
    case '~':
        next();
        unary();
        vpushi(-1);
        gen_op('^');
        break;
    case '+':
        next();
        unary();
        if ((vtop->type.t & VT_BTYPE) == VT_PTR)
            tcc_error("pointer not accepted for unary plus");
        /* `In` order to force cast, we add zero, except for floating point
	   where we really need an noop (otherwise -0.0 will be transformed
	   into +0.0).  */
	if (!is_float(vtop->type.t)) {
	    vpushi(0);
	    gen_op('+');
	}
        break;
    case TOK_SIZEOF:
    case TOK_ALIGNOF1:
    case TOK_ALIGNOF2:
    case TOK_ALIGNOF3:
        t = tok;
        next();
        if (tok == '(')
            tok = TOK_SOTYPE;
        expr_type(&type, unary);
        if ((type.t & VT_BTYPE) == VT_AUTO && type.ref) {
            CType *tv = type_value_of(type.ref);
            if (tv)
                type = *tv;
        }
        if (t == TOK_SIZEOF) {
            vpush_type_size(&type, &align);
            gen_cast_s(VT_SIZE_T);
        } else {
            type_size(&type, &align);
            s = NULL;
            if (vtop[1].r & VT_SYM)
                s = vtop[1].sym; /* hack: accessing previous vtop */
            if (s && s->a.aligned)
                align = 1 << (s->a.aligned - 1);
            vpushs(align);
        }
        break;

    case TOK_builtin_expect:
	/* `__builtin_expect` is a no-op for now */
	parse_builtin_params(0, "ee");
	vpop();
        break;
    case TOK_builtin_types_compatible_p:
	parse_builtin_params(0, "tt");
	n = compare_types(&vtop[-1].type, &vtop[0].type, 1);
	vtop -= 2;
	vpushi(n);
        break;
    case TOK_builtin_choose_expr:
	{
	    int64_t c;
	    next();
	    skip('(');
	    c = expr_const64();
	    skip(',');
	    if (!c) {
		nocode_wanted++;
	    }
	    expr_eq();
	    if (!c) {
		vpop();
		nocode_wanted--;
	    }
	    skip(',');
	    if (c) {
		nocode_wanted++;
	    }
	    expr_eq();
	    if (c) {
		vpop();
		nocode_wanted--;
	    }
	    skip(')');
	}
        break;
    case TOK_builtin_constant_p:
	parse_builtin_params(1, "e");
	n = 1;
	if ((vtop->r & (VT_VALMASK | VT_LVAL)) != VT_CONST
	    || ((vtop->r & VT_SYM) && vtop->sym->a.addrtaken)
	    )
	    n = 0;
	vtop--;
	vpushi(n);
        break;
    case TOK_builtin_unreachable:
	parse_builtin_params(0, ""); /* just skip '()' */
        type.t = VT_VOID;
        vpush(&type);
        CODE_OFF();
        break;
    case TOK_builtin_frame_address:
    case TOK_builtin_return_address:
        {
            int tok1 = tok;
            int level;
            next();
            skip('(');
            level = expr_const();
            if (level < 0)
                tcc_error("%s only takes positive integers", get_tok_str(tok1, 0));
            skip(')');
            type.t = VT_VOID;
            mk_pointer(&type);
            vset(&type, VT_LOCAL, 0);       /* local frame */
            while (level--) {
#ifdef TCC_TARGET_RISCV64
                vpushi(2*PTR_SIZE);
                gen_op('-');
#endif
                mk_pointer(&vtop->type);
                indir();                    /* -> parent frame */
            }
            if (tok1 == TOK_builtin_return_address) {
                // assume return address is just above frame pointer on stack
#ifdef TCC_TARGET_ARM
                vpushi(2*PTR_SIZE);
                gen_op('+');
#elif defined TCC_TARGET_RISCV64
                vpushi(PTR_SIZE);
                gen_op('-');
#else
                vpushi(PTR_SIZE);
                gen_op('+');
#endif
                mk_pointer(&vtop->type);
                indir();
            }
        }
        break;
#ifdef TCC_TARGET_RISCV64
    case TOK_builtin_va_start:
        parse_builtin_params(0, "ee");
        r = vtop->r & VT_VALMASK;
        if (r == VT_LLOCAL)
            r = VT_LOCAL;
        if (r != VT_LOCAL)
            tcc_error("__builtin_va_start expects a local variable");
        gen_va_start();
	vstore();
        break;
#endif
#ifdef TCC_TARGET_X86_64
#ifdef TCC_TARGET_PE
    case TOK_builtin_va_start:
	parse_builtin_params(0, "ee");
        r = vtop->r & VT_VALMASK;
        if (r == VT_LLOCAL)
            r = VT_LOCAL;
        if (r != VT_LOCAL)
            tcc_error("__builtin_va_start expects a local variable");
        vtop->r = r;
	vtop->type = char_pointer_type;
	vtop->c.i += 8;
	vstore();
        break;
#else
    case TOK_builtin_va_arg_types:
	parse_builtin_params(0, "t");
	vpushi(classify_x86_64_va_arg(&vtop->type));
	vswap();
	vpop();
	break;
#endif
#endif

#ifdef TCC_TARGET_ARM64
    case TOK_builtin_va_start: {
	parse_builtin_params(0, "ee");
        //xx check types
        gen_va_start();
        vpushi(0);
        vtop->type.t = VT_VOID;
        break;
    }
    case TOK_builtin_va_arg: {
	parse_builtin_params(0, "et");
	type = vtop->type;
	vpop();
        //xx check types
        gen_va_arg(&type);
        vtop->type = type;
        break;
    }
#endif
#ifdef TCC_TARGET_ARM64
    case TOK___arm64_clear_cache: {
	parse_builtin_params(0, "ee");
        gen_clear_cache();
        vpushi(0);
        vtop->type.t = VT_VOID;
        break;
    }
#endif
#ifdef TCC_TARGET_RISCV64
    case TOK___riscv64_clear_cache: {
	parse_builtin_params(0, "ee");
	vpop();
	vpop();
        gen_clear_cache();
        vpushi(0);
        vtop->type.t = VT_VOID;
        break;
    }
#endif

    /* atomic operations */
    case TOK___atomic_store:
    case TOK___atomic_load:
    case TOK___atomic_exchange:
    case TOK___atomic_compare_exchange:
    case TOK___atomic_fetch_add:
    case TOK___atomic_fetch_sub:
    case TOK___atomic_fetch_or:
    case TOK___atomic_fetch_xor:
    case TOK___atomic_fetch_and:
    case TOK___atomic_fetch_nand:
    case TOK___atomic_add_fetch:
    case TOK___atomic_sub_fetch:
    case TOK___atomic_or_fetch:
    case TOK___atomic_xor_fetch:
    case TOK___atomic_and_fetch:
    case TOK___atomic_nand_fetch:
        parse_atomic(tok);
        break;

    /* pre operations */
    case TOK_INC:
    case TOK_DEC:
        t = tok;
        next();
        unary();
        inc(0, t);
        break;
    case '-':
        next();
        unary();
	if (is_float(vtop->type.t)) {
            gen_opif(TOK_NEG);
	} else {
            vpushi(0);
            vswap();
            gen_op('-');
        }
        break;
    case TOK_LAND:
        if (!gnu_ext)
            goto tok_identifier;
        next();
        /* allow to take the address of a label */
        if (tok < TOK_UIDENT)
            expect("label identifier");
        s = label_find(tok);
        if (!s) {
            s = label_push(&global_label_stack, tok, LABEL_FORWARD);
        } else {
            if (s->r == LABEL_DECLARED)
                s->r = LABEL_FORWARD;
        }
        if ((s->type.t & VT_BTYPE) != VT_PTR) {
            s->type.t = VT_VOID;
            mk_pointer(&s->type);
            s->type.t |= VT_STORAGE_STATIC;
        }
        vpushsym(&s->type, s);
        next();
        break;

    case TOK_GENERIC:
    {
	CType controlling_type;
	int has_default = 0;
	int has_match = 0;
	int learn = 0;
	TokenString *str = NULL;
	int saved_nocode_wanted = nocode_wanted;
        nocode_wanted &= ~CONST_WANTED_MASK;

        next();
	skip('(');
	expr_type(&controlling_type, expr_eq);
	convert_expression_type(&controlling_type);

        nocode_wanted = saved_nocode_wanted;

        for (;;) {
	    learn = 0;
	    skip(',');
	    if (tok == TOK_DEFAULT) {
		if (has_default)
		    tcc_error("too many 'default'");
		has_default = 1;
		if (!has_match)
		    learn = 1;
		next();
	    } else {
		int v;
		parse_btype(&type, &ad, 0);
		type_decl(&type, &ad, &v, TYPE_ABSTRACT);
		if (compare_types(&controlling_type, &type, 0)) {
		    if (has_match) {
		      tcc_error("type match twice");
		    }
		    has_match = 1;
		    learn = 1;
		}
	    }
	    skip(':');
	    if (learn) {
		if (str)
		    tok_str_free(str);
		skip_or_save_block(&str);
	    } else {
		skip_or_save_block(NULL);
	    }
	    if (tok == ')')
		break;
	}
	if (!str) {
	    char buf[60];
	    type_to_str(buf, sizeof buf, &controlling_type, NULL);
	    tcc_error("type '%s' does not match any association", buf);
	}
	begin_macro(str, 1);
	next();
	expr_eq();
	if (tok != TOK_EOF)
	    expect(",");
	end_macro();
        next();
	break;
    }
    // special qnan , snan and infinity values
    case TOK___NAN__:
        n = 0x7fc00000;
special_math_val:
	vpushi(n);
	vtop->type.t = VT_FLOAT;
        next();
        break;
    case TOK___SNAN__:
	n = 0x7f800001;
	goto special_math_val;
    case TOK___INF__:
	n = 0x7f800000;
	goto special_math_val;

    default:
    tok_identifier:
        if (tok < TOK_UIDENT)
            tcc_error("expression expected before '%s'", get_tok_str(tok, &tokc));
        t = tok;
        next();
        /* `$(...)` is the compile-time constructor in D mode; it must be
           checked before '$' is taken as the subscript length */
        if (tcc_state->lang_d && tok == '('
            && !strcmp(get_tok_str(t, NULL), "$")) {
            parse_dollar_tuple();
            break;
        }
        /* UFCS: T.$members */
        if (tcc_state->lang_d && tok == '.' ) {
            next(); /* consume '.' */
            if (tok >= TOK_UIDENT && !strcmp(get_tok_str(tok, NULL), "$members")) {
                Sym *tsym = sym_find(t);
                memset(&ad, 0, sizeof ad);
                if (tsym && (tsym->type.t & VT_STORAGE_TYPEDEF)) {
                    type1 = tsym->type;
                } else {
                    Sym *ss = struct_find(t);
                    if (ss) {
                        type1.t = ss->type.t;
                        type1.ref = ss;
                    } else {
                        tcc_error("expected type");
                    }
                }
                next(); /* consume $members */
                if ((type1.t & VT_BTYPE) != VT_STRUCT && !IS_ENUM(type1.t))
                    tcc_error("$members requires a struct, union or enum type");
                gen_members_from_type(type1);
                return;
            } else {
                /* not $members: restore the consumed '.' */
                unget_tok('.');
            }
        }
        if (tcc_state->lang_d && tok == '('
            && !strcmp(get_tok_str(t, NULL), "$members")) {
            skip('(');
            if (!parse_btype(&type1, &ad, 0))
                tcc_error("expected type");
            type_decl(&type1, &ad, &n, TYPE_ABSTRACT);
            skip(')');
            if ((type1.t & VT_BTYPE) != VT_STRUCT && !IS_ENUM(type1.t))
                tcc_error("$members requires a struct, union or enum type");
            gen_members_from_type(type1);
            break;
        }
        if (tcc_state->lang_d && tok == '('
            && (!strcmp(get_tok_str(t, NULL), "$is")
                || !strcmp(get_tok_str(t, NULL), "$isA")
                || !strcmp(get_tok_str(t, NULL), "$hasIndirections")
                || !strcmp(get_tok_str(t, NULL), "$hasAliasing")
                || !strcmp(get_tok_str(t, NULL), "$isSigned")
                || !strcmp(get_tok_str(t, NULL), "$isUnsigned")
                || !strcmp(get_tok_str(t, NULL), "$isConst")
                || !strcmp(get_tok_str(t, NULL), "$isVolatile")
                || !strcmp(get_tok_str(t, NULL), "$isImmutable"))) {
            int is_isA = !strcmp(get_tok_str(t, NULL), "$isA");
            int is_hasInd = !strcmp(get_tok_str(t, NULL), "$hasIndirections");
            int is_hasAlias = !strcmp(get_tok_str(t, NULL), "$hasAliasing");
            int is_isSigned = !strcmp(get_tok_str(t, NULL), "$isSigned");
            int is_isUnsigned = !strcmp(get_tok_str(t, NULL), "$isUnsigned");
            int is_isConst = !strcmp(get_tok_str(t, NULL), "$isConst");
            int is_isVolatile = !strcmp(get_tok_str(t, NULL), "$isVolatile");
            int is_isImmutable = !strcmp(get_tok_str(t, NULL), "$isImmutable");
            skip('(');
            if (is_isA) {
                CType t1, t2;
                AttrDef ad1, ad2;
                memset(&ad1, 0, sizeof ad1);
                memset(&ad2, 0, sizeof ad2);
                if (!parse_btype(&t1, &ad1, 0))
                    tcc_error("expected type");
                type_decl(&t1, &ad1, &n, TYPE_ABSTRACT);
                skip(',');
                if (!parse_btype(&t2, &ad2, 0))
                    tcc_error("expected type");
                type_decl(&t2, &ad2, &n, TYPE_ABSTRACT);
                skip(')');
                int res = (t1.t == t2.t) && (t1.ref == t2.ref);
                vpushi(res);
            } else if (is_hasInd) {
                CType t1;
                AttrDef ad1 = {0};
                if (!parse_btype(&t1, &ad1, 0))
                    tcc_error("expected type");
                type_decl(&t1, &ad1, &n, TYPE_ABSTRACT);
                skip(')');
                vpushi(type_has_indirections(&t1));
            } else if (is_hasAlias) {
                CType t1;
                AttrDef ad1 = {0};
                if (!parse_btype(&t1, &ad1, 0))
                    tcc_error("expected type");
                type_decl(&t1, &ad1, &n, TYPE_ABSTRACT);
                skip(')');
                vpushi(type_has_aliasing(&t1));
            } else if (is_isSigned || is_isUnsigned || is_isConst || is_isVolatile || is_isImmutable) {
                CType t1;
                AttrDef ad1 = {0};
                if (!parse_btype(&t1, &ad1, 0))
                    tcc_error("expected type");
                type_decl(&t1, &ad1, &n, TYPE_ABSTRACT);
                skip(')');
                int res = 0;
                if (is_isSigned) res = type_is_signed(&t1);
                else if (is_isUnsigned) res = type_is_unsigned(&t1);
                else if (is_isConst) res = type_is_const(&t1);
                else if (is_isVolatile) res = type_is_volatile(&t1);
                else if (is_isImmutable) res = type_is_immutable(&t1);
                vpushi(res);
            } else {
                CType t1;
                AttrDef ad1 = {0};
                if (!parse_btype(&t1, &ad1, 0))
                    tcc_error("expected type");
                type_decl(&t1, &ad1, &n, TYPE_ABSTRACT);
                if (tok == TOK_EQ) {
                    next();
                    int kind = -1;
                    if (tok == TOK_STRUCT) { kind = 0; next(); }
                    else if (tok == TOK_UNION) { kind = 1; next(); }
                    else if (tok == TOK_ENUM) { kind = 2; next(); }
                    else {
                        const char *nm = get_tok_str(tok, NULL);
                        if (!strcmp(nm, "pointer")) kind = 3;
                        else if (!strcmp(nm, "array")) kind = 4;
                        else if (!strcmp(nm, "slice")) kind = 5;
                        else if (!strcmp(nm, "function")) kind = 6;
                        else if (!strcmp(nm, "void")) kind = 7;
                        else if (!strcmp(nm, "int") || !strcmp(nm, "integer") || !strcmp(nm, "char") || !strcmp(nm, "short") || !strcmp(nm, "long")) kind = 8;
                        else if (!strcmp(nm, "float") || !strcmp(nm, "double")) kind = 9;
                        next();
                    }
                    skip(')');
                    vpushi(type_is(&t1, kind));
                } else if (tok == ',') {
                    next();
                    CType t2;
                    AttrDef ad2 = {0};
                    if (!parse_btype(&t2, &ad2, 0))
                        tcc_error("expected type");
                    type_decl(&t2, &ad2, &n, TYPE_ABSTRACT);
                    skip(')');
                    vpushi((t1.t == t2.t) && (t1.ref == t2.ref));
                } else {
                    skip(')');
                    vpushi(1);
                }
            }
            break;
        }
        if (tcc_state->lang_d && tok == '('
            && (!strcmp(get_tok_str(t, NULL), "$nameof")
                || !strcmp(get_tok_str(t, NULL), "$offsetof")
                || !strcmp(get_tok_str(t, NULL), "$valueof"))) {
            int want_name = !strcmp(get_tok_str(t, NULL), "$nameof");
            int want_value = !strcmp(get_tok_str(t, NULL), "$valueof");
            skip('(');
            expr_eq();
            if (!vtop->sym || !vtop->sym->asm_label) {
                /* not a symbol value: try as a normal expression */
            } else {
                Sym *sv = vtop->sym;
                vpop();
                if (want_name) {
                    const char *nm = get_tok_str(sv->asm_label & ~SYM_FIELD, NULL);
                    {
                        Sym *ss = make_string_sym(nm);
                        vpushsym(&ss->type, ss);
                        /* decay array to pointer */
                        vtop->type.t = (vtop->type.t & ~VT_ARRAY) | VT_PTR;
                    }
                    /* make it a pointer */
                    vtop->type.t = VT_PTR;
                    /* adjust: make_string_sym returns array; decay handled by vpushsym? */
                } else {
                    vpushi(sv->c);
                }
                skip(')');
                break;
            }
            skip(')');
            break;
        }
        s = sym_find(t);
        if (!s || IS_ASM_SYM(s)) {
            const char *name = get_tok_str(t, NULL);
            /* '$' denotes the length of the subscripted aggregate */
            if (nb_dollar_ctx && !strcmp(name, "$")) {
                gen_dollar();
                break;
            }
            if (tcc_state->lang_d) {
                Sym *es = struct_find(t);
                if (es && IS_ENUM(es->type.t)) {
                    vset(&es->type, VT_CONST, 0);
                    vtop->sym = es;
                    break;
                }
            }
            if (tok != '(')
                tcc_error("'%s' undeclared", name);

            /* for simple function calls, we tolerate undeclared
               external reference to int() function */
            if (!func_old)
            tcc_warning_c(warn_implicit_function_declaration)(
                "implicit declaration of function '%s'", name);
            s = external_global_sym(t, &func_old_type);
        }
        if (s && s->v < SYM_FIRST_ANOM
            && (s->sym_scope == 0 || (s->type.t & (VT_STORAGE_STATIC | VT_STORAGE_EXTERN | VT_STORAGE_TLS)))
            && (s->type.t & VT_BTYPE) != VT_FUNC && !IS_ENUM_VAL(s->type.t) && !(s->type.t & VT_STORAGE_TYPEDEF)) {
            if (IS_PURE(cur_scope->fn.pure)) {
                tcc_error("global variable '%s' not allowed in @pure scope", get_tok_str(s->v, NULL));
            } else if (cur_scope->fn.pure == FUNC_PURITY_UNKNOWN) {
                cur_scope->fn.pure = FUNC_PURITY_INFERRED_FALSE;
            }
        }
        r = s->r;
        /* A symbol that has a register is a local register variable,
           which starts out as `VT_LOCAL` value.  */
        if ((r & VT_VALMASK) < VT_CONST)
            r = (r & ~VT_VALMASK) | VT_LOCAL;

        vset(&s->type, r, s->c);
        /* `Point` to s as backpointer (even without r&`VT_SYM`).
	   `Will` be used by at least the x86 inline asm parser for
	   regvars.  */
	vtop->sym = s;

        if (r & VT_SYM) {
            vtop->c.i = 0;
#ifdef TCC_TARGET_PE
            if (s->a.dllimport) {
                mk_pointer(&vtop->type);
                vtop->r |= VT_LVAL;
                indir();
            }
#endif
        } else if (r == VT_CONST && IS_ENUM_VAL(s->type.t)) {
            vtop->c.i = s->enum_val;
        }
        break;
    }

    /* post operations */
    while (1) {
        if (tok == TOK_INC || tok == TOK_DEC) {
            inc(1, tok);
            next();
        } else if (tok == '.' || tok == TOK_ARROW) {
            int qualifiers, cumofs;
            int is_arrow = tok == TOK_ARROW;
            Sym *ms;
            if (!is_arrow && (vtop->type.t & VT_BTYPE) == VT_PTR) {
                CType *pt = pointed_type(&vtop->type);
                if (pt && (pt->t & VT_BTYPE) == VT_STRUCT) {
                    is_arrow = 1;
                }
            }
            if (is_arrow)
                indir();
            qualifiers = vtop->type.t & VT_QUALIFIER;
            /* expect pointer on structure */
            next();
            if (tcc_state->lang_d && tok >= TOK_UIDENT
                && (!strcmp(get_tok_str(tok, NULL), "$nameof")
                    || !strcmp(get_tok_str(tok, NULL), "$offsetof")
                    || !strcmp(get_tok_str(tok, NULL), "$valueof"))) {
                int want_name = !strcmp(get_tok_str(tok, NULL), "$nameof");
                next(); /* consume */
                if (!vtop->sym || !vtop->sym->asm_label) {
                    /* not a symbol value */
                } else {
                    Sym *sv = vtop->sym;
                    if (want_name) {
                        const char *nm = get_tok_str(sv->asm_label & ~SYM_FIELD, NULL);
                        Sym *ss = make_string_sym(nm);
                        vpop();
                        vpushsym(&ss->type, ss);
                        vtop->type.t = (vtop->type.t & ~VT_ARRAY) | VT_PTR;
                    } else {
                        int val = sv->c;
                        vpop();
                        vpushi(val);
                    }
                    continue;
                }
            }
            if (tcc_state->lang_d && (vtop->type.t & VT_ENUM)
                && tok >= TOK_UIDENT
                && strcmp(get_tok_str(tok, NULL), "$nameof")
                && strcmp(get_tok_str(tok, NULL), "$offsetof")
                && strcmp(get_tok_str(tok, NULL), "$valueof")) {
                Sym *es = vtop->type.ref;
                int member_tok = tok;
                if (member_tok < TOK_UIDENT)
                    expect("enum member identifier");
                next();
                ms = es ? es->next : NULL;
                while (ms && ms->v != member_tok)
                    ms = ms->next;
                if (!ms)
                    tcc_error("'%s' is not a member of enum", get_tok_str(member_tok, NULL));
                vpop();
                vpushi(ms->enum_val);
                vtop->type = ms->type;
                vtop->sym = ms;
                continue;
            }
            if (tcc_state->lang_d && tok == TOK_SIZEOF) {

                next();
                {
                    CType expr_type_val = vtop->type;
                    if ((expr_type_val.t & VT_BTYPE) == VT_AUTO && expr_type_val.ref) {
                        CType *tv = type_value_of(expr_type_val.ref);
                        if (tv)
                            expr_type_val = *tv;
                    }
                    vpop();
                    vpush_type_size(&expr_type_val, &align);
                    gen_cast_s(VT_SIZE_T);
                }
                continue;
            }
            if (tcc_state->lang_d && tok >= TOK_UIDENT
                && (!strcmp(get_tok_str(tok, NULL), "$nameof")
                    || !strcmp(get_tok_str(tok, NULL), "$offsetof")
                    || !strcmp(get_tok_str(tok, NULL), "$valueof"))) {
                int want_name = !strcmp(get_tok_str(tok, NULL), "$nameof");
                next(); /* consume */
                if (!vtop->sym || !vtop->sym->asm_label) {
                    /* not a symbol value; leave as-is */
                } else {
                    Sym *sv = vtop->sym;
                    vpop();
                    if (want_name) {
                        const char *nm = get_tok_str(sv->asm_label & ~SYM_FIELD, NULL);
                        Sym *ss = make_string_sym(nm);
                        vpushsym(&ss->type, ss);
                        vtop->type.t = VT_PTR;
                    } else {
                        vpushi(sv->c);
                    }
                }
                continue;
            }
            if (!is_arrow && (is_slice_type(&vtop->type)
                              || (vtop->type.t & (VT_ARRAY | VT_VLA)))) {
                CType elem, slice_type;
                int esize, ealign, is_arr;
                int is_len = tok == TOK_length;
                int is_dup = tok == TOK_dup;
                int is_idup = tok == TOK_idup;
                int is_resize = tok == TOK_resize;

                is_arr = vtop->type.t & (VT_ARRAY | VT_VLA);
                if (is_arr) {
                    if (!is_dup && !is_idup)
                        tcc_error("'.%s' requires a D slice",
                                  is_len ? "length" : "resize");
                    elem = vtop->type.ref->type;
                    if (is_idup)
                        elem.t |= VT_QUALIFIER_CONSTANT;
                    slice_type.t = VT_STRUCT;
                    slice_type.ref = make_slice_type(&elem);
                } else {
                    elem = *pointed_type(&vtop->type.ref->next->next->type);
                    slice_type = vtop->type;
                    if (is_idup) {
                        elem.t |= VT_QUALIFIER_CONSTANT;
                        slice_type.ref = make_slice_type(&elem);
                    }
                }
                esize = type_size(&elem, &ealign);
                if (is_len) {
                    /* '.length' is the plain header field; use
                       '.resize(n)' to reallocate the elements.  */
                    next();
                    vtop->type.t = VT_SIZE_T;
                    vtop->type.ref = NULL;
                    continue;
                } else if (is_resize) {
                    next();
                    vpush_helper_func(TOK___darray_resize);
                    vpushv(vtop - 1);
                    gaddrof();
                    vtop->type = char_pointer_type;
                    vpushs(esize);
                    skip('(');
                    expr_eq();
                    skip(')');
                    gfunc_call(3);
                    continue;
                } else if (is_dup || is_idup) {
                    int size, align, l, r2;
                    next();
                    if (is_arr)
                        convert_array_to_slice(&slice_type);
                    size = type_size(&slice_type, &align);
                    l = get_temp_local_var(size, align, &r2);
                    vpush_helper_func(TOK___darray_dup);
                    vset(&char_pointer_type, VT_LOCAL, l);
                    vpushv(vtop - 2);
                    gaddrof();
                    vtop->type = char_pointer_type;
                    vpushs(esize);
                    gfunc_call(3);
                    vpop();
                    vset(&slice_type, VT_LOCAL | VT_LVAL, l);
                    vtop->r2 = r2;
                    continue;
                }
            }
            test_lvalue();
            s = NULL;
            if ((vtop->type.t & VT_BTYPE) == VT_STRUCT) {
                s = find_field_opt(&vtop->type, tok, &cumofs);
            }
            if (!s) {
                Sym *fs = sym_find(tok);
                if (fs && (fs->type.t & VT_BTYPE) == VT_FUNC) {
                    vpushsym(&fs->type, fs);
                    vswap();
                    next();
                    if (tok != '(')
                        expect("(' after UFCS function call");
                    {
                        SValue ret;
                        Sym *sa;
                        int nb_args, ret_nregs, ret_align, regsize, variadic;
                        TokenString *p, *p2;

                        s = vtop[-1].type.ref;
                        if (cur_scope->fn.safety == FUNC_SAFETY_SAFE && s && (s->f.func_safety == FUNC_SAFETY_SYSTEM || s->f.func_safety == FUNC_SAFETY_INFERRED_SYSTEM))
                            tcc_error("calling @system function not allowed in @safe scope");
                        vtop[-1].r &= ~VT_LVAL;
                        next();
                        sa = s->next ? s->next->next : NULL;
                        nb_args = 1;
                        ret.r2 = VT_CONST;
                        if ((s->type.t & VT_BTYPE) == VT_STRUCT) {
                            variadic = (s->f.func_type == FUNC_ELLIPSIS);
                            ret_nregs = gfunc_sret(&s->type, variadic, &ret.type,
                                                   &ret_align, &regsize);
                            if (ret_nregs <= 0) {
                                int size, align;
                                size = type_size(&s->type, &align);
                                loc = (loc - size) & -align;
                                ret.type = s->type;
                                ret.r = VT_LOCAL | VT_LVAL;
                                vseti(VT_LOCAL, loc);
                                ret.c = vtop->c;
                            }
                        } else {
                            ret_nregs = 1;
                            ret.type = s->type;
                        }

                        if (ret_nregs > 0) {
                            ret.c.i = 0;
                            PUT_R_RET(&ret, ret.type.t);
                        }

                        p = NULL;
                        if (tok != ')') {
                            int r = tcc_state->reverse_funcargs;
                            for(;;) {
                                if (r) {
                                    skip_or_save_block(&p2);
                                    p2->prev = p, p = p2;
                                } else {
                                    call_arg_expr(sa);
                                    gfunc_param_typed(s, sa);
                                }
                                nb_args++;
                                if (sa)
                                    sa = sa->next;
                                if (tok == ')')
                                    break;
                                skip(',');
                            }
                        }
                        if (sa)
                            tcc_error("too few arguments to function");

                        if (p) {
                            int n;
                            for (n = 0; p; p = p2, ++n) {
                                p2 = p, sa = s->next ? s->next->next : NULL;
                                do {
                                    if (sa) sa = sa->next;
                                    p2 = p2->prev;
                                } while (p2 && sa);
                                p2 = p->prev;
                                begin_macro(p, 1), next();
                                call_arg_expr(sa);
                                gfunc_param_typed(s, sa);
                                end_macro();
                            }
                            vrev(n);
                        }

                        next();
                        vcheck_cmp();
                        gfunc_call(nb_args);

                        if (ret_nregs < 0) {
                            vsetc(&ret.type, ret.r, &ret.c);
#ifdef TCC_TARGET_RISCV64
                            arch_transfer_ret_regs(1);
#endif
                        } else {
                            vsetc(&ret.type, ret.r, &ret.c);
                            PUT_R_RET(vtop, ret.type.t);
                        }
                        goto skip_normal_next;
                    }
                } else if ((vtop->type.t & VT_BTYPE) == VT_STRUCT) {
                    s = find_field(&vtop->type, tok, &cumofs);
                } else {
                    expect("struct or union");
                }
            }
            /* add field offset to pointer */
            gaddrof();
            vtop->type = char_pointer_type; /* change type to 'char *' */
            vpushi(cumofs);
            gen_op('+');
            /* change type to field type, and set to lvalue */
            vtop->type = s->type;
            if (qualifiers)
                parse_btype_qualify(&vtop->type, qualifiers);
            /* an array is never an lvalue */
            if (!(vtop->type.t & VT_ARRAY)) {
                vtop->r |= VT_LVAL;
#ifdef CONFIG_TCC_BCHECK
                /* if bound checking, the referenced pointer must be checked */
                if (tcc_state->do_bounds_check)
                    vtop->r |= VT_MUSTBOUND;
#endif
            }
            next();
            skip_normal_next: ;
        } else if (tok == '[') {
            CType base_type, elem_type;
            SValue *base_sv;
            int base_is_slice;

            next();
            base_type = vtop->type;
            base_sv = vtop;
            if (is_tuple_type(&base_type)) {
                /* tuple element access: the same operation as a `.` field
                   access, with the field selected by constant index */
                TupleConst *tc = tuple_const_of(base_sv);
                int idx = expr_const();
                skip(']');
                if (tc) {
                    /* a constant tuple folds to the element's value */
                    if (idx < 0 || idx >= tc->n) {
                        char buf[256];
                        type_to_str(buf, sizeof buf, &base_type, NULL);
                        tcc_error("tuple index %d out of range for type '%s'",
                                  idx, buf);
                    }
                    tuple_const_push(tc, idx);
                    vswap();
                    vpop();
                } else {
                    gen_tuple_field(&base_type, idx);
                }
                continue;
            }
            base_is_slice = is_slice_type(&base_type);
            if ((base_type.t & VT_BTYPE) == VT_PTR) {
                if (cur_scope->fn.safety == FUNC_SAFETY_SAFE)
                    tcc_error("pointer indexing not allowed in @safe scope");
                else {
                    if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                        cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
                    if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                        root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
                }
            }
            if (tok == TOK_TWODOTS || tok == TOK_DOTS
                || (tcc_state->lang_d && tok == ']')) {
                /* full slice: a[..], or a[] in D mode */
                if (nocode_wanted & DATA_ONLY_WANTED)
                    tcc_error("slice expression is not computable at load time");
                if (tok != ']')
                    next();
                skip(']');
                if (base_is_slice) {
                    copy_slice_value();
                } else if (base_type.t & (VT_ARRAY | VT_VLA)) {
                    CType slice_type, elem_type;
                    slice_type.t = VT_STRUCT;
                    elem_type = base_type.ref->type;
                    elem_type.t |= base_type.t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
                    slice_type.ref = make_slice_type(&elem_type);
                    convert_array_to_slice(&slice_type);
                } else {
                    tcc_error("cannot slice a pointer without bounds");
                }
                slice_range_target = 1;
                continue;
            }

            /* make '$' refer to the length of this aggregate; the base
               value is still intact here, before it is converted to a
               pointer below */
            if (nb_dollar_ctx ==
                (int)(sizeof dollar_ctx_stack / sizeof dollar_ctx_stack[0]))
                tcc_error("subscript expressions nested too deeply");
            dollar_ctx_stack[nb_dollar_ctx].type = base_type;
            dollar_ctx_stack[nb_dollar_ctx].sv = *base_sv;
            ++nb_dollar_ctx;

            if (base_is_slice) {
                elem_type = *pointed_type(&base_type.ref->next->next->type);
                /* pointees of a qualified slice are qualified as well */
                elem_type.t |= base_type.t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
                slice_to_ptr();
            } else if (base_type.t & (VT_ARRAY | VT_VLA)) {
                elem_type = base_type.ref->type;
                elem_type.t |= base_type.t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
            } else if ((base_type.t & VT_BTYPE) == VT_PTR) {
                elem_type = *pointed_type(&base_type);
            } else {
                elem_type = base_type;
            }

            gexpr();
            if (tok == TOK_TWODOTS || tok == TOK_DOTS) {
                /* range slice: a[lo..hi] */
                if (nocode_wanted & DATA_ONLY_WANTED)
                    tcc_error("slice expression is not computable at load time");
                next();
                gexpr();
                skip(']');
                --nb_dollar_ctx;
                /* [base, lo, hi] -> [base + lo, hi - lo] */
                vpushv(vtop - 1);   /* lo */
                gen_op('-');        /* hi - lo */
                vrott(3);           /* [len, base, lo] */
                gen_op('+');        /* [len, base + lo] */
                vswap();            /* [ptr, len] */
                gen_slice_value(&elem_type);
                slice_range_target = 1;
            } else {
                gen_op('+');
                indir();
                skip(']');
                --nb_dollar_ctx;
            }
        } else if (tok == '(') {
            SValue ret;
            Sym *sa;
            int nb_args, ret_nregs, ret_align, regsize, variadic;
            TokenString *p, *p2;

            /* function call  */
            if ((vtop->type.t & VT_BTYPE) != VT_FUNC) {
                /* pointer test (no array accepted) */
                if ((vtop->type.t & (VT_BTYPE | VT_ARRAY)) == VT_PTR) {
                    vtop->type = *pointed_type(&vtop->type);
                    if ((vtop->type.t & VT_BTYPE) != VT_FUNC)
                        goto error_func;
                } else {
                error_func:
                    expect("function pointer");
                }
            } else {
                vtop->r &= ~VT_LVAL; /* no lvalue */
            }
            /* get return type */
            s = vtop->type.ref;
            if (cur_scope->fn.safety == FUNC_SAFETY_SAFE && s && (s->f.func_safety == FUNC_SAFETY_SYSTEM || s->f.func_safety == FUNC_SAFETY_INFERRED_SYSTEM))
                tcc_error("calling @system function not allowed in @safe scope");
            next();
            sa = s->next; /* first parameter */
            nb_args = regsize = 0;
            ret.r2 = VT_CONST;
            /* compute first implicit argument if a structure is returned */
            if ((s->type.t & VT_BTYPE) == VT_STRUCT) {
                variadic = (s->f.func_type == FUNC_ELLIPSIS);
                ret_nregs = gfunc_sret(&s->type, variadic, &ret.type,
                                       &ret_align, &regsize);
                if (ret_nregs <= 0) {
                    /* get some space for the returned structure */
                    size = type_size(&s->type, &align);
#ifdef TCC_TARGET_ARM64
                /* `On` arm64, a small struct is return in registers.
                   `It` is much easier to write it to memory if we know
                   that we are allowed to write some extra bytes, so
                   round the allocated space up to a power of 2: */
                if (size < 16)
                    while (size & (size - 1))
                        size = (size | (size - 1)) + 1;
#endif
                    loc = (loc - size) & -align;
                    ret.type = s->type;
                    ret.r = VT_LOCAL | VT_LVAL;
                    /* pass it as 'int' to avoid structure arg passing
                       problems */
                    vseti(VT_LOCAL, loc);
#ifdef CONFIG_TCC_BCHECK
                    if (tcc_state->do_bounds_check) {
                        /* the hidden return slot has no symbol, so register
                           it here for the checked copy that follows */
                        add_local_bound(loc, size);
                        --loc;
                    }
#endif
                    ret.c = vtop->c;
                    if (ret_nregs < 0)
                      vtop--;
                    else
                      nb_args++;
                }
            } else {
                ret_nregs = 1;
                ret.type = s->type;
            }

            if (ret_nregs > 0) {
                /* return in register */
                ret.c.i = 0;
                PUT_R_RET(&ret, ret.type.t);
            }

            p = NULL;
            if (tok != ')') {
                r = tcc_state->reverse_funcargs;
                for(;;) {
                    if (r) {
                        skip_or_save_block(&p2);
                        p2->prev = p, p = p2;
                    } else {
                        call_arg_expr(sa);
                        gfunc_param_typed(s, sa);
                    }
                    nb_args++;
                    if (sa)
                        sa = sa->next;
                    if (tok == ')')
                        break;
                    skip(',');
                }
            }
            if (sa)
                tcc_error("too few arguments to function");

            if (p) { /* with `reverse_funcargs` */
                for (n = 0; p; p = p2, ++n) {
                    p2 = p, sa = s;
                    do {
                        sa = sa->next, p2 = p2->prev;
                    } while (p2 && sa);
                    p2 = p->prev;
                    begin_macro(p, 1), next();
                    call_arg_expr(sa);
                    gfunc_param_typed(s, sa);
                    end_macro();
                }
                vrev(n);
            }

            next();
            vcheck_cmp(); /* the generators don't like `VT_CMP` on vtop */
            gfunc_call(nb_args);

            if (ret_nregs < 0) {
                vsetc(&ret.type, ret.r, &ret.c);
#ifdef TCC_TARGET_RISCV64
                arch_transfer_ret_regs(1);
#endif
            } else {
                /* return value */
                n = ret_nregs;
                while (n > 1) {
                    int rc = reg_classes[ret.r] & ~(RC_INT | RC_FLOAT);
                    /* `We` assume that when a structure is returned in multiple
                       registers, their classes are consecutive values of the
                       suite s(n) = 2^n */
                    rc <<= --n;
                    for (r = 0; r < NB_REGS; ++r)
                        if (reg_classes[r] & rc)
                            break;
                    vsetc(&ret.type, r, &ret.c);
                }
                vsetc(&ret.type, ret.r, &ret.c);
                vtop->r2 = ret.r2;

                /* handle packed struct return */
                if (((s->type.t & VT_BTYPE) == VT_STRUCT) && ret_nregs) {
                    int addr, offset;

                    size = type_size(&s->type, &align);
                    /* `We`'re writing whole regs often, make sure there's enough
                       space.  `Assume` register size is power of 2.  */
                    size = (size + regsize - 1) & -regsize;
                    if (ret_align > align)
                        align = ret_align;
                    loc = (loc - size) & -align;
                    addr = loc;
#ifdef CONFIG_TCC_BCHECK
                    if (tcc_state->do_bounds_check)
                        add_local_bound(addr, size);
#endif
                    offset = 0;
                    for (;;) {
                        vset(&ret.type, VT_LOCAL | VT_LVAL, addr + offset);
                        vswap();
                        vstore();
                        vtop--;
                        if (--ret_nregs == 0)
                          break;
                        offset += regsize;
                    }
                    vset(&s->type, VT_LOCAL | VT_LVAL, addr);
                }

                /* `Promote` char/short return values. `This` is matters only
                   for calling function that were not compiled by `TPC` and
                   only on some architectures.  `For` those where it doesn't
                   matter we expect things to be already promoted to int,
                   but not larger.  */
                t = s->type.t & VT_BTYPE;
                if (t == VT_BYTE || t == VT_SHORT || t == VT_BOOL) {
#ifdef PROMOTE_RET
                    vtop->r |= BFVAL(VT_MUSTCAST, 1);
#else
                    vtop->type.t = VT_INT;
#endif
                }
            }
            if (s->f.func_noreturn) {
                if (debug_modes)
	            tcc_tcov_block_end(tcc_state, -1);
                CODE_OFF();
	    }
        } else {
            break;
        }
    }
}

# define expr_landor_next(op) unary(), expr_infix(precedence(op) + 1)
# define expr_lor() unary(), expr_infix(1)

static int precedence(int tok)
{
    switch (tok) {
        case TOK_LOR: return 1;
        case TOK_LAND: return 2;
	case '|': return 3;
	case '^': return 4;
	case '&': return 5;
	case TOK_EQ: case TOK_NE: return 6;
 relat: case TOK_ULT: case TOK_UGE: return 7;
	case TOK_SHL: case TOK_SAR: return 8;
	case '+': case '-': case '~': return 9;
	case '*': case '/': case '%': return 10;
	default:
	    if (tok >= TOK_ULE && tok <= TOK_GT)
	        goto relat;
	    return 0;
    }
}
static TCC_TLS unsigned char prec[256];
static void init_prec(void)
{
    int i;
    for (i = 0; i < 256; i++)
	prec[i] = precedence(i);
}
#define precedence(i) ((unsigned)i < 256 ? prec[i] : 0)

static void expr_landor(int op);

static void expr_infix(int p)
{
    int t = tok, p2;
    while ((p2 = precedence(t)) >= p) {
        if (t == TOK_LOR || t == TOK_LAND) {
            expr_landor(t);
        } else {
            next();
            unary();
            if (precedence(tok) > p2)
              expr_infix(p2 + 1);
            if (t == '~')
                gen_slice_concat();
            else
                gen_op(t);
        }
        t = tok;
    }
}


/* `Assuming` vtop is a value used in a conditional context
   (i.e. compared with zero) return 0 if it's false, 1 if
   true and -1 if it can't be statically determined.  */
static int condition_3way(void)
{
    int c = -1;
    if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST &&
	(!(vtop->r & VT_SYM) || !vtop->sym->a.weak)) {
	vdup();
        gen_cast_s(VT_BOOL);
	c = vtop->c.i;
	vpop();
    }
    return c;
}

static void expr_landor(int op)
{
    int t = 0, cc = 1, f = 0, i = op == TOK_LAND, c;
    for(;;) {
        c = f ? i : condition_3way();
        if (c < 0)
            save_regs(1), cc = 0;
        else if (c != i)
            nocode_wanted++, f = 1;
        if (tok != op)
            break;
        if (c < 0)
            t = gvtst(i, t);
        else
            vpop();
        next();
        expr_landor_next(op);
    }
    if (cc || f) {
        vpop();
        vpushi(i ^ f);
        gsym(t);
        nocode_wanted -= f;
    } else {
        gvtst_set(i, t);
    }
}

static int is_cond_bool(SValue *sv)
{
    if ((sv->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
        && (sv->type.t & VT_BTYPE) == VT_INT)
        return (unsigned)sv->c.i < 2;
    if (sv->r == VT_CMP)
        return 1;
    return 0;
}

static void expr_cond(void)
{
    int tt, u, r1, r2, rc, t1, t2, islv, c, g;
    SValue sv;
    CType type;

    expr_lor();
    if (tok == '?') {
        next();
	c = condition_3way();
        g = (tok == ':' && gnu_ext);
        tt = 0;
        if (!g) {
            if (c < 0) {
                save_regs(1);
                tt = gvtst(1, 0);
            } else {
                vpop();
            }
        } else if (c < 0) {
            /* needed to avoid having different registers saved in
               each branch */
            save_regs(1);
            gv_dup();
            tt = gvtst(0, 0);
        }

        if (c == 0)
          nocode_wanted++;
        if (!g)
          gexpr();

        if ((vtop->type.t & VT_BTYPE) == VT_FUNC)
          mk_pointer(&vtop->type);
        sv = *vtop; /* save value to handle it later */
        vtop--; /* no vpop so that `FP` stack is not flushed */

        if (g) {
            u = tt;
        } else if (c < 0) {
            u = gjmp(0);
            gsym(tt);
        } else
          u = 0;

        if (c == 0)
          nocode_wanted--;
        if (c == 1)
          nocode_wanted++;
        skip(':');
        expr_cond();

        if ((vtop->type.t & VT_BTYPE) == VT_FUNC)
          mk_pointer(&vtop->type);

        /* cast operands to correct type according to `ISOC` rules */
        if (!combine_types(&type, &sv, vtop, '?'))
          type_incompatibility_error(&sv.type, &vtop->type,
            "type mismatch in conditional expression (have '%s' and '%s')");

        if (c < 0 && is_cond_bool(vtop) && is_cond_bool(&sv)) {
            /* optimize "if (f ? a > b : c || d) ..." for example, where normally
               "a < b" and "c || d" would be forced to "(int)0/1" first, whereas
               this code jumps directly to the if's then/else branches. */
            t1 = gvtst(0, 0);
            t2 = gjmp(0);
            gsym(u);
            vpushv(&sv);
            /* combine jump targets of 2nd op with `VT_CMP` of 1st op */
            gvtst_set(0, t1);
            gvtst_set(1, t2);
            gen_cast(&type);
            //  tcc_warning("two conditions expr_cond");
            return;
        }

        /* keep structs lvalue by transforming `(expr ? a : b)` to `*(expr ? &a : &b)` so
           that `(expr ? a : b).mem` does not error  with "lvalue expected" */
        islv = VT_STRUCT == (type.t & VT_BTYPE);

        /* now we convert second operand */
        if (c != 1) {
            gen_cast(&type);
            if (islv) {
                mk_pointer(&vtop->type);
                gaddrof();
            }
        }

        rc = RC_TYPE(type.t);
        /* for long longs, we use fixed registers to avoid having
           to handle a complicated move */
        if (USING_TWO_WORDS(type.t))
          rc = RC_RET(type.t);

        tt = r2 = 0;
        if (c < 0) {
            if (type.t != VT_VOID)
                r2 = gv(rc);
            tt = gjmp(0);
        }
        gsym(u);
        if (c == 1)
          nocode_wanted--;

        /* this is horrible, but we must also convert first
           operand */
        if (c != 0) {
            *vtop = sv;
            gen_cast(&type);
            if (islv) {
                mk_pointer(&vtop->type);
                gaddrof();
            }
        }

        if (c < 0) {
            if (type.t != VT_VOID) {
                r1 = gv(rc);
                move_reg(r2, r1, islv ? VT_PTR : type.t);
                vtop->r = r2;
            }
            gsym(tt);
        }

        if (islv)
          indir();
    }
}

static void expr_eq(void)
{
    int t;

    if (tok == '(' && try_tuple_assign())
        return;
    slice_range_target = 0;
    expr_cond();
    if ((t = tok) == '=' || TOK_ASSIGN(t)) {
        if (is_slice_type(&vtop->type) && slice_range_target) {
            slice_range_target = 0;
            gen_slice_assign(t);
            return;
        }
        slice_range_target = 0;
        test_lvalue();
        if (t == TOK_A_CAT) {
            gen_slice_append();
            return;
        }
        next();
        {
            CType *saved_expect = tuple_expect;
            if (is_tuple_type(&vtop->type))
                tuple_expect = &vtop->type;
            if (t == '=') {
                expr_eq();
            } else {
                vdup();
                expr_eq();
                gen_op(TOK_ASSIGN_OP(t));
            }
            tuple_expect = saved_expect;
        }
        vstore();
    }
}

ST_FUNC void gexpr(void)
{
    expr_eq();
    if (tok == ',') {
        do {
            vpop();
            next();
            expr_eq();
        } while (tok == ',');

        /* convert array & function to pointer */
        convert_expression_type(&vtop->type);

        /* make `builtin_constant_p`((1,2)) return 0 (like on gcc) */
        if ((vtop->r & VT_VALMASK) == VT_CONST && nocode_wanted && !CONST_WANTED)
            if (vtop->type.t != VT_VOID && (vtop->type.t & VT_BTYPE) != VT_STRUCT)
                gv(RC_TYPE(vtop->type.t));
    }
}

/* parse a constant expression and return value in vtop.  */
static void expr_const1(void)
{
    nocode_wanted += CONST_WANTED_BIT;
    expr_cond();
    nocode_wanted -= CONST_WANTED_BIT;
}

/* parse an integer constant and return its value. */
static inline int64_t expr_const64(void)
{
    int64_t c;
    expr_const1();
    if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_NONCONST)) != VT_CONST)
        expect("constant expression");
    c = vtop->c.i;
    vpop();
    return c;
}

/* parse an integer constant and return its value.
   `Complain` if it doesn't fit 32bit (signed or unsigned).  */
ST_FUNC int expr_const(void)
{
    int c;
    int64_t wc = expr_const64();
    c = wc;
    if (c != wc && (unsigned)c != wc)
        tcc_error("constant exceeds 32 bit");
    return c;
}

/* ------------------------------------------------------------------------- */
/* return from function */

#ifndef TCC_TARGET_ARM64
static void gfunc_return(CType *func_type)
{
    if ((func_type->t & VT_BTYPE) == VT_STRUCT) {
        CType type, ret_type;
        int ret_align, ret_nregs, regsize;
        ret_nregs = gfunc_sret(func_type, func_var, &ret_type,
                               &ret_align, &regsize);
        if (ret_nregs < 0) {
#ifdef TCC_TARGET_RISCV64
            arch_transfer_ret_regs(0);
#endif
        } else if (0 == ret_nregs) {
            /* if returning structure, must copy it to implicit
               first pointer arg location */
            type = *func_type;
            mk_pointer(&type);
            vset(&type, VT_LOCAL | VT_LVAL, func_vc);
            indir();
            vswap();
            /* copy structure value to pointer */
            vstore();
        } else {
            /* returning structure packed into registers */
            int size, addr, align, rc, n;
            size = type_size(func_type,&align);
            if (ret_nregs * regsize > size ||
		((align & (ret_align - 1))
                 && ((vtop->r & VT_VALMASK) < VT_CONST /* pointer to struct */
                     || (vtop->c.i & (ret_align - 1))
                     ))) {
		if (ret_nregs * regsize > size)
		    size = ret_nregs * regsize;
		if (ret_align > align)
		    align = ret_align;
                loc = (loc - size) & -align;
                addr = loc;
                type = *func_type;
                vset(&type, VT_LOCAL | VT_LVAL, addr);
                vswap();
                vstore();
                vpop();
                vset(&ret_type, VT_LOCAL | VT_LVAL, addr);
            }
            vtop->type = ret_type;
            rc = RC_RET(ret_type.t);
            //printf("struct return: n:%d t:%02x rc:%02x\n", ret_nregs, ret_type.t, rc);
            for (n = ret_nregs; --n > 0;) {
                vdup();
                gv(rc);
                vswap();
                incr_offset(regsize);
                /* `We` assume that when a structure is returned in multiple
                   registers, their classes are consecutive values of the
                   suite s(n) = 2^n */
                rc <<= 1;
            }
            gv(rc);
            vtop -= ret_nregs - 1;
        }
    } else {
        gv(RC_RET(func_type->t));
    }
    vtop--; /* `NOT` vpop() because on x86 it would flush the fp stack */
}
#endif

static void check_func_return(void)
{
    if ((func_vt.t & VT_BTYPE) == VT_VOID)
        return;
    if ((!strcmp(funcname, "main") || func_old)
        && (func_vt.t & VT_BTYPE) == VT_INT) {
        /* main returns 0 by default */
        vpushi(0);
        gfunc_return(&func_vt);
    } else {
        tcc_warning("function might return no value: '%s'", funcname);
    }
}

/* ------------------------------------------------------------------------- */
/* switch/case */

static int case_cmp(uint64_t a, uint64_t b)
{
    if (cur_switch->sv.type.t & VT_UNSIGNED)
        return a < b ? -1 : a > b;
    else
        return (int64_t)a < (int64_t)b ? -1 : (int64_t)a > (int64_t)b;
}

static int case_cmp_qs(const void *pa, const void *pb)
{
    return case_cmp((*(struct case_t**)pa)->v1, (*(struct case_t**)pb)->v1);
}

static void case_sort(struct switch_t *sw)
{
    struct case_t **p;
    if (sw->n < 2)
        return;
    qsort(sw->p, sw->n, sizeof *sw->p, case_cmp_qs);
    p = sw->p;
    while (p < sw->p + sw->n - 1) {
        if (case_cmp(p[0]->v2, p[1]->v1) >= 0) {
            int l1 = p[0]->line, l2 = p[1]->line;
            /* using special format "%i:..." to show specific line */
            tcc_error("%i:duplicate case value", l1 > l2 ? l1 : l2);
        } else if (p[0]->v2 + 1 == p[1]->v1 && p[0]->ind == p[1]->ind) {
            /* treat "case 1: case 2: case 3:" like "case 1 ... 3: */
            p[1]->v1 = p[0]->v1;
            tcc_free(p[0]);
            memmove(p, p + 1, (--sw->n - (p - sw->p)) * sizeof *p);
        } else
            ++p;
    }
}

static int gcase(struct case_t **base, int len, int dsym)
{
    struct case_t *p;
    int t, l2, e;

    t = vtop->type.t & VT_BTYPE;
    if (t != VT_LLONG)
        t = VT_INT;
    while (len) {
        /* binary search while len > 8, else linear */
        l2 = len > 8 ? len/2 : 0;
        p = base[l2];
        vdup(), vpush64(t, p->v2);
        if (l2 == 0 && p->v1 == p->v2) {
            gen_op(TOK_EQ); /* jmp to case when equal */
            gsym_addr(gvtst(0, 0), p->ind);
        } else {
            /* case v1 ... v2 */
            gen_op(TOK_GT); /* jmp over when > `V2` */
            if (len == 1) /* last case test jumps to default when false */
                dsym = gvtst(0, dsym), e = 0;
            else
                e = gvtst(0, 0);
            vdup(), vpush64(t, p->v1);
            gen_op(TOK_GE); /* jmp to case when >= `V1` */
            gsym_addr(gvtst(0, 0), p->ind);
            dsym = gcase(base, l2, dsym);
            gsym(e);
        }
        ++l2, base += l2, len -= l2;
    }
    /* jump automagically will suppress more jumps */
    return gjmp(dsym);
}

static void end_switch(void)
{
    struct switch_t *sw = cur_switch;
    dynarray_reset(&sw->p, &sw->n);
    cur_switch = sw->prev;
    tcc_free(sw);
}

/* ------------------------------------------------------------------------- */
/* `__attribute__`((cleanup(fn))) */

/* protect symbol lvalues from further modification  */
static void save_lvalues(void)
{
    SValue *sv = vtop;
    while (sv >= vstack) {
        if (sv->sym && (sv->r & VT_LVAL)) {
            int align, size = type_size(&sv->type, &align);
            int r2, l = get_temp_local_var(size, align, &r2);
            vset(&sv->type, VT_LOCAL | VT_LVAL, l), vtop->r2 = r2;
            vpushv(sv), *sv = vtop[-1], vstore(), --vtop;
        }
        --sv;
    }
}

static void try_call_scope_cleanup(Sym *stop)
{
    Sym *cls = cur_scope->cl.s;
    for (; cls != stop; cls = cls->next) {
	Sym *fs = cls->cleanup_func;
	Sym *vs = cls->cleanup_sym;
        if (!vs) {
            TokenString *dstr = (TokenString *)fs;
            int saved_vtop_idx = vtop - vstack;
            begin_macro(dstr, 0);
            next();
            if (tok == '{') {
                block(0);
            } else {
                gexpr();
                vpop();
            }
            end_macro();
            vtop = vstack + saved_vtop_idx;
        } else {
            save_lvalues();
            vpushsym(&fs->type, fs);
            vset(&vs->type, vs->r, vs->c);
            vtop->sym = vs;
            mk_pointer(&vtop->type);
            gaddrof();
            gfunc_call(1);
        }
    }
}


static void try_call_cleanup_goto(Sym *cleanupstate)
{
    Sym *oc, *cc;
    int ocd, ccd;

    if (!cur_scope->cl.s)
	return;

    /* search `NCA` of both cleanup chains given parents and initial depth */
    ocd = cleanupstate ? cleanupstate->v & ~SYM_FIELD : 0;
    for (ccd = cur_scope->cl.n, oc = cleanupstate; ocd > ccd; --ocd, oc = oc->next)
      ;
    for (cc = cur_scope->cl.s; ccd > ocd; --ccd, cc = cc->next)
      ;
    for (; cc != oc; cc = cc->next, oc = oc->next, --ccd)
      ;

    try_call_scope_cleanup(cc);
}

/* call 'func' for each `__attribute__`((cleanup(func))) */
static void block_cleanup(Scope *o)
{
    int jmp = 0;
    Sym *g, **pg;
    for (pg = &pending_gotos; (g = *pg) && g->c > o->cl.n;) {
        if (g->cleanup_label->r & LABEL_FORWARD) {
            Sym *pcl = g->next;
            if (!jmp)
                jmp = gjmp(0);
            gsym(pcl->jnext);
            try_call_scope_cleanup(o->cl.s);
            pcl->jnext = gjmp(0);
            if (!o->cl.n)
                goto remove_pending;
            g->c = o->cl.n;
            pg = &g->prev;
        } else {
    remove_pending:
            *pg = g->prev;
            sym_free(g);
        }
    }
    gsym(jmp);
    try_call_scope_cleanup(o->cl.s);
}

/* ------------------------------------------------------------------------- */
/* `VLA` */

static void vla_restore(int loc)
{
    if (loc)
        gen_vla_sp_restore(loc);
}

static void vla_leave(Scope *o)
{
    Scope *c = cur_scope, *v = NULL;
    for (; c != o && c; c = c->prev)
      if (c->vla.num)
        v = c;
    if (v)
      vla_restore(v->vla.locorig);
}

/* ------------------------------------------------------------------------- */
/* local scopes */

static Scope *push_scope(void)
{
    Scope *o;

    if (scope_depth >= scope_depth_limit)
        tcc_error("too many nested scopes (max %d)", scope_depth_limit);
    o = &scope_stack[++scope_depth];
    o->prev = &scope_stack[scope_depth - 1];
    return o;
}

static Scope *new_scope(void)
{
    Scope *o = push_scope();

    /* copy and link previous scope */
    *o = *cur_scope;
    o->prev = &scope_stack[scope_depth - 1];
    cur_scope = o;
    cur_scope->vla.num = 0;

    /* record local declaration stack position */
    o->lstk = local_stack;
    o->llstk = local_label_stack;
    ++local_scope;

    if (pending_block_safety) {
        cur_scope->fn.safety = pending_block_safety;
        pending_block_safety = 0;
    }
    if (pending_block_pure) {
        cur_scope->fn.pure = pending_block_pure;
        pending_block_pure = 0;
    }
    return o;
}

/* a function frame does not inherit block state from its enclosing
   scope, only the enclosing scope as its parent */
static Scope *new_function_scope(Sym *sym)
{
    Scope *o = push_scope();

    memset(o, 0, sizeof *o);
    o->prev = &scope_stack[scope_depth - 1];
    if (sym->type.ref) {
        o->fn.vt = sym->type.ref->type;
        o->fn.variadic = sym->type.ref->f.func_type == FUNC_ELLIPSIS;
        o->fn.old_style = sym->type.ref->f.func_type == FUNC_OLD;
        o->fn.safety = sym->type.ref->f.func_safety;
        o->fn.pure = sym->type.ref->f.func_pure;
    } else {
        o->fn.vt = sym->type;
        o->fn.safety = FUNC_SAFETY_UNKNOWN;
        o->fn.pure = FUNC_PURITY_UNKNOWN;
    }
    o->fn.name = get_tok_str(sym->v, NULL);
    o->fn.ind = ind;
    cur_scope = o;
    return o;
}
static void prev_scope(Scope *o, int is_expr)
{
    if (o != cur_scope)
        tcc_error("internal error: unbalanced scope");

    vla_leave(o->prev);

    if (o->cl.s != o->prev->cl.s)
        block_cleanup(o->prev);

    if (debug_modes)
        tcc_debug_end_scope(o->lstk, !is_expr);

    /* pop locally defined labels */
    label_pop(&local_label_stack, o->llstk, is_expr);

    /* `In` the `is_expr` case (a statement expression is finished here),
       vtop might refer to symbols on the `local_stack`.  `Either` via the
       type or via vtop->sym.  `We` can't pop those nor any that in turn
       might be referred to.  `To` make it easier we don't roll back
       any symbols in that case; some upper level call to block() will
       do that.  `We` do have to remove such symbols from the lookup
       tables, though.  `sym_pop` will do that.  */

    /* pop locally defined symbols */
    sym_pop(&local_stack, o->lstk, is_expr);
    cur_scope = o->prev;
    --scope_depth;
    --local_scope;
}

/* leave a scope via break/continue(/goto) */
static void leave_scope(Scope *o)
{
    if (!o)
        return;
    try_call_scope_cleanup(o->cl.s);
    vla_leave(o);
}

/* short versiona for scopes with 'if/do/while/switch' which can
   declare only types (of struct/union/enum) */
static void new_scope_s(Scope *o)
{
    o->lstk = local_stack;
    ++local_scope;
}

static void prev_scope_s(Scope *o)
{
    sym_pop(&local_stack, o->lstk, 0);
    --local_scope;
}

/* ------------------------------------------------------------------------- */
/* call block from 'for do while' loops */

static void lblock(int *bsym, int *csym)
{
    Scope *lo = loop_scope, *co = cur_scope;
    int *b = co->bsym, *c = co->csym;
    if (csym) {
        co->csym = csym;
        loop_scope = co;
    }
    co->bsym = bsym;
    block(0);
    co->bsym = b;
    if (csym) {
        co->csym = c;
        loop_scope = lo;
    }
}

/* D-style foreach loop over an array or slice:

     foreach (v; a) ...          value only
     foreach (i, v; a) ...       index and value
     `foreach_reverse` (...) ...   iterate from the end

   `The` aggregate is evaluated once and converted to a slice, so that
   arrays and slices are handled uniformly.  */
static void do_foreach(int reverse)
{
    Scope *o;
    AttrDef ad, adbase, adbases[2];
    CType btype, type, agg_type, slice_type, elem_type, len_type, ptr_type;
    CType vtypes[2];
    int names[2], vaddr[2], nvars, explicit[2], i;
    int slice_addr, len_addr, ptr_addr, idx_addr;
    int size, align, a, b, c, d, e;

    o = new_scope();
    skip('(');

    nvars = 0;
    memset(&adbases[0], 0, sizeof(adbases));
    btype.t = VT_INT;
    btype.ref = NULL;

    for (;;) {
        memset(&adbase, 0, sizeof(adbase));
        if (is_type_start(tok)) {
            parse_btype(&btype, &adbase, 0);
            type = btype;
            ad = adbase;
            type_decl(&type, &ad, &names[nvars], TYPE_DIRECT);
            vtypes[nvars] = type;
            adbases[nvars] = adbase;
            explicit[nvars] = 1;
        } else {
            if (tok < TOK_UIDENT)
                expect("identifier");
            names[nvars] = tok;
            explicit[nvars] = 0;
            memset(&adbases[nvars], 0, sizeof(AttrDef));
            next();
        }
        ++nvars;
        if (tok != ',')
            break;
        if (nvars == 2)
            tcc_error("too many variables in 'foreach'");
        next();
    }
    skip(';');

    gexpr();

    if (tok == TOK_TWODOTS || tok == TOK_DOTS) {
        CType start_type, end_type;
        int start_addr, end_addr;

        if (nvars != 1)
            tcc_error("range 'foreach' requires a single loop variable");

        start_type = vtop->type;
        size = type_size(&start_type, &align);
        loc = (loc - size) & -align;
        start_addr = loc;
        vset(&start_type, VT_LOCAL | VT_LVAL, start_addr);
        vswap();
        vstore();
        vpop();

        next(); /* consume '..' */

        gexpr();
        skip(')');

        end_type = vtop->type;
        size = type_size(&end_type, &align);
        loc = (loc - size) & -align;
        end_addr = loc;
        vset(&end_type, VT_LOCAL | VT_LVAL, end_addr);
        vswap();
        vstore();
        vpop();

        if (!explicit[0])
            vtypes[0] = start_type;

        decl_initializer_alloc(&vtypes[0], &adbases[0], VT_LOCAL | VT_LVAL, 0,
                               names[0], 0);
        vaddr[0] = sym_find(names[0])->c;

        /* i = reverse ? end - 1 : start */
        {
            CType mut_vtype = vtypes[0];
            mut_vtype.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
            vset(&mut_vtype, VT_LOCAL | VT_LVAL, vaddr[0]);
            if (reverse) {
                vset(&end_type, VT_LOCAL | VT_LVAL, end_addr);
                vpushi(1);
                gen_op('-');
                gen_assign_cast(&mut_vtype);
            } else {
                vset(&start_type, VT_LOCAL | VT_LVAL, start_addr);
                gen_assign_cast(&mut_vtype);
            }
            vstore();
            vpop();
        }

        a = b = 0;
        c = gind();
        /* condition: forward: i < end; reverse: i >= start */
        vset(&vtypes[0], VT_LOCAL | VT_LVAL, vaddr[0]);
        if (reverse) {
            vset(&start_type, VT_LOCAL | VT_LVAL, start_addr);
            gen_op(TOK_GE);
        } else {
            vset(&end_type, VT_LOCAL | VT_LVAL, end_addr);
            gen_op(TOK_LT);
        }
        a = gvtst(1, 0);
        e = gjmp(0);
        d = gind();

        /* increment: ++i, or --i when iterating in reverse */
        {
            CType mut_vtype = vtypes[0];
            mut_vtype.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
            vset(&mut_vtype, VT_LOCAL | VT_LVAL, vaddr[0]);
            vset(&mut_vtype, VT_LOCAL | VT_LVAL, vaddr[0]);
            vpushi(1);
            gen_op(reverse ? '-' : '+');
            vstore();
            vpop();
        }
        gjmp_addr(c);
        gsym(e);

        lblock(&a, &b);
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
        prev_scope(o, 0);
        return;
    }

    skip(')');

    agg_type = vtop->type;
    if (is_slice_type(&agg_type)) {
        slice_type = agg_type;
    } else if (agg_type.t & (VT_ARRAY | VT_VLA)) {
        CType elem_type_arr = agg_type.ref->type;
        elem_type_arr.t |= agg_type.t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
        slice_type.t = VT_STRUCT;
        slice_type.ref = make_slice_type(&elem_type_arr);
        convert_array_to_slice(&slice_type);
    } else {
        tcc_error("'foreach' requires an array or slice");
    }

    elem_type = *pointed_type(&slice_type.ref->next->next->type);
    len_type = slice_type.ref->next->type;
    ptr_type = slice_type.ref->next->next->type;

    /* an array element is bound to a slice, as arrays are not
       assignable values in C */
    if (elem_type.t & (VT_ARRAY | VT_VLA)) {
        CType st;
        st.t = VT_STRUCT;
        st.ref = make_slice_type(&elem_type.ref->type);
        elem_type = st;
    }

    /* keep the slice in a temporary local so that both members can be
       loaded from it */
    slice_addr = copy_slice_to_temp(&slice_type);

    size = type_size(&len_type, &align);
    loc = (loc - size) & -align;
    len_addr = loc;
    size = type_size(&ptr_type, &align);
    loc = (loc - size) & -align;
    ptr_addr = loc;
    size = type_size(&len_type, &align);
    loc = (loc - size) & -align;
    idx_addr = loc;

    /* len = slice.length; ptr = slice.ptr */
    vset(&len_type, VT_LOCAL | VT_LVAL, slice_addr);
    vset(&len_type, VT_LOCAL | VT_LVAL, len_addr);
    vswap();
    vstore();
    vpop();
    vset(&ptr_type, VT_LOCAL | VT_LVAL, slice_addr + PTR_SIZE);
    vset(&ptr_type, VT_LOCAL | VT_LVAL, ptr_addr);
    vswap();
    vstore();
    vpop();

    /* declare the loop variables */
    for (i = 0; i < nvars; ++i) {
        if (!explicit[i])
            vtypes[i] = (nvars == 2 && i == 0) ? len_type : elem_type;
        decl_initializer_alloc(&vtypes[i], &adbases[i], VT_LOCAL | VT_LVAL, 0,
                               names[i], 0);
        vaddr[i] = sym_find(names[i])->c;
    }

    /* idx = reverse ? len : 0 */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    if (reverse)
        vset(&len_type, VT_LOCAL | VT_LVAL, len_addr);
    else
        vpushi(0);
    vstore();
    vpop();

    a = b = 0;
    c = gind();
    /* condition: idx < len, or idx > 0 when iterating in reverse */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    if (reverse) {
        vpushi(0);
        gen_op(TOK_GT);
    } else {
        vset(&len_type, VT_LOCAL | VT_LVAL, len_addr);
        gen_op(TOK_LT);
    }
    a = gvtst(1, 0);
    e = gjmp(0);
    d = gind();
    /* increment: ++idx, or --idx when iterating in reverse */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vpushi(1);
    gen_op(reverse ? '-' : '+');
    vstore();
    vpop();
    gjmp_addr(c);
    gsym(e);

    /* index variable */
    if (nvars == 2) {
        CType mut_vtype0 = vtypes[0];
        mut_vtype0.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
        vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
        if (reverse) {
            vpushi(1);
            gen_op('-');
        }
        gen_assign_cast(&mut_vtype0);
        vset(&mut_vtype0, VT_LOCAL | VT_LVAL, vaddr[0]);
        vswap();
        vstore();
        vpop();
    }

    /* value variable */
    i = nvars - 1;
    {
        CType mut_vtypei = vtypes[i];
        mut_vtypei.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
        vset(&ptr_type, VT_LOCAL | VT_LVAL, ptr_addr);
        vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
        if (reverse) {
            vpushi(1);
            gen_op('-');
        }
        gen_op('+');
        indir();
        gen_assign_cast(&mut_vtypei);
        vset(&mut_vtypei, VT_LOCAL | VT_LVAL, vaddr[i]);
        vswap();
        vstore();
        vpop();
    }

    lblock(&a, &b);
    gjmp_addr(d);
    gsym_addr(b, d);
    gsym(a);
    prev_scope(o, 0);
}

/* c2y if/switch declaration */
static void gexpr_decl(void)
{
    int v = decl(VT_JMP);
    if (v > 1 && tok != ';') {
        Sym *s = sym_find(v);
        vset(&s->type, s->r, (s->r & VT_SYM) ? 0 : s->c);
        vtop->sym = s;
    } else {
        if (v)
            skip(';');
        gexpr();
    }
}

static void do_defer(void)
{
    TokenString *dstr;
    Sym *cls;

    dstr = tok_str_alloc();
    if (tok == '{') {
        int braces = 1;
        tok_str_add2(dstr, tok, &tokc);
        next();
        while (braces > 0 && tok != TOK_EOF) {
            if (tok == '{')
                braces++;
            else if (tok == '}')
                braces--;
            tok_str_add2(dstr, tok, &tokc);
            if (braces > 0)
                next();
        }
        if (braces == 0)
            next();
    } else {
        while (tok != ';' && tok != TOK_EOF) {
            tok_str_add2(dstr, tok, &tokc);
            next();
        }
        tok_str_add2(dstr, ';', NULL);
        skip(';');
    }
    tok_str_add(dstr, -1);
    cls = sym_push2(&all_cleanups, SYM_FIELD | ++cur_scope->cl.n, 0, 0);
    cls->cleanup_sym = NULL;
    cls->cleanup_func = (Sym *)dstr;
    cls->next = cur_scope->cl.s;
    cur_scope->cl.s = cls;
}

static void block(int flags)
{
    int a, b, c, d, e, t;
    Scope o, *p;
    AttrDef ad;
    Sym *s;
again:
    memset(&ad, 0, sizeof ad);
    if (check_d_conditional(VT_LOCAL, flags))
        return;
    if (check_d_dollar_if(VT_LOCAL, flags))
        return;
    if (check_d_dollar_foreach(VT_LOCAL, flags))
        return;
    t = tok;
    /* `If` the token carries a value, next() might destroy it. `Only` with
       invalid code such as f(){"123"4;} */
    if (TOK_HAS_VALUE(t))
        goto expr;
    next();

    if (debug_modes)
        tcc_tcov_check_line (tcc_state, 0), tcc_tcov_block_begin (tcc_state);
    parse_statement:
    if (t == TOK_IF) {
        new_scope_s(&o);
        skip('(');
        gexpr_decl();
        a = gvtst(1, 0);
        skip(')');
        block(0);
        if (tok == TOK_ELSE) {
            d = gjmp(0);
            gsym(a);
            next();
            block(0);
            gsym(d); /* patch else jmp */
        } else {
            gsym(a);
        }
        prev_scope_s(&o);

    } else if (t == TOK_WHILE) {
        new_scope_s(&o);
        d = gind();
        skip('(');
        gexpr();
        a = gvtst(1, 0);
        skip(')');
        b = 0;
        lblock(&a, &b);
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
        prev_scope_s(&o);

    } else if (t == '@') {
        unget_tok('@');
        parse_d_attributes(&ad);
        if (tok == '{') {
            t = tok;
            next();
            goto parse_compound_block;
        }
        if (tok == TOK_FOR || tok == TOK_FOREACH || tok == TOK_FOREACH_REVERSE || tok == TOK_WHILE || tok == TOK_DO || tok == TOK_SWITCH || tok == TOK_IF) {
            pending_block_safety = ad.f.func_safety;
            pending_block_pure = ad.f.func_pure;
            t = tok;
            next();
            goto parse_statement;
        }
        if (flags & STMT_EXPR) {
            vpop();
            gexpr();
        } else {
            gexpr();
            vpop();
        }
        skip(';');

    } else if (t == '{') {
    parse_compound_block:
        if (debug_modes)
            tcc_debug_stabn(tcc_state, N_LBRAC, ind - func_ind);
        p = new_scope();
        if (pending_block_safety) {
            cur_scope->fn.safety = pending_block_safety;
            pending_block_safety = 0;
        }
        if (pending_block_pure) {
            cur_scope->fn.pure = pending_block_pure;
            pending_block_pure = 0;
        }
        if (ad.f.func_safety)
            cur_scope->fn.safety = ad.f.func_safety;
        if (ad.f.func_pure)
            cur_scope->fn.pure = ad.f.func_pure;

        /* handle local labels declarations */
        while (tok == TOK_LABEL) {
            do {
                next();
                if (tok < TOK_UIDENT)
                    expect("label identifier");
                label_push(&local_label_stack, tok, LABEL_DECLARED);
                next();
            } while (tok == ',');
            skip(';');
        }

        while (tok != '}') {
	    decl(VT_LOCAL);
            if (tok != '}') {
                block(flags | STMT_COMPOUND);
            }
        }

        prev_scope(p, flags & STMT_EXPR);
        if (debug_modes)
            tcc_debug_stabn(tcc_state, N_RBRAC, ind - func_ind);
        if (local_scope)
            next();
        else if (!nocode_wanted)
            check_func_return();


    } else if (t == TOK_RETURN) {
        b = (func_vt.t & VT_BTYPE) != VT_VOID;
        if (tok != ';') {
            CType *saved_expect = tuple_expect;
            if (is_tuple_type(&func_vt))
                tuple_expect = &func_vt;
            gexpr();
            tuple_expect = saved_expect;
            if (b) {
                if (vtop >= vstack &&
                    (vtop->r & VT_SYM || (vtop->r & VT_VALMASK) == VT_LOCAL || (vtop->r & VT_VALMASK) == VT_LLOCAL || (vtop->r & VT_LVAL))) {
                    check_stack_address_escape(NULL, vtop->sym, 1, &func_vt);
                }
                gen_assign_cast(&func_vt);
            } else {
                if (vtop->type.t != VT_VOID)
                    tcc_warning("void function returns a value");
                vtop--;
            }
        } else if (b && func_old && (func_vt.t & VT_BTYPE) == VT_INT) {
            vpushi(0);
        } else if (b) {
            tcc_warning("'return' with no value");
            b = 0;
        }
        leave_scope(root_scope);
        if (b)
            gfunc_return(&func_vt);
        skip(';');
        /* jump unless last stmt in top-level block */
        if (tok != '}' || local_scope != 1)
            rsym = gjmp(rsym);
        if (debug_modes)
	    tcc_tcov_block_end (tcc_state, -1);
        CODE_OFF();

    } else if (t == TOK_BREAK) {
        /* compute jump */
        if (!cur_scope->bsym)
            tcc_error("cannot break");
        if (cur_switch && cur_scope->bsym == cur_switch->bsym)
            leave_scope(cur_switch->scope);
        else
            leave_scope(loop_scope);
        *cur_scope->bsym = gjmp(*cur_scope->bsym);
        skip(';');

    } else if (t == TOK_CONTINUE) {
        /* compute jump */
        if (!cur_scope->csym)
            tcc_error("cannot continue");
        leave_scope(loop_scope);
        *cur_scope->csym = gjmp(*cur_scope->csym);
        skip(';');

    } else if (t == TOK_DEFER) {
        do_defer();
    } else if (t == TOK_ASSERT) {
        do_d_assert();
    } else if (tcc_state->lang_d && tok == '('
               && t >= TOK_UIDENT
               && !strcmp(get_tok_str(t, &tokc), "scope")) {
        next(); /* skip '(' */
        if (tok >= TOK_UIDENT) {
            const char *name = get_tok_str(tok, &tokc);
            if (!strcmp(name, "success") || !strcmp(name, "failure")) {
                tcc_error("scope(success) / scope(failure) requires error status tracking");
            }
        }
        if (!(tok >= TOK_UIDENT
              && !strcmp(get_tok_str(tok, &tokc), "exit")))
            tcc_error("only 'scope(exit)' is supported");
        next();
        skip(')');
        do_defer();
    } else if (t == TOK_FOREACH || t == TOK_FOREACH_REVERSE) {
        do_foreach(t == TOK_FOREACH_REVERSE);
    } else if (t == TOK_FOR) {
        p = new_scope();

        skip('(');
        if (tok != ';') {
            /* c99 for-loop init decl? */
            if (!decl(VT_JMP)) {
                /* no, regular for-loop init expr */
                gexpr();
                vpop();
            }
        }
        skip(';');
        a = b = 0;
        c = d = gind();
        if (tok != ';') {
            gexpr();
            a = gvtst(1, 0);
        }
        skip(';');
        if (tok != ')') {
            e = gjmp(0);
            d = gind();
            gexpr();
            vpop();
            gjmp_addr(c);
            gsym(e);
        }
        skip(')');
        lblock(&a, &b);
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
        prev_scope(p, 0);

    } else if (t == TOK_DO) {
        new_scope_s(&o);
        a = b = 0;
        d = gind();
        lblock(&a, &b);
        gsym(b);
        skip(TOK_WHILE);
        skip('(');
	gexpr();
        c = gvtst(0, 0);
        skip(')');
        skip(';');
	gsym_addr(c, d);
        gsym(a);
        prev_scope_s(&o);

    } else if (t == TOK_SWITCH) {
        struct switch_t *sw;

        sw = tcc_mallocz(sizeof *sw);
        sw->bsym = &a;
        sw->scope = cur_scope;
        sw->prev = cur_switch;
        sw->nocode_wanted = nocode_wanted;
        cur_switch = sw;

        new_scope_s(&o);
        skip('(');
        gexpr_decl();
        if (!is_integer_btype(vtop->type.t & VT_BTYPE))
            tcc_error("switch value not an integer");
        skip(')');
        sw->sv = *vtop--; /* save switch value */
        a = 0;
        b = gjmp(0); /* jump to first case */
        lblock(&a, NULL);
        a = gjmp(a); /* add implicit break */
        /* case lookup */
        gsym(b);
        prev_scope_s(&o);
        if (sw->nocode_wanted)
            goto skip_switch;
        case_sort(sw);
        sw->bsym = NULL; /* marker for 32bit:`gen_opl`() */
        vpushv(&sw->sv);
        gv(RC_INT);
        d = gcase(sw->p, sw->n, 0);
        vpop();
        if (sw->def_sym)
            gsym_addr(d, sw->def_sym);
        else
            gsym(d);
    skip_switch:
        /* break label */
        gsym(a);
        end_switch();

    } else if (t == TOK_CASE) {
        struct case_t *cr;
        if (!cur_switch)
            expect("switch");
        cr = tcc_malloc(sizeof(struct case_t));
        dynarray_add(&cur_switch->p, &cur_switch->n, cr);
        t = cur_switch->sv.type.t;
        cr->v1 = cr->v2 = value64(expr_const64(), t);
        if (tok == TOK_DOTS && gnu_ext) {
            next();
            cr->v2 = value64(expr_const64(), t);
            if (case_cmp(cr->v2, cr->v1) < 0)
                tcc_warning("empty case range");
        }
        /* case and default are unreachable from a switch under `nocode_wanted` */
        if (!cur_switch->nocode_wanted)
            cr->ind = gind();
        cr->line = file->line_num;
        skip(':');
        goto block_after_label;

    } else if (t == TOK_DEFAULT) {
        if (!cur_switch)
            expect("switch");
        if (cur_switch->def_sym)
            tcc_error("too many 'default'");
        cur_switch->def_sym = cur_switch->nocode_wanted ? -1 : gind();
        skip(':');
        goto block_after_label;

    } else if (t == TOK_GOTO) {
        vla_restore(cur_scope->vla.locorig);
        if (tok == '*' && gnu_ext) {
            /* computed goto */
            next();
            gexpr();
            if ((vtop->type.t & VT_BTYPE) != VT_PTR)
                expect("pointer");
            ggoto();

        } else if (tok >= TOK_UIDENT) {
	    s = label_find(tok);
	    /* put forward definition if needed */
            if (!s)
              s = label_push(&global_label_stack, tok, LABEL_FORWARD);
            else if (s->r == LABEL_DECLARED)
              s->r = LABEL_FORWARD;

	    if (s->r & LABEL_FORWARD) {
		/* start new goto chain for cleanups, linked via label->next */
		if (cur_scope->cl.s && !nocode_wanted) {
                    sym_push2(&pending_gotos, SYM_FIELD, 0, cur_scope->cl.n);
                    pending_gotos->cleanup_label = s;
                    s = sym_push2(&s->next, SYM_FIELD, 0, 0);
                    pending_gotos->next = s;
                }
		s->jnext = gjmp(s->jnext);
	    } else {
		try_call_cleanup_goto(s->cleanupstate);
		gjmp_addr(s->jind);
	    }
	    next();

        } else {
            expect("label identifier");
        }
        skip(';');

    } else if (t == TOK_ASM1 || t == TOK_ASM2 || t == TOK_ASM3) {
        if (cur_scope->fn.safety == FUNC_SAFETY_SAFE)
            tcc_error("inline assembly not allowed in @safe scope");
        else {
            if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
        }
        asm_instr();

    } else {
        if (tok == ':' && t >= TOK_UIDENT) {
            /* label case */
	    next();
            s = label_find(t);
            if (s) {
                if (s->r == LABEL_DEFINED)
                    tcc_error("duplicate label '%s'", get_tok_str(s->v, NULL));
                s->r = LABEL_DEFINED;
		if (s->next) {
		    Sym *pcl; /* pending cleanup goto */
		    for (pcl = s->next; pcl; pcl = pcl->prev)
		      gsym(pcl->jnext);
		    sym_pop(&s->next, NULL, 0);
		} else
		  gsym(s->jnext);
            } else {
                s = label_push(&global_label_stack, t, LABEL_DEFINED);
            }
            s->jind = gind();
            s->cleanupstate = cur_scope->cl.s;

    block_after_label:
            /* `Accept` attributes after labels (e.g. 'unused') */
            parse_attribute(NULL);

            if (debug_modes)
                tcc_tcov_reset_ind(tcc_state);
            vla_restore(cur_scope->vla.loc);

            if (tok != '}') {
                if (0 == (flags & STMT_COMPOUND))
                    goto again;
                /* `C23`: insert implicit null-statement whithin compound statement */
            } else {
                /* we accept this, but it is a mistake */
                tcc_warning_c(warn_all)("deprecated use of label at end of compound statement");
            }
        } else {
            /* expression case */
            if (t != ';') {
                unget_tok(t);
    expr:
                if (flags & STMT_EXPR) {
                    vpop();
                    gexpr();
                } else {
                    gexpr();
                    vpop();
                }
                skip(';');
            }
        }
    }

    if (debug_modes)
        tcc_tcov_check_line (tcc_state, 0), tcc_tcov_block_end (tcc_state, 0);
}

/* `This` skips over a stream of tokens containing balanced {} and ()
   pairs, stopping at outer ',' ';' and '}' (or matching '}' if we started
   with a '{').  `If` `STR` then allocates and stores the skipped tokens
   in *`STR`.  `This` doesn't check if () and {} are nested correctly,
   i.e. "({)}" is accepted.  */
static void skip_or_save_block(TokenString **str)
{
    int braces = tok == '{';
    int level = 0;
    if (str)
      *str = tok_str_alloc();

    while (1) {
	int t = tok;
        if (level == 0
            && (t == ','
             || t == ';'
             || t == '}'
             || t == ')'
             || t == ']'))
             break;
	if (t == TOK_EOF) {
	     if (str || level > 0)
	       tcc_error("unexpected end of file");
	     else
	       break;
	}
	if (str)
	  tok_str_add_tok(*str);
	next();
	if (t == '{' || t == '(' || t == '[') {
	    level++;
	} else if (t == '}' || t == ')' || t == ']') {
	    level--;
	    if (level == 0 && braces && t == '}')
	      break;
	}
    }
    if (str)
	tok_str_add(*str, TOK_EOF);
}

#define EXPR_CONST 1
#define EXPR_ANY   2

static void parse_init_elem(int expr_type)
{
    int saved_global_expr;
    switch(expr_type) {
    case EXPR_CONST:
        /* compound literals must be allocated globally in this case */
        saved_global_expr = global_expr;
        global_expr = 1;
        expr_const1();
        global_expr = saved_global_expr;
        /* `NOTE`: symbols are accepted, as well as lvalue for anon symbols
	   (compound literals).  */
        if (((vtop->r & (VT_VALMASK | VT_LVAL)) != VT_CONST
             && ((vtop->r & (VT_SYM|VT_LVAL)) != (VT_SYM|VT_LVAL)
                 || vtop->sym->v < SYM_FIRST_ANOM))
#ifdef TCC_TARGET_PE
                 || ((vtop->r & VT_SYM) && vtop->sym->a.dllimport)
#endif
           )
            tcc_error("initializer element is not constant");
        break;
    case EXPR_ANY:
        expr_eq();
        break;
    }
}

#if 1
static void init_assert(init_params *p, int offset)
{
    if (p->sec ? !NODATA_WANTED && offset > p->sec->data_offset
               : !nocode_wanted && offset > p->local_offset)
        tcc_internal_error("initializer overflow");
}
#else
#define init_assert(sec, offset)
#endif

/* put zeros for variable based init */
static void init_putz(init_params *p, unsigned long c, int size)
{
    init_assert(p, c + size);
    if (p->sec) {
        /* nothing to do because globals are already set to zero */
    } else {
        vpush_helper_func(TOK_memset);
        vseti(VT_LOCAL, c);
        vpushi(0);
        vpushs(size);
#if defined TCC_TARGET_ARM && defined TCC_ARM_EABI
        vswap();  /* using `__aeabi_memset`(void*, `size_t`, int) */
#endif
        gfunc_call(3);
    }
}

#define DIF_FIRST     1
#define DIF_SIZE_ONLY 2
#define DIF_HAVE_ELEM 4
#define DIF_CLEAR     8

/* delete relocations for specified range c ... c + size. `Unfortunatly`
   in very special cases, relocations may occur unordered */
static void decl_design_delrels(Section *sec, int c, int size)
{
    ElfW_Rel *rel, *rel2, *rel_end;
    if (!sec || !sec->reloc)
        return;
    rel = rel2 = (ElfW_Rel*)sec->reloc->data;
    rel_end = (ElfW_Rel*)(sec->reloc->data + sec->reloc->data_offset);
    while (rel < rel_end) {
        if (rel->r_offset >= c && rel->r_offset < c + size) {
            sec->reloc->data_offset -= sizeof *rel;
        } else {
            if (rel2 != rel)
                memcpy(rel2, rel, sizeof *rel);
            ++rel2;
        }
        ++rel;
    }
}

static void decl_design_flex(init_params *p, Sym *ref, int index)
{
    if (ref == p->flex_array_ref) {
        if (index >= ref->c)
            ref->c = index + 1;
    } else if (ref->c < 0 && index >= 0)
        tcc_error("flexible array has zero size in this context");
}

/* t is the array or struct type. c is the array or struct
   address. `cur_field` is the pointer to the current
   field, for arrays the 'c' member contains the current start
   index.  'flags' is as in `decl_initializer`.
   'al' contains the already initialized length of the
   current container (starting at c).  `This` returns the new length of that.  */
static int decl_designator(init_params *p, CType *type, unsigned long c,
                           Sym **cur_field, int flags, int al)
{
    Sym *s, *f;
    int index, index_last, align, l, nb_elems, elem_size;
    unsigned long corig = c;

    elem_size = 0;
    nb_elems = 1;

    if (flags & DIF_HAVE_ELEM)
        goto no_designator;

    if (gnu_ext && tok >= TOK_UIDENT) {
        l = tok, next();
        if (tok == ':')
            goto struct_field;
        unget_tok(l);
    }

    /* `NOTE`: we only support ranges for last designator */
    while (nb_elems == 1 && (tok == '[' || tok == '.')) {
        if (tok == '[') {
            if (!(type->t & VT_ARRAY))
                expect("array type");
            next();
            index = index_last = expr_const();
            if (tok == TOK_DOTS && gnu_ext) {
                next();
                index_last = expr_const();
            }
            skip(']');
            s = type->ref;
            decl_design_flex(p, s, index_last);
            if (index < 0 || index_last >= s->c || index_last < index)
	        tcc_error("index exceeds array bounds or range is empty");
            if (cur_field)
		(*cur_field)->c = index_last;
            type = pointed_type(type);
            elem_size = type_size(type, &align);
            c += index * elem_size;
            nb_elems = index_last - index + 1;
        } else {
            int cumofs;
            next();
            l = tok;
        struct_field:
            next();
	    f = find_field(type, l, &cumofs);
            if (cur_field)
                *cur_field = f;
	    type = &f->type;
            c += cumofs;
        }
        cur_field = NULL;
    }
    if (!cur_field) {
        if (tok == '=') {
            next();
        } else if (!gnu_ext) {
	    expect("=");
        }
    } else {
    no_designator:
        if (type->t & VT_ARRAY) {
	    index = (*cur_field)->c;
            s = type->ref;
            decl_design_flex(p, s, index);
            if (index >= s->c)
                tcc_error("too many initializers");
            type = pointed_type(type);
            elem_size = type_size(type, &align);
            c += index * elem_size;
        } else {
            f = *cur_field;
	    /* `Skip` bitfield padding. `Also` with size 32 and 64. */
	    while (f && (f->v & SYM_FIRST_ANOM) &&
		   is_integer_btype(f->type.t & VT_BTYPE))
	        *cur_field = f = f->next;
            if (!f)
                tcc_error("too many initializers");
	    type = &f->type;
            c += f->c;
        }
    }

    if (!elem_size) /* for structs */
        elem_size = type_size(type, &align);

    /* `Using` designators the same element can be initialized more
       than once.  `In` that case we need to delete possibly already
       existing relocations. */
    if (!(flags & DIF_SIZE_ONLY) && c - corig < al) {
        decl_design_delrels(p->sec, c, elem_size * nb_elems);
        flags &= ~DIF_CLEAR; /* mark stack dirty too */
    }

    decl_initializer(p, type, c, flags & ~DIF_FIRST);

    if (!(flags & DIF_SIZE_ONLY) && nb_elems > 1) {
        Sym aref = {0};
        CType t1;
        int i;
        if (p->sec || (type->t & VT_ARRAY)) {
            /* make `init_putv`/vstore believe it were a struct */
            aref.c = elem_size;
            t1.t = VT_STRUCT, t1.ref = &aref;
            type = &t1;
        }
        if (p->sec)
            vpush_ref(type, p->sec, c, elem_size);
        else
	    vset(type, VT_LOCAL|VT_LVAL, c);
        for (i = 1; i < nb_elems; i++) {
            vdup();
            init_putv(p, type, c + elem_size * i);
	}
        vpop();
    }

    c += nb_elems * elem_size;
    if (c - corig > al)
      al = c - corig;
    return al;
}

static void write_ldouble(unsigned char *d, void *s)
{
    //printf("long double %Lf\n", *(long double*)s);
#ifdef TCC_CROSS_TEST
    if (LDOUBLE_SIZE >= 10) {
        double b = *(long double*)s;
        s = &b;
#else
    if (sizeof (long double) == 8 && LDOUBLE_SIZE >= 10) {
#endif
        /* our 'long double' is a double really (`_WIN32`, `__APPLE__`) */
        uint64_t m = *(uint64_t*)s;
        int e = m >> 48;
        int f = e >> 4 & 0x7FF;
        m <<= 11;
        if (0 == f) {
            if (0 == m)
                goto set;
            for (f = 1; !(m & 1ULL<<63); --f)
                m <<= 1;
        }
        if (f == 0x7ff)
            f = 0x43FF;
        e = (e & 0x8000) | (f + 0x3C00);
        m |= 1ULL<<63;
    set:
    #if (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* double -> extended */
        write64le(d, m);
        write16le(d+8, e);
    #elif LDOUBLE_SIZE == 16
        /* double -> quad */
        write64le(d+6, m << 1);
        write16le(d+14, e);
    #endif
        ;
    } else {
    #if LDOUBLE_SIZE == 8
        /* long double -> double */
        double b = *(long double*)s;
        memcpy(d, &b, 8);
    #elif (__i386__ || __x86_64__) && (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* extended -> extended */
        memcpy(d, s, 10);
    #elif (__i386__ || __x86_64__) && (defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64)
        /* extended -> quad */
        uint64_t m = *(uint64_t*)s;
        int e = *(uint16_t*)((char*)s + 8);
        write64le(d+6, m << 1);
        write16le(d+14, e);
    #elif (__aarch64__ || __riscv) && (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* quad -> extended */
        uint64_t m = read64le((char*)s + 6);
        int e = read16le((char*)s + 14);
        (e & 0x7fff) && (m & 1) && 0 == ++m && ++e;
        write64le(d, m >> 1 | ((e & 0x7fff) ? 1ULL<<63 : 0));
        write16le(d+8, e);
    #else
        /* unknown */
        if (sizeof (long double) == LDOUBLE_SIZE)
            memcpy(d, s, LDOUBLE_SIZE);
    #endif
    }
}

/* store a value or an expression directly in global data or in local array */
static void init_putv(init_params *p, CType *type, unsigned long c)
{
    int bt;
    void *ptr;
    CType dtype;
    int size, align;
    Section *sec = p->sec;
    uint64_t val;

    dtype = *type;
    /* need to do that to avoid false warning */
    dtype.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);

    size = type_size(type, &align);
    if (type->t & VT_BITFIELD)
        size = (BIT_POS(type->t) + BIT_SIZE(type->t) + 7) / 8;
    init_assert(p, c + size);

    if (sec) {
        /* `XXX`: not portable */
        /* `XXX`: generate error if incorrect relocation */
        gen_assign_cast(&dtype);
        bt = type->t & VT_BTYPE;

        if ((vtop->r & VT_SYM)
            && bt != VT_PTR
            && (bt != (PTR_SIZE == 8 ? VT_LLONG : VT_INT)
                || (type->t & VT_BITFIELD))
            && !((vtop->r & VT_CONST) && vtop->sym->v >= SYM_FIRST_ANOM)
            )
            tcc_error("initializer element is not computable at load time");

        if (NODATA_WANTED) {
            vtop--;
            return;
        }

        ptr = sec->data + c;
        val = vtop->c.i;

	if ((vtop->r & (VT_SYM|VT_CONST)) == (VT_SYM|VT_CONST)
            && vtop->sym->v >= SYM_FIRST_ANOM
            && ((vtop->r & VT_LVAL) /* compound literal */
                || bt == VT_STRUCT /* designator */
                || (type->t & VT_ARRAY) /* string/compound literal */
                )) {
	    /* memcpy stuff over.  */
	    Section *ssec;
	    ElfSym *esym;
	    ElfW_Rel *rel;
	    esym = elfsym(vtop->sym);
	    ssec = tcc_state->sections[esym->st_shndx];
	    memmove (ptr, ssec->data + esym->st_value + (int)vtop->c.i, size);
	    if (ssec->reloc) {
		/* `We` need to copy over all memory contents, and that
		   includes relocations.  `Use` the fact that relocs are
		   created it order, so look from the end of relocs
		   until we hit one before the copied region.  */
                unsigned long relofs = ssec->reloc->data_offset;
		while (relofs >= sizeof(*rel)) {
                    relofs -= sizeof(*rel);
                    rel = (ElfW_Rel*)(ssec->reloc->data + relofs);
		    if (rel->r_offset >= esym->st_value + size)
		      continue;
		    if (rel->r_offset < esym->st_value)
		      break;
		    put_elf_reloca(symtab_section, sec,
				   c + rel->r_offset - esym->st_value,
				   ELFW(R_TYPE)(rel->r_info),
				   ELFW(R_SYM)(rel->r_info),
#if PTR_SIZE == 8
				   rel->r_addend
#else
				   0
#endif
				  );
		}
	    }
	} else {
            if (type->t & VT_BITFIELD) {
                int bit_pos, bit_size, bits, n;
                unsigned char *p, v, m;
                bit_pos = BIT_POS(vtop->type.t);
                bit_size = BIT_SIZE(vtop->type.t);
                p = (unsigned char*)ptr + (bit_pos >> 3);
                bit_pos &= 7, bits = 0;
                while (bit_size) {
                    n = 8 - bit_pos;
                    if (n > bit_size)
                        n = bit_size;
                    v = val >> bits << bit_pos;
                    m = ((1 << n) - 1) << bit_pos;
                    *p = (*p & ~m) | (v & m);
                    bits += n, bit_size -= n, bit_pos = 0, ++p;
                }
            } else
            switch(bt) {
	    case VT_BOOL:
		*(char *)ptr = val != 0;
                break;
	    case VT_BYTE:
		*(char *)ptr = val;
		break;
	    case VT_SHORT:
                write16le(ptr, val);
		break;
	    case VT_FLOAT:
                write32le(ptr, val);
		break;
	    case VT_DOUBLE:
                write64le(ptr, val);
		break;
	    case VT_LDOUBLE:
                write_ldouble(ptr, &vtop->c.ld);
		break;

#if PTR_SIZE == 8
            /* `intptr_t` may need a reloc too, see tcctest.c:`relocation_test`() */
	    case VT_LLONG:
	    case VT_PTR:
	        if (vtop->r & VT_SYM)
	          greloca(sec, vtop->sym, c, R_DATA_PTR, val);
	        else
	          write64le(ptr, val);
	        break;
            case VT_INT:
                write32le(ptr, val);
                break;
#else
	    case VT_LLONG:
                write64le(ptr, val);
                break;
            case VT_PTR:
            case VT_INT:
	        if (vtop->r & VT_SYM)
	          greloc(sec, vtop->sym, c, R_DATA_PTR);
	        write32le(ptr, val);
	        break;
#endif
	    default:
                //tcc_internal_error("unexpected type");
                break;
	    }
	}
        vtop--;
    } else {
        vset(&dtype, VT_LOCAL|VT_LVAL, c);
        vswap();
        vstore();
        vpop();
    }
}

/* `Initialize` a slice object at 'c' from the array expression on top of
   the value stack.  */
static void init_slice_from_array(init_params *p, CType *type, unsigned long c)
{
    CType st = vtop->type;
    Sym *s = type->ref;
    CType *ptr_type = &s->next->next->type;
    int elem_size, align;

    if (!is_compatible_unqualified_types(&st.ref->type,
                                         pointed_type(ptr_type)))
        cast_error(&st, type);

    if (st.t & VT_VLA) {
        elem_size = type_size(&st.ref->type, &align);
        vset(&int_type, VT_LOCAL | VT_LVAL, st.ref->c);
        if (elem_size != 1) {
            vpushi(elem_size);
            gen_op('/');
        }
    } else {
        vpushs(array_slice_length(&st));
    }
    init_putv(p, &s->next->type, c);

    init_putv(p, ptr_type, c + PTR_SIZE);
}

/* 't' contains the type and storage info. 'c' is the offset of the
   object in section 'sec'. `If` 'sec' is `NULL`, it means stack based
   allocation. 'flags & `DIF_FIRST`' is true if array '{' must be read (multi
   dimension implicit array init handling). 'flags & `DIF_SIZE_ONLY`' is true if
   size only evaluation is wanted (only for arrays). */
static void decl_initializer(init_params *p, CType *type, unsigned long c, int flags)
{
    int len, n, no_oblock, i;
    int size1, align1;
    Sym *s, *f;
    Sym indexsym;
    CType *t1;

    /* generate line number info */
    if (debug_modes && !(flags & DIF_SIZE_ONLY) && !p->sec)
        tcc_debug_line(tcc_state), tcc_tcov_check_line (tcc_state, 1);

    if (!(flags & DIF_HAVE_ELEM) && tok != '{' &&
	/* `In` case of strings we have special handling for arrays, so
	   don't consume them as initializer value (which would commit them
	   to some anonymous symbol).  */
	tok != TOK_LSTR && tok != TOK_STR &&
	(!(flags & DIF_SIZE_ONLY)
            /* a struct may be initialized from a struct of same type, as in
                    struct {int x,y;} a = {1,2}, b = {3,4}, c[] = {a,b};
               `In` that case we need to parse the element in order to check
               it for compatibility below */
            || (type->t & VT_BTYPE) == VT_STRUCT)
        ) {
        int ncw_prev = nocode_wanted;
        if ((flags & DIF_SIZE_ONLY) && !p->sec)
            ++nocode_wanted;
        if (is_tuple_type(type))
            tuple_expect = type;
	parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
        nocode_wanted = ncw_prev;
        flags |= DIF_HAVE_ELEM;
        tuple_expect = NULL;
    }

    if (is_slice_type(type) && !(flags & DIF_HAVE_ELEM)
        && (tok == TOK_STR || tok == TOK_LSTR)) {
        /* A string literal initializes a slice.  `Adjacent` literals are
           concatenated, as in C array initializers.  */
        int ncw_prev = nocode_wanted;
        int size1 = (tok == TOK_STR ? 1 : sizeof(nwchar_t));
        cstr_reset(&initstr);
        while (tok == TOK_STR || tok == TOK_LSTR) {
            if ((tok == TOK_STR ? 1 : sizeof(nwchar_t)) != size1)
                tcc_error("unhandled string literal merging");
            if (initstr.size)
                initstr.size -= size1;
            cstr_cat(&initstr, tokc.str.data, tokc.str.size);
            next();
        }
        unget_tok(size1 == 1 ? TOK_STR : TOK_LSTR);
        tokc.str.size = initstr.size;
        tokc.str.data = initstr.data;
        if ((flags & DIF_SIZE_ONLY) && !p->sec)
            ++nocode_wanted;
        parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
        nocode_wanted = ncw_prev;
        if (flags & DIF_SIZE_ONLY)
            vpop();
        else if (vtop->type.t & (VT_ARRAY | VT_VLA))
            init_slice_from_array(p, type, c);
        else {
            /* `The` literal was only part of a larger expression that
               already evaluated to a slice.  */
            if (!is_compatible_unqualified_types(type, &vtop->type))
                cast_error(&vtop->type, type);
            vset(type, VT_LOCAL | VT_LVAL, c);
            vswap();
            vstore();
            vpop();
        }
        return;
    }

    if ((flags & DIF_HAVE_ELEM) && is_slice_type(type)) {
        if (vtop->type.t & (VT_ARRAY | VT_VLA)) {
            if (flags & DIF_SIZE_ONLY)
                vpop();
            else
                init_slice_from_array(p, type, c);
            return;
        }
        if (!is_compatible_unqualified_types(type, &vtop->type))
            cast_error(&vtop->type, type);
    }

    if (type->t & VT_ARRAY) {
        no_oblock = 1;
        if (((flags & DIF_FIRST) && tok != TOK_LSTR && tok != TOK_STR) ||
            tok == '{') {
            skip('{');
            no_oblock = 0;
        }

        s = type->ref;
        n = s->c;
        t1 = pointed_type(type);
        size1 = type_size(t1, &align1);

        /* only parse strings here if correct type (otherwise: handle
           them as ((w)char *) expressions */
        if ((tok == TOK_LSTR &&
#ifdef TCC_TARGET_PE
             (t1->t & VT_BTYPE) == VT_SHORT && (t1->t & VT_UNSIGNED)
#else
             (t1->t & VT_BTYPE) == VT_INT
#endif
            ) || (tok == TOK_STR && (t1->t & VT_BTYPE) == VT_BYTE)) {
	    len = 0;
            cstr_reset(&initstr);
            if (size1 != (tok == TOK_STR ? 1 : sizeof(nwchar_t)))
              tcc_error("unhandled string literal merging");
            while (tok == TOK_STR || tok == TOK_LSTR) {
                if (initstr.size)
                  initstr.size -= size1;
                if (tok == TOK_STR)
                  len += tokc.str.size;
                else
                  len += tokc.str.size / sizeof(nwchar_t);
                len--;
                cstr_cat(&initstr, tokc.str.data, tokc.str.size);
                next();
            }
            if (tok != ')' && tok != '}' && tok != ',' && tok != ';'
                && tok != TOK_EOF) {
                /* `Not` a lone literal but part of a bigger expression.  */
                unget_tok(size1 == 1 ? TOK_STR : TOK_LSTR);
                tokc.str.size = initstr.size;
                tokc.str.data = initstr.data;
                goto do_init_array;
            }

            decl_design_flex(p, s, len);
            if (!(flags & DIF_SIZE_ONLY)) {
                int nb = n, ch;
                if (len < nb)
                    nb = len;
                if (len > nb)
                  tcc_warning("initializer-string for array is too long");
                /* in order to go faster for common case (char
                   string in global variable, we handle it
                   specifically */
                if (p->sec && size1 == 1) {
                    init_assert(p, c + nb);
                    if (!NODATA_WANTED)
                      memcpy(p->sec->data + c, initstr.data, nb);
                } else {
                    for(i=0;i<n;i++) {
                        if (i >= nb) {
                          /* only add trailing zero if enough storage (no
                             warning in this case since it is standard) */
                          if (flags & DIF_CLEAR)
                            break;
                          if (n - i >= 4) {
                            init_putz(p, c + i * size1, (n - i) * size1);
                            break;
                          }
                          ch = 0;
                        } else if (size1 == 1)
                          ch = ((unsigned char *)initstr.data)[i];
                        else
                          ch = ((nwchar_t *)initstr.data)[i];
                        vpushi(ch);
                        init_putv(p, t1, c + i * size1);
                    }
                }
            }
            if (tok == ',' && !no_oblock) /* static const char s[] = { "123", }; */
                next();
        } else {

          do_init_array:
	    indexsym.c = 0;
	    f = &indexsym;

          do_init_list:
            /* zero memory once in advance */
            if (!(flags & (DIF_CLEAR | DIF_SIZE_ONLY))) {
                init_putz(p, c, n*size1);
                flags |= DIF_CLEAR;
            }

	    len = 0;
            /* `GNU` extension: if the initializer is empty for a flex array,
               it's size is zero.  `We` won't enter the loop, so set the size
               now.  */
            decl_design_flex(p, s, len - 1);
	    while (tok != '}' || (flags & DIF_HAVE_ELEM)) {
		len = decl_designator(p, type, c, &f, flags, len);
		flags &= ~DIF_HAVE_ELEM;
		if (type->t & VT_ARRAY) {
		    ++indexsym.c;
		    /* special test for multi dimensional arrays (may not
		       be strictly correct if designators are used at the
		       same time) */
		    if (no_oblock && len >= n*size1)
		        break;
		} else {
		    if (s->type.t == VT_UNION)
		        f = NULL;
		    else
		        f = f->next;
		    if (no_oblock && f == NULL)
		        break;
		}

		if (tok == '}')
		    break;
		skip(',');
	    }
        }
        if (!no_oblock)
            skip('}');

    } else if ((flags & DIF_HAVE_ELEM)
        /* `Use` `i_c_parameter_t`, to strip toplevel qualifiers.
           `The` source type might have `VT_QUALIFIER_CONSTANT` set, which is
           of course assignable to non-const elements.  */
            && is_compatible_unqualified_types(type, &vtop->type)) {
        goto one_elem;

    } else if ((type->t & VT_BTYPE) == VT_STRUCT) {
        /* `An` initializer expression was parsed but does not have the
           struct type, as in `(int, int) t = 1;`.  A braced list never
           reaches this point with DIF_HAVE_ELEM set.  */
        if (is_tuple_type(type) && (flags & DIF_HAVE_ELEM))
            cast_error(&vtop->type, type);
        no_oblock = 1;
        if ((flags & DIF_FIRST) || tok == '{') {
            skip('{');
            no_oblock = 0;
        }
        s = type->ref;
        f = s->next;
        n = s->c;
        size1 = 1;
	goto do_init_list;

    } else if (tok == '{') {
        if (flags & DIF_HAVE_ELEM)
          skip(';');
        next();
        decl_initializer(p, type, c, flags & ~DIF_HAVE_ELEM);
        skip('}');

    } else one_elem: if ((flags & DIF_SIZE_ONLY)) {
	/* `If` we supported only `ISO` C we wouldn't have to accept calling
	   this on anything than an array if `DIF_SIZE_ONLY` (and even then
	   only on the outermost level, so no recursion would be needed),
	   because initializing a flex array member isn't supported.
	   `But` `GNU` C supports it, so we need to recurse even into
	   subfields of structs and arrays when `DIF_SIZE_ONLY` is set.  */
        /* just skip expression */
        if (flags & DIF_HAVE_ELEM)
            vpop();
        else
            skip_or_save_block(NULL);

    } else {
	if (!(flags & DIF_HAVE_ELEM)) {
	    /* `This` should happen only when we haven't parsed
	       the init element above for fear of committing a
	       string constant to memory too early.  */
	    if (tok != TOK_STR && tok != TOK_LSTR)
	      expect("string constant");
	    parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
	}
        if (!p->sec && (flags & DIF_CLEAR) /* container was already zero'd */
            && (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
            && vtop->c.i == 0
            && btype_size(type->t & VT_BTYPE) /* not for fp constants */
            )
            vpop();
        else
            init_putv(p, type, c);
    }
}

/* parse an initializer for type 't' if '`has_init`' is non zero, and
   allocate space in local or global data space ('r' is either
   `VT_LOCAL` or `VT_CONST`). `If` 'v' is non zero, then an associated
   variable 'v' of scope 'scope' is declared before initializers
   are parsed. `If` 'v' is zero, then a reference to the new object
   is put in the value stack. `If` '`has_init`' is 2, a special parsing
   is done to handle string constants. */
static void decl_initializer_alloc(CType *type, AttrDef *ad, int r,
                                   int has_init, int v, int scope)
{
    int size, align, addr;
    TokenString *init_str = NULL;

    Section *sec;
    Sym *flexible_array;
    Sym *sym = NULL;
    int saved_nocode_wanted = nocode_wanted;
#ifdef CONFIG_TCC_BCHECK
    int bcheck = tcc_state->do_bounds_check && !NODATA_WANTED;
#endif
    init_params p = {0};

    if (scope == VT_CONST) {
        /* see if a global symbol was already defined */
        sym = sym_find(v);
        if (sym) {
            patch_storage(sym, ad, type);
            if (sym->a.final_var && has_init)
                sym->a.assigned = 1;
            /* we accept several definitions of the same global variable. */
            if (!has_init && sym->c && elfsym(sym)->st_shndx != SHN_UNDEF)
                return;
            type = &sym->type;
        }
    }

    /* `Always` allocate static or global variables */
    if (v && (r & VT_VALMASK) == VT_CONST)
        nocode_wanted |= DATA_ONLY_WANTED;

    flexible_array = NULL;
    size = type_size(type, &align);

    /* exactly one flexible array may be initialized, either the
       toplevel array or the last member of the toplevel struct */

    if (size < 0) {
        // error out except for top-level incomplete arrays
        // (arrays of incomplete types are handled in array parsing)
        if (!(type->t & VT_ARRAY))
            tcc_error("initialization of incomplete type");
        /* `If` the base type itself was an array type of unspecified
           size (like in 'typedef int arr[]; arr x = {1};') then
           we will overwrite the unknown size by the real one for
           this decl.  `We` need to unshare the ref symbol holding
           that size.  */
        if (IS_BT_ARRAY(type->t))
            type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        p.flex_array_ref = type->ref;

    } else if (has_init && (type->t & VT_BTYPE) == VT_STRUCT) {
        Sym *field = type->ref->next;
        if (field) {
            while (field->next)
                field = field->next;
            if (field->type.t & VT_ARRAY && field->type.ref->c < 0) {
                flexible_array = field;
                p.flex_array_ref = field->type.ref;
                size = -1;
            }
        }
    }

    if (size < 0) {
        /* `If` unknown size, do a dry-run 1st pass */
        if (!has_init)
            goto err_size;
        if (has_init == 2) {
            /* only get strings */
            init_str = tok_str_alloc();
            while (tok == TOK_STR || tok == TOK_LSTR) {
                tok_str_add_tok(init_str);
                next();
            }
            tok_str_add(init_str, TOK_EOF);
        } else
            skip_or_save_block(&init_str);
        unget_tok(0);

        /* compute size */
        begin_macro(init_str, 1);
        next();
        decl_initializer(&p, type, 0, DIF_FIRST | DIF_SIZE_ONLY);
        /* prepare second initializer parsing */
        macro_ptr = init_str->str;
        next();

        /* if still unknown size, error */
        size = type_size(type, &align);
        if (size < 0)
    err_size:
            tcc_error("unknown type size");

        /* `If` there's a flex member and it was used in the initializer
           adjust size.  */
        if (flexible_array && flexible_array->type.ref->c > 0)
            size += flexible_array->type.ref->c
                    * pointed_size(&flexible_array->type);
    }

    /* take into account specified alignment if bigger */
    if (ad->a.aligned) {
	int speca = 1 << (ad->a.aligned - 1);
        if (speca > align)
            align = speca;
    } else if (ad->a.packed) {
        align = 1;
    }

    if (!v && NODATA_WANTED)
        size = 0, align = 1;

    if ((r & VT_VALMASK) == VT_LOCAL) {
        sec = NULL;
#ifdef CONFIG_TCC_BCHECK
        if (bcheck && v) {
            /* add padding between stack variables for bound checking */
            loc -= align;
        }
#endif
        loc = (loc - size) & -align;
        addr = loc;
        p.local_offset = addr + size;
#ifdef CONFIG_TCC_BCHECK
        if (bcheck && v) {
            /* add padding between stack variables for bound checking */
            loc -= align;
        }
#endif
        if (v) {
            /* local variable */
#ifdef CONFIG_TCC_ASM
	    if (ad->asm_label) {
		int reg = asm_parse_regvar(ad->asm_label);
		if (reg >= 0)
		    r = (r & ~VT_VALMASK) | reg;
	    }
#endif
            sym = sym_push(v, type, r, addr);
	    if (ad->cleanup_func) {
		Sym *cls = sym_push2(&all_cleanups,
                    SYM_FIELD | ++cur_scope->cl.n, 0, 0);
		cls->cleanup_sym = sym;
		cls->cleanup_func = ad->cleanup_func;
		cls->next = cur_scope->cl.s;
		cur_scope->cl.s = cls;
	    }

            sym->a = ad->a;
            if (sym->a.final_var && has_init)
                sym->a.assigned = 1;
        } else {
            /* push local reference */
            vset(type, r, addr);
        }
    } else {
        CType *tp = type;
        int is_const;

        while ((tp->t & (VT_BTYPE|VT_ARRAY)) == (VT_PTR|VT_ARRAY))
            tp = &tp->ref->type;
        is_const = tp->t & VT_QUALIFIER_CONSTANT;

        /* allocate symbol in corresponding section */
        sec = ad->section;
        if (!sec) {
            if (type->t & VT_STORAGE_TLS) {
                sec = find_section(tcc_state, has_init ? ".tdata" : ".tbss");
                sec->sh_flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
                sec->sh_type = has_init ? SHT_PROGBITS : SHT_NOBITS;
            } else if (is_const) {
		sec = rodata_section;
            } else if (has_init) {
		sec = data_section;
                /*if (`g_debug` & 4)
                    `tcc_warning`("rw data: %s", `get_tok_str`(v, 0));*/
            } else if (tcc_state->nocommon)
                sec = bss_section;
        } else if (ad->new_section) {
            /* `XXX`: `DWIM` logic: set section flags according to first usage */
            if (!is_const) {
                sec->sh_flags |= SHF_WRITE;
                if (!has_init)
                    sec->sh_type = SHT_NOBITS;
            }
        }

#ifdef CONFIG_TCC_BCHECK
        if (bcheck)
            size = size + 1; /* add padding */
#endif
        if (sec) {
	    addr = section_add(sec, size, align);
        } else {
            addr = align; /* `SHN_COMMON` is special, symbol value is align */
	    sec = common_section;
        }

        if (v) {
            if (!sym) {
                sym = sym_push(v, type, r | VT_SYM, 0);
                patch_storage(sym, ad, NULL);
                if (sym->a.final_var && has_init)
                    sym->a.assigned = 1;
            }
            /* update symbol definition */
	    put_extern_sym(sym, sec, addr, size);
        } else {
            /* push global reference */
            vpush_ref(type, sec, addr, size);
            sym = vtop->sym;
	    vtop->r |= r;
        }

#ifdef CONFIG_TCC_BCHECK
        /* handles bounds now because the symbol must be defined
           before for the relocation */
        if (bcheck) {
            addr_t *bounds_ptr;

            greloca(bounds_section, sym, bounds_section->data_offset, R_DATA_PTR, 0);
            /* then add global bound info */
            bounds_ptr = section_ptr_add(bounds_section, 2 * sizeof(addr_t));
            bounds_ptr[0] = 0; /* relocated */
            bounds_ptr[1] = size - 1;
        }
#endif
    }

    if (type->t & VT_VLA) {
        int a;

        if (has_init)
            tcc_error("variable length array cannot be initialized");

        if (NODATA_WANTED)
            goto no_alloc;

        /* save before-`VLA` stack pointer if needed */
        if (cur_scope->vla.num == 0) {
            if (cur_scope->prev && cur_scope->prev->vla.num) {
                cur_scope->vla.locorig = cur_scope->prev->vla.loc;
            } else {
                gen_vla_sp_save(loc -= PTR_SIZE);
                cur_scope->vla.locorig = loc;
            }
        }

        vpush_type_size(type, &a);
        gen_vla_alloc(type, a);
#if defined TCC_TARGET_PE && defined TCC_TARGET_X86_64
        /* on `_WIN64`, because of the function args scratch area, the
           result of alloca differs from `RSP` and is returned in `RAX`.  */
        gen_vla_result(addr), addr = (loc -= PTR_SIZE);
#endif
        gen_vla_sp_save(addr);
        cur_scope->vla.loc = addr;
        cur_scope->vla.num++;
    } else if (has_init) {
        p.sec = sec;
        decl_initializer(&p, type, addr, DIF_FIRST);
        /* patch flexible array member size back to -1, */
        /* for possible subsequent similar declarations */
        if (flexible_array)
            flexible_array->type.ref->c = -1;
    }

 no_alloc:
    /* restore parse state if needed */
    if (init_str) {
        end_macro();
        next();
    }

    nocode_wanted = saved_nocode_wanted;
}

/* generate vla code saved in `post_type`() */
static void func_vla_arg_code(Sym *arg)
{
    int align;
    TokenString *vla_array_tok = NULL;

    if (arg->type.ref)
        func_vla_arg_code(arg->type.ref);

    if ((arg->type.t & VT_VLA) && arg->type.ref->vla_array_str) {
	loc -= type_size(&int_type, &align);
	loc &= -align;
	arg->type.ref->c = loc;

	unget_tok(0);
	vla_array_tok = tok_str_alloc();
	vla_array_tok->str = arg->type.ref->vla_array_str;
	begin_macro(vla_array_tok, 1);
	next();
	gexpr();
	end_macro();
	next();
	vpush_type_size(&arg->type.ref->type, &align);
	gen_op('*');
	vset(&int_type, VT_LOCAL|VT_LVAL, arg->type.ref->c);
	vswap();
	vstore();
	vpop();
    }
}

static void func_vla_arg(Sym *sym)
{
    Sym *arg;

    for (arg = sym->type.ref->next; arg; arg = arg->next)
        if ((arg->type.t & VT_BTYPE) == VT_PTR && (arg->type.ref->type.t & VT_VLA))
            func_vla_arg_code(arg->type.ref);
}

/* set the local stack address for function parameter from `gfunc_prolog`() */
ST_FUNC Sym *gfunc_set_param(Sym *s, int c, int byref)
{
    s = sym_find(s->v);
    if (!s) /* unnamed parameters, not enabled */
        return NULL;
    s->c = c;
    if (byref)
        s->r = VT_LLOCAL | VT_LVAL; /* otherwise `VT_LOCAL` */
    return s;
}

/* push parameters (and their types), last first */
static void sym_push_params(Sym *ref)
{
    Sym *s = ref;
    while (s->next)
        s = s->next;
    while (s != ref) {
        if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
            Sym *ps = sym_copy(s, &local_stack);
            ps->a.is_param = 1;
        }
        s = s->prev;
    }
}

/* parse a function defined by symbol 'sym' and generate its code in
   '`cur_text_section`' */
static void gen_function(Sym *sym)
{
    Scope *saved_scope = cur_scope, *saved_root = root_scope;
    Scope *saved_loop = loop_scope;
    struct switch_t *saved_switch = cur_switch;
    Sym *saved_local_stack = local_stack;
    Sym *saved_local_label_stack = local_label_stack;
    Sym *saved_global_label_stack = global_label_stack;
    Sym *saved_cleanups = all_cleanups;
    Sym *saved_pending_gotos = pending_gotos;
    Section *saved_text_section = cur_text_section;
    int saved_local_scope = local_scope, saved_rsym = rsym;
    int saved_nb_temp_local_vars = nb_temp_local_vars;
    int saved_loc = loc, saved_ind = ind;
    int saved_nocode_wanted = nocode_wanted;

    nocode_wanted = 0;

    tuple_expect = NULL;

    ind = cur_text_section->data_offset;
    if (sym->a.aligned) {
	size_t newoff = section_add(cur_text_section, 0,
				    1 << (sym->a.aligned - 1));
	gen_fill_nops(newoff - ind);
    }

    root_scope = new_function_scope(sym);

    /* `NOTE`: we patch the symbol size later */
    put_extern_sym(sym, cur_text_section, ind, 0);

    if (sym->type.ref->f.func_ctor)
        add_array (tcc_state, ".init_array", sym->c);
    if (sym->type.ref->f.func_dtor)
        add_array (tcc_state, ".fini_array", sym->c);

    /* put debug symbol */
    tcc_debug_funcstart(tcc_state, sym);

    /* push a dummy symbol to enable local sym storage */
    sym_push2(&local_stack, SYM_FIELD, 0, 0);
    /* push parameters */
    local_scope = 1;
    sym_push_params(sym->type.ref);

    local_scope = 0;
    rsym = 0;
    nb_temp_local_vars = 0;

    gfunc_prolog(sym);
    tcc_debug_prolog_epilog(tcc_state, 0);
    func_vla_arg(sym);
    block(0);
    if (root_scope->fn.pure == FUNC_PURITY_UNKNOWN) {
        root_scope->fn.pure = FUNC_PURITY_INFERRED_TRUE;
    }
    if (sym->type.ref) {
        sym->type.ref->f.func_pure = root_scope->fn.pure;
    }
    if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN) {
        root_scope->fn.safety = FUNC_SAFETY_INFERRED_SAFE;
    }
    if (sym->type.ref) {
        sym->type.ref->f.func_safety = root_scope->fn.safety;
    }
    gsym(rsym);
    nocode_wanted = 0;
    tcc_debug_end_scope(NULL, !func_var);
    tcc_debug_prolog_epilog(tcc_state, 1);
    gfunc_epilog();

    /* end of function */
    tcc_debug_funcend(tcc_state, ind - func_ind);

    /* patch symbol size */
    elfsym(sym)->st_size = ind - func_ind;
    cur_text_section->data_offset = ind;

    sym_pop(&local_stack, saved_local_stack, 0);
    label_pop(&local_label_stack, saved_local_label_stack, 0);
    label_pop(&global_label_stack, saved_global_label_stack, 0);
    sym_pop(&all_cleanups, saved_cleanups, 0);
    pending_gotos = saved_pending_gotos;

    /* pop the function scope and resume the enclosing function */
    cur_scope = saved_scope;
    root_scope = saved_root;
    loop_scope = saved_loop;
    cur_switch = saved_switch;
    --scope_depth;
    local_scope = saved_local_scope;
    rsym = saved_rsym;
    nb_temp_local_vars = saved_nb_temp_local_vars;
    loc = saved_loc;

    if (scope_depth) {
        ind = saved_ind;
        nocode_wanted = saved_nocode_wanted;
        cur_text_section = saved_text_section;
    } else {
        /* `It`'s better to crash than to generate wrong code */
        cur_text_section = NULL;
        ind = 0;
        nocode_wanted = DATA_ONLY_WANTED;
    }
    check_vstack();

    /* do this after funcend debug info */
    next();
}

static void gen_inline_functions(TCCState *s)
{
    Sym *sym;
    int inline_generated, i;
    struct InlineFunc *fn;

    tcc_open_bf(s, ":inline:", 0);
    /* iterate while inline function are referenced */
    do {
        inline_generated = 0;
        for (i = 0; i < s->nb_inline_fns; ++i) {
            fn = s->inline_fns[i];
            sym = fn->sym;
            if (sym && (sym->c || !(sym->type.t & VT_STORAGE_INLINE))) {
                /* the function was used or forced (and then not internal):
                   generate its code and convert it to a normal function */
                fn->sym = NULL;
                tccpp_putfile(fn->filename);
                begin_macro(fn->func_str, 1);
                next();
                cur_text_section = text_section;
                gen_function(sym);
                end_macro();

                inline_generated = 1;
            }
        }
    } while (inline_generated);
    tcc_close();
}

static void free_inline_functions(TCCState *s)
{
    int i;
    /* free tokens of unused inline functions */
    for (i = 0; i < s->nb_inline_fns; ++i) {
        struct InlineFunc *fn = s->inline_fns[i];
        if (fn->sym)
            tok_str_free(fn->func_str);
    }
    dynarray_reset(&s->inline_fns, &s->nb_inline_fns);
}

/* `Check` that the constant tuple on top of the value stack has only
   integer elements, pop it, and return it.  `What` names the construct in
   the diagnostics.  */
static TupleConst *pop_int_tuple(const char *what)
{
    TupleConst *tc;
    int i;

    if (!is_tuple_type(&vtop->type) || !(tc = tuple_const_of(vtop)))
        tcc_error("%s requires a constant tuple", what);
    for (i = 0; i < tc->n; ++i) {
        int bt = tc->types[i].t & VT_BTYPE;
        if ((tc->nested && tc->nested[i])
            || bt == VT_VOID || bt == VT_FUNC || bt == VT_STRUCT
            || bt == VT_FLOAT || bt == VT_DOUBLE || bt == VT_LDOUBLE
            || (tc->types[i].t & VT_ARRAY))
            tcc_error("%s: element %d is not an integer constant",
                      what, i + 1);
    }
    vpop();
    return tc;
}

/* `Parse` and act on `enum (name1, ..., nameN) = tuple`: bind every
   element of the constant tuple as an integer manifest constant.  `tok`
   is on the '(' of the pattern.  */
static void destructure_manifest_constant(void)
{
    TupleConst *tc;
    void **names = NULL;
    int nb_names = 0, n, i;
    CType type;
    Sym *s;

    skip('(');
    for (;;) {
        if (tok < TOK_UIDENT)
            expect("identifier");
        dynarray_add(&names, &nb_names, (void *)(intptr_t)tok);
        next();
        if (tok != ',')
            break;
        next();
        if (tok == ')')         /* (name,) is a 1-element pattern */
            break;
    }
    skip(')');
    if (tok != '=')
        tcc_error("manifest constant destructuring requires an initializer");
    next();
    expr_const1();
    tc = pop_int_tuple("manifest constant destructuring");
    n = tc->n;
    if (n != nb_names) {
        tcc_free(names);
        tcc_error("manifest constant destructuring pattern has %d names "
                  "but the initializer has %d elements", nb_names, n);
    }
    for (i = 0; i < n; ++i) {
        type = tc->types[i];
        type.t = (type.t & ~VT_STORAGE) | VT_STORAGE_STATIC | VT_ENUM_VAL;
        type.ref = push_enum_ref();
        s = sym_push((int)(intptr_t)names[i], &type, VT_CONST, 0);
        s->enum_val = tc->vals[i].i;
    }
    tcc_free(names);
    skip(';');
}

static int check_d_manifest_constant(int l)
{
    int saved_tok;
    int id;
    int64_t val;
    CType type;
    Sym *ss;
    CType btype;
    AttrDef adbase;

    if (tok != TOK_ENUM)
        return 0;

    saved_tok = tok;
    next(); /* consume `TOK_ENUM` */
    if (tok == '(') {
        destructure_manifest_constant();
        return 1;
    }
    if (tok == '{') {
        unget_tok(saved_tok);
        return 0;
    }
    if (tok >= TOK_UIDENT) {
        id = tok;
        next();
        if (tok == '=') {
            next(); /* consume '=' */
            expr_const1();
            val = vtop->c.i;
            type.t = VT_INT | VT_STORAGE_STATIC | VT_ENUM_VAL;
            type.ref = push_enum_ref();
            ss = sym_push(id, &type, VT_CONST, 0);
            ss->enum_val = val;
            vpop();
            skip(';');
            return 1;
        }
        unget_tok(id);
        unget_tok(saved_tok);
    } else {
        memset(&adbase, 0, sizeof(adbase));
        if (parse_btype(&btype, &adbase, l == VT_LOCAL)) {
            if (tok >= TOK_UIDENT) {
                int id2 = tok;
                next();
                if (tok == '=') {
                    next(); /* consume '=' */
                    expr_const1();
                    val = vtop->c.i;
                    btype.t = (btype.t & ~VT_STORAGE) | VT_STORAGE_STATIC | VT_ENUM_VAL;
                    btype.ref = push_enum_ref();
                    ss = sym_push(id2, &btype, VT_CONST, 0);
                    ss->enum_val = val;
                    vpop();
                    skip(';');
                    return 1;
                }
                tcc_error("expected '=' in manifest constant declaration");
            }
        }
        unget_tok(saved_tok);
    }
    return 0;
}


static int check_d_static_assert(void)
{
    int saved_tok;

    if (tok == TOK_STATIC) {
        saved_tok = tok;
        next();
        if (tok >= TOK_IDENT && strcmp(get_tok_str(tok, &tokc), "assert") == 0) {
            do_Static_assert();
            return 1;
        }
        unget_tok(saved_tok);
    } else if (tok >= TOK_IDENT
               && strcmp(get_tok_str(tok, &tokc), "$assert") == 0) {
        saved_tok = tok;
        next();
        if (tok == '(') {
            unget_tok(saved_tok);
            do_Static_assert();
            return 1;
        }
        unget_tok(saved_tok);
    }
    return 0;
}

static void do_Static_assert(void)
{
    int c;
    const char *msg;

    next();
    skip('(');
    c = expr_const();
    msg = "_Static_assert fail";
    if (tok == ',') {
        next();
        msg = parse_mult_str("string constant")->data;
    }
    skip(')');
    if (c == 0)
        tcc_error("%s", msg);
    skip(';');
}
static void vpush_string(const char *str)
{
    int len = strlen(str) + 1;
    unsigned long offset = section_add(rodata_section, len, 1);
    if (!NODATA_WANTED)
        memcpy(rodata_section->data + offset, str, len);
    vpush_ref(&char_pointer_type, rodata_section, offset, len);
    vtop->type.t = VT_PTR;
}

static void push_sym_value(Sym *s)
{
    int r = s->r;
    if ((r & VT_VALMASK) < VT_CONST)
        r = (r & ~VT_VALMASK) | VT_LOCAL;
    vset(&s->type, r, s->c);
    vtop->sym = s;
    if (r & VT_SYM) {
        vtop->c.i = 0;
    } else if (r == VT_CONST && IS_ENUM_VAL(s->type.t)) {
        vtop->c.i = s->enum_val;
    }
}

/* the type of the temporary that holds the value of 's' when an assertion
   compares two evaluated operands: scalars keep their own (unqualified)
   type so that both the comparison and the failure diagnostic use their
   actual representation, while other types are reduced to a 64-bit
   integer, as before */
static void assert_operand_type(Sym *s, CType *type)
{
    *type = s->type;
    type->t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE | VT_BITFIELD);
    if (!is_scalar_value_type(&s->type)) {
        type->t = VT_LLONG;
        type->ref = NULL;
    }
}

/* evaluate the value of symbol 's' exactly once, by storing it into a
   fresh temporary local variable of type 'type', and leave the resulting
   rvalue on top of the value stack */
static void eval_sym_to_temp(Sym *s, CType *type, int *offset, int *r2)
{
    int size, align;

    size = type_size(type, &align);
    *offset = get_temp_local_var(size, align, r2);
    vset(type, VT_LOCAL | VT_LVAL, *offset);
    vtop->r2 = *r2;
    push_sym_value(s);
    vstore();
    vpop();
}

static void do_d_assert(void)
{
    TokenString *str;
    int a, b;
    int depth = 1, t;
    int saved_tok;

    if (!tcc_state->lang_d)
        tcc_error("'assert' requires -lang=d");

    skip('(');
    str = tok_str_alloc();
    str->prev = NULL;

    do {
        t = tok;
        if (t == TOK_EOF)
            tcc_error("unexpected end of file in assert");

        if (t == '(') depth++;
        else if (t == ')') depth--;

        if (depth > 1 || (depth == 1 && t != ')' && t != '(')) {
            tok_str_add2(str, t, &tokc);
        }
        next();
    } while (depth > 0);
    tok_str_add(str, TOK_EOF);
    saved_tok = tok;

    if (str->len == 4 && str->str[0] >= TOK_IDENT && str->str[2] >= TOK_IDENT) {
        /* simple binary comparison of two identifiers: ident `OP` ident */
        int op = str->str[1];
        int id_l = str->str[0];
        int id_r = str->str[2];
        int cmp_op = TOK_EQ;
        const char *name_l = get_tok_str(id_l, NULL);
        const char *name_r = get_tok_str(id_r, NULL);
        const char *op_str = NULL;
        const char *neg_str = NULL;
        char assertion_buf[256];
        Sym *s_l, *s_r;
        int l_l = 0, r2_l = 0;
        int l_r = 0, r2_r = 0;
        int la, lb, elem_bt, esize, ealign;
        CType slice_type;

        switch (op) {
        case TOK_EQ: op_str = "=="; neg_str = "!="; cmp_op = TOK_EQ; break;
        case TOK_NE: op_str = "!="; neg_str = "=="; cmp_op = TOK_NE; break;
        case TOK_LT: op_str = "<";  neg_str = ">="; cmp_op = TOK_LT; break;
        case TOK_GE: op_str = ">="; neg_str = "<";  cmp_op = TOK_GE; break;
        case TOK_LE: op_str = "<="; neg_str = ">";  cmp_op = TOK_LE; break;
        case TOK_GT: op_str = ">";  neg_str = "<="; cmp_op = TOK_GT; break;
        default:
            tcc_error("unsupported operator in D assert: %s",
                      get_tok_str(op, NULL));
            break;
        }
        snprintf(assertion_buf, sizeof(assertion_buf), "%s %s %s",
                 name_l, op_str, name_r);

        s_l = sym_find(id_l);
        if (!s_l)
            tcc_error("undeclared identifier '%s'", name_l);
        s_r = sym_find(id_r);
        if (!s_r)
            tcc_error("undeclared identifier '%s'", name_r);

        if ((op == TOK_EQ || op == TOK_NE) &&
            ((s_l->type.t & (VT_ARRAY | VT_VLA)) || is_slice_type(&s_l->type)) &&
            ((s_r->type.t & (VT_ARRAY | VT_VLA)) || is_slice_type(&s_r->type))) {
            /* array or slice comparison: evaluate both sequences into
               temporaries and report them through the runtime helper */
            CType elem;
            int r;

            /* push the left operand */
            r = s_l->r;
            if ((r & VT_VALMASK) < VT_CONST)
                r = (r & ~VT_VALMASK) | VT_LOCAL;
            vset(&s_l->type, r, s_l->c);
            vtop->sym = s_l;
            /* push the right operand */
            r = s_r->r;
            if ((r & VT_VALMASK) < VT_CONST)
                r = (r & ~VT_VALMASK) | VT_LOCAL;
            vset(&s_r->type, r, s_r->c);
            vtop->sym = s_r;

            get_array_or_slice_elem(&vtop->type, &elem);
            elem_bt = elem.t & VT_BTYPE;
            slice_type.t = VT_STRUCT;
            slice_type.ref = make_slice_type(&elem);
            esize = type_size(&elem, &ealign);

            /* `Right` operand is at vtop */
            if (is_slice_type(&vtop->type)) {
                lb = copy_slice_to_temp(&vtop->type);
            } else if (vtop->type.t & (VT_ARRAY | VT_VLA)) {
                if (!is_compatible_unqualified_types(&vtop->type.ref->type, &elem))
                    cast_error(&vtop->type, &slice_type);
                convert_array_to_slice(&slice_type);
                lb = copy_slice_to_temp(&slice_type);
            } else {
                tcc_error("cannot compare array/slice with non-array/non-slice");
            }

            /* `Left` operand is now at vtop */
            if (is_slice_type(&vtop->type)) {
                la = copy_slice_to_temp(&vtop->type);
            } else if (vtop->type.t & (VT_ARRAY | VT_VLA)) {
                if (!is_compatible_unqualified_types(&vtop->type.ref->type, &elem))
                    cast_error(&vtop->type, &slice_type);
                convert_array_to_slice(&slice_type);
                la = copy_slice_to_temp(&slice_type);
            } else {
                tcc_error("cannot compare array/slice with non-array/non-slice");
            }

            vpush_helper_func(TOK___darray_eq);
            vset(&char_pointer_type, VT_LOCAL, la);
            vset(&char_pointer_type, VT_LOCAL, lb);
            vpushs(esize);
            gfunc_call(3);
            vpushi(0);
            vtop->type.t = VT_INT;
            vtop->r = REG_IRET;
            if (op == TOK_NE) {
                vpushi(0);
                gen_op(TOK_EQ);
            }
            a = gvtst(1, 0);
            b = gjmp(0);
            gsym(a);
            /* failure: pass the evaluated sequences to the runtime helper */
            vpush_helper_func(tok_alloc_const("__d_assert_cmp_arr"));
            vpush_string(assertion_buf);
            vpush_string(neg_str);
            vset(&char_pointer_type, VT_LOCAL, la);
            vset(&char_pointer_type, VT_LOCAL, lb);
            vpushs(esize);
            vpushi(elem_bt);
            vpush_string(name_l);
            vpush_string(name_r);
            gfunc_call(8);
            gsym(b);
        } else {
        CType type_l, type_r;
        int size_l, size_r, align;

        /* evaluate each operand exactly once into a temporary local */
        assert_operand_type(s_l, &type_l);
        assert_operand_type(s_r, &type_r);
        eval_sym_to_temp(s_l, &type_l, &l_l, &r2_l);
        /* keep the $L temporary allocated so that $R gets a distinct one */
        vset(&type_l, VT_LOCAL | VT_LVAL, l_l);
        vtop->r2 = r2_l;
        eval_sym_to_temp(s_r, &type_r, &l_r, &r2_r);
        vpop(); /* discard placeholder marking the $L temporary as used */

        /* generate the comparison over the evaluated temporaries */
        vset(&type_l, VT_LOCAL | VT_LVAL, l_l);
        vtop->r2 = r2_l;
        vset(&type_r, VT_LOCAL | VT_LVAL, l_r);
        vtop->r2 = r2_r;
        gen_op(cmp_op);
        a = gvtst(1, 0);
        b = gjmp(0);
        gsym(a);
        /* failure: pass the evaluated values to the runtime helper */
        size_l = type_size(&type_l, &align);
        size_r = type_size(&type_r, &align);
        vpush_helper_func(tok_alloc_const("__d_assert_cmp"));
        vpush_string(assertion_buf);
        vpush_string(neg_str);
        vset(&char_pointer_type, VT_LOCAL, l_l);
        vpushi(type_l.t);
        vpushi(size_l);
        vset(&char_pointer_type, VT_LOCAL, l_r);
        vpushi(type_r.t);
        vpushi(size_r);
        gfunc_call(8);
        gsym(b);
        tok = saved_tok;
        }
    } else {
        begin_macro(str, 1);
        next();
        gexpr();
        a = gvtst(1, 0);
        b = gjmp(0);
        gsym(a);
        vpush_helper_func(TOK___d_assert);
        if (tok == ',') {
            next();
            gexpr();
        } else {
            vpushi(0);
        }
        gfunc_call(1);
        gsym(b);
        end_macro();
        tok = saved_tok;
    }
    tok_str_free(str);
}

#ifdef TCC_TARGET_PE
static void pe_check_linkage(CType *type, AttrDef *ad)
{
    if (!ad->a.dllimport && !ad->a.dllexport)
        return;
    if (type->t & VT_STORAGE_STATIC)
        tcc_error("cannot have dll linkage with static");
    if (type->t & VT_STORAGE_TYPEDEF) {
        const char *m = ad->a.dllimport ? "im" : "ex";
        tcc_warning("'dll%sport' attribute ignored for typedef", m);
        ad->a.dllimport = 0;
        ad->a.dllexport = 0;
    } else if (ad->a.dllimport) {
        if ((type->t & VT_BTYPE) == VT_FUNC)
            ad->a.dllimport = 0;
        else
            type->t |= VT_STORAGE_EXTERN;
    }
}
#endif

/* `Register` 'type' as a typedef named 'v'.  `Both` C 'typedef' and D-style
   'alias' declarations use this representation.  */
static void declare_typedef(int v, CType *type, AttrDef *ad)
{
    Sym *sym;

    /* save typedefed type  */
    /* `XXX`: test storage specifiers ? */
    sym = sym_find(v);
    if (sym && sym->sym_scope == local_scope) {
        if (!is_compatible_types(&sym->type, type)
            || !(sym->type.t & VT_STORAGE_TYPEDEF))
            tcc_error("incompatible redefinition of '%s'",
                get_tok_str(v, NULL));
        sym->type = *type;
    } else {
        sym = sym_push(v, type, 0, 0);
    }
    sym->a = ad->a;
    if ((type->t & VT_BTYPE) == VT_FUNC)
      merge_funcattr(&sym->type.ref->f, &ad->f);
    if (debug_modes)
        tcc_debug_typedef (tcc_state, sym);
}

/* D-style type alias:  alias `Name` = `Type`;  */
static void do_d_alias(void)
{
    int v, dummy;
    CType type;
    AttrDef ad;

    next();
    if (tok < TOK_UIDENT)
        expect("identifier");
    v = tok;
    next();
    skip('=');
    if (!parse_btype(&type, &ad, 0))
        expect("type");
    while (tok == '[')
        d_array_suffix(&type, 0);
    type_decl(&type, &ad, &dummy, TYPE_ABSTRACT);
    type.t |= VT_STORAGE_TYPEDEF;
    declare_typedef(v, &type, &ad);
    skip(';');
}

/* D-style 'unittest { ... }' block.  `With` -unittest it becomes a
   constructor function that runs before main.  `The` current token must
   be the opening '{'.  */
static TCC_TLS int nb_unittest;

static void do_unittest(void)
{
    CType type;
    Sym *sym, *sr;
    AttrDef ad;
    char name[64];

    memset(&ad, 0, sizeof ad);
    snprintf(name, sizeof name, "__unittest_%d", nb_unittest++);

    /* build the type of 'static void `__unittest_N`(void)' */
    type.t = VT_VOID;
    type.ref = NULL;
    sr = sym_push(SYM_FIELD, &type, 0, 0);
    sr->f.func_type = FUNC_NEW;
    sr->f.func_call = FUNC_CDECL;
    sr->f.func_args = 0;
    sr->f.func_ctor = 1;
    sr->next = NULL;
    type.t = VT_FUNC | VT_STORAGE_STATIC;
    type.ref = sr;

    sym = external_sym(tok_alloc(name, strlen(name))->tok, &type, 0, &ad);
    cur_text_section = text_section;
    gen_function(sym);
}

/* D conditional compilation (version and debug blocks).  */

static int d_ident_is(int t, const char *name)
{
    return t >= TOK_UIDENT && !strcmp(get_tok_str(t, &tokc), name);
}

static int d_tok_is_int(int t)
{
    return t == TOK_CINT || t == TOK_CUINT
        || t == TOK_CLONG || t == TOK_CULONG
        || t == TOK_CLLONG || t == TOK_CULLONG;
}

static int d_id_is_defined(char **ids, int nb_ids, const char *id)
{
    int i;
    for (i = 0; i < nb_ids; ++i)
        if (!strcmp(ids[i], id))
            return 1;
    return 0;
}

ST_FUNC void tcc_add_d_version(TCCState *s, const char *id)
{
    if (!d_id_is_defined(s->d_version_ids, s->nb_d_version_ids, id))
        dynarray_add(&s->d_version_ids, &s->nb_d_version_ids,
                     tcc_strdup(id));
}

ST_FUNC void tcc_add_d_debug(TCCState *s, const char *id)
{
    if (!d_id_is_defined(s->d_debug_ids, s->nb_d_debug_ids, id))
        dynarray_add(&s->d_debug_ids, &s->nb_d_debug_ids,
                     tcc_strdup(id));
}

static int d_version_id_is_defined(const char *id)
{
    TCCState *s = tcc_state;

    /* "none" and "all" are special version identifiers that cannot be
       overridden by a version specification or a command-line option.  */
    if (!strcmp(id, "none"))
        return 0;
    if (!strcmp(id, "all"))
        return 1;
    if (d_id_is_defined(s->d_version_ids, s->nb_d_version_ids, id))
        return 1;
    if (!strcmp(id, "D_Version2"))
        return 1;
    if (!strcmp(id, "unittest"))
        return s->do_unittest;
    if (!strcmp(id, "assert"))
        return 1;
#if defined TCC_TARGET_I386
    if (!strcmp(id, "X86"))
        return 1;
#elif defined TCC_TARGET_X86_64
    if (!strcmp(id, "X86_64"))
        return 1;
#elif defined TCC_TARGET_ARM
    if (!strcmp(id, "ARM"))
        return 1;
#elif defined TCC_TARGET_ARM64
    if (!strcmp(id, "AArch64"))
        return 1;
#elif defined TCC_TARGET_RISCV64
    if (!strcmp(id, "RISCV64"))
        return 1;
#endif
#if defined TCC_TARGET_PE
    if (!strcmp(id, "Windows"))
        return 1;
# if PTR_SIZE == 8
    if (!strcmp(id, "Win64"))
        return 1;
# else
    if (!strcmp(id, "Win32"))
        return 1;
# endif
#elif defined TCC_TARGET_MACHO
    if (!strcmp(id, "OSX") || !strcmp(id, "Darwin"))
        return 1;
#endif
#if defined TARGETOS_ANDROID
    if (!strcmp(id, "Android") || !strcmp(id, "linux"))
        return 1;
#elif defined TARGETOS_FreeBSD || defined TARGETOS_FreeBSD_kernel
    if (!strcmp(id, "FreeBSD"))
        return 1;
#elif defined TARGETOS_OpenBSD
    if (!strcmp(id, "OpenBSD"))
        return 1;
#elif defined TARGETOS_NetBSD
    if (!strcmp(id, "NetBSD"))
        return 1;
#elif defined TARGETOS_DragonFly
    if (!strcmp(id, "DragonFlyBSD"))
        return 1;
#elif defined TARGETOS_Linux
    if (!strcmp(id, "linux"))
        return 1;
#endif
#if !defined TCC_TARGET_PE && !defined TCC_TARGET_C67
    if (!strcmp(id, "Posix"))
        return 1;
#endif
#if !defined TCC_TARGET_C67
    if (!strcmp(id, "LittleEndian"))
        return 1;
#else
    if (!strcmp(id, "BigEndian"))
        return 1;
#endif
    return 0;
}

static int d_debug_id_is_defined(const char *id)
{
    TCCState *s = tcc_state;
    return d_id_is_defined(s->d_debug_ids, s->nb_d_debug_ids, id);
}

static void d_scan_body(TokenString *str, int file_scope);

/* `Scan` a parenthesized token sequence, saving it in `STR` if not `NULL`.
   `The` current token must be '('.  */
static void d_scan_parens(TokenString *str)
{
    int depth = 0, t;
    do {
        t = tok;
        if (t == TOK_EOF)
            tcc_error("unexpected end of file in version/debug condition");
        if (str)
            tok_str_add_tok(str);
        next();
        if (t == '(')
            ++depth;
        else if (t == ')')
            --depth;
    } while (depth > 0);
}

/* `Scan` a balanced brace group, saving it in `STR` if not `NULL`.  */
static void d_scan_braces(TokenString *str)
{
    int depth = 0, t;
    do {
        t = tok;
        if (t == TOK_EOF)
            tcc_error("unexpected end of file in version/debug block");
        if (str)
            tok_str_add_tok(str);
        next();
        if (t == '{')
            ++depth;
        else if (t == '}')
            --depth;
    } while (depth > 0);
}

/* `Scan` a declaration or expression statement.  `Top`-level brace groups
   are skipped, but a function body terminates the scan when it is not
   followed by a declarator separator.  */
static void d_scan_decl_or_expr(TokenString *str)
{
    int level = 0, has_init = 0, t;
    while (tok != TOK_EOF) {
        t = tok;
        if (level == 0 && t == ';') {
            if (str)
                tok_str_add_tok(str);
            next();
            return;
        }
        if (level == 0 && (t == '}' || t == TOK_ELSE))
            return;
        if (level == 0 && t == '{') {
            d_scan_braces(str);
            if (!has_init && tok != ';' && tok != ',')
                return;
            has_init = 0;
            continue;
        }
        if (str)
            tok_str_add_tok(str);
        if (t == '=')
            has_init = 1;
        if (t == '(' || t == '[')
            ++level;
        else if (t == ')' || t == ']') {
            if (level)
                --level;
        }
        next();
    }
}

/* `Scan` one D declaration or statement, appending it to `STR` if not `NULL`.
   `At` file scope a leading '{' is stripped and its declarations are
   scanned individually.  */
static void d_scan_body(TokenString *str, int file_scope)
{
    int t = tok;

    if (t == '{') {
        if (file_scope) {
            next();
            while (tok != '}' && tok != TOK_EOF)
                d_scan_body(str, 1);
            if (tok != '}')
                tcc_error("unexpected end of file in version/debug block");
            next();
        } else {
            d_scan_braces(str);
        }
        return;
    }
    if (t == TOK_IF || d_ident_is(t, "$if")) {
        if (str)
            tok_str_add_tok(str);
        next();
        d_scan_parens(str);
        d_scan_body(str, 0);
        if (tok == TOK_ELSE) {
            if (str)
                tok_str_add_tok(str);
            next();
            d_scan_body(str, 0);
        }
        return;
    }
    if (t == TOK_FOR || t == TOK_WHILE || t == TOK_SWITCH
        || t == TOK_FOREACH || t == TOK_FOREACH_REVERSE
        || d_ident_is(t, "$foreach")) {
        if (str)
            tok_str_add_tok(str);
        next();
        d_scan_parens(str);
        d_scan_body(str, 0);
        return;
    }
    if (t == TOK_DO) {
        if (str)
            tok_str_add_tok(str);
        next();
        d_scan_body(str, 0);
        if (tok == TOK_WHILE) {
            if (str)
                tok_str_add_tok(str);
            next();
            d_scan_parens(str);
        }
        if (tok == ';') {
            if (str)
                tok_str_add_tok(str);
            next();
        }
        return;
    }
    if (d_ident_is(t, "version") || d_ident_is(t, "debug")) {
        int is_debug = d_ident_is(t, "debug");
        if (str)
            tok_str_add_tok(str);
        next();
        if (tok == '=') {
            d_scan_decl_or_expr(str);
        } else {
            if (tok == '(')
                d_scan_parens(str);
            else if (!is_debug)
                expect("'('");
            d_scan_body(str, file_scope);
            if (tok == TOK_ELSE) {
                if (str)
                    tok_str_add_tok(str);
                next();
                d_scan_body(str, file_scope);
            }
        }
        return;
    }
    d_scan_decl_or_expr(str);
}

/* `Scan` both branches of a D conditional and replay the selected one.
   `On` entry 'tok' is the token that follows the condition.  */
static void d_conditional_body(int l, int flags, int take)
{
    TokenString *body, *else_body, *sel;

    body = tok_str_alloc();
    d_scan_body(body, l == VT_CONST);
    tok_str_add(body, TOK_EOF);
    else_body = NULL;
    if (tok == TOK_ELSE) {
        next();
        else_body = tok_str_alloc();
        d_scan_body(else_body, l == VT_CONST);
        tok_str_add(else_body, TOK_EOF);
    }

    sel = take ? body : else_body;
    if (sel != body)
        tok_str_free(body);
    if (else_body && sel != else_body)
        tok_str_free(else_body);
    if (sel) {
        if (sel->len > 1) {
            /* `Keep` the token following the conditional in a macro
               stream so that it is restored after the replay.  */
            unget_tok(0);
            begin_macro(sel, 1);
            next();
            if (l == VT_CONST) {
                decl(VT_CONST);
            } else {
                decl(VT_LOCAL);
                if (tok != TOK_EOF)
                    block(flags);
            }
            while (tok != TOK_EOF)
                next();
            end_macro();
            next();
        } else {
            tok_str_free(sel);
        }
    }
}

/* `Parse` and act on a D 'version' or 'debug' specification or block.
   `Returns` nonzero when the current token started such a construct.  */
static int check_d_conditional(int l, int flags)
{
    int is_debug, take;

    if (!tcc_state->lang_d || (l != VT_CONST && l != VT_LOCAL))
        return 0;
    if (!(d_ident_is(tok, "version") || d_ident_is(tok, "debug")))
        return 0;

    is_debug = d_ident_is(tok, "debug");
    next();
    if (tok == '=') {
        next();
        if (tok >= TOK_UIDENT) {
            const char *id = get_tok_str(tok, &tokc);
            if (is_debug)
                tcc_add_d_debug(tcc_state, id);
            else
                tcc_add_d_version(tcc_state, id);
            next();
        } else if (d_tok_is_int(tok)) {
            unsigned level = tokc.i;
            if (is_debug)
                tcc_state->d_debug_level = level;
            else
                tcc_state->d_version_level = level;
            next();
        } else {
            expect("identifier or integer literal");
        }
        skip(';');
        return 1;
    }

    if (tok == '(') {
        next();
        if (tok == TOK_UNITTEST) {
            take = !is_debug && tcc_state->do_unittest;
            next();
        } else if (tok >= TOK_UIDENT || tok == TOK_ASSERT) {
            const char *id = (tok == TOK_ASSERT) ? "assert" : get_tok_str(tok, &tokc);
            take = is_debug ? d_debug_id_is_defined(id)
                            : d_version_id_is_defined(id);
            next();
        } else if (d_tok_is_int(tok)) {
            uint64_t level = tokc.i;
            take = is_debug ? (uint64_t)tcc_state->d_debug_level >= level
                            : (uint64_t)tcc_state->d_version_level >= level;
            next();
        } else {
            expect("version identifier or integer literal");
        }
        skip(')');
    } else {
        if (!is_debug)
            expect("'('");
        take = tcc_state->d_debug_level > 0;
    }
    if (tok == ':')
        tcc_error("version/debug colon declarations are not supported");
    d_conditional_body(l, flags, take);
    return 1;
}

/* `Parse` and act on `$if (c) ... [else ...]`.  `The` condition must be a
   compile-time constant and only the selected branch is parsed, so the
   other one may contain code that is invalid for this target.  */
static int check_d_dollar_if(int l, int flags)
{
    int take;

    if (!tcc_state->lang_d || (l != VT_CONST && l != VT_LOCAL))
        return 0;
    if (!d_ident_is(tok, "$if"))
        return 0;
    next();
    skip('(');
    take = expr_const() != 0;
    skip(')');
    if (tok == ':')
        tcc_error("'$if' colon declarations are not supported");
    d_conditional_body(l, flags, take);
    return 1;
}

/* `Save` the tokens of an initializer up to the ';' or ',' that ends the
   declaration at the current nesting level, followed by `TOK_EOF`.
   `Return` that terminating token.  */
static int save_decl_initializer(TokenString *str)
{
    int level = 0, end_tok = ';';

    while (tok != TOK_EOF) {
        int t = tok;
        if (level == 0 && (t == ';' || t == ',')) {
            end_tok = t;
            break;
        }
        tok_str_add_tok(str);
        if (t == '(' || t == '[' || t == '{')
            ++level;
        else if (t == ')' || t == ']' || t == '}') {
            if (level)
                --level;
        }
        next();
    }
    tok_str_add(str, TOK_EOF);
    return end_tok;
}

/* `If` the saved initializer 'init_str' is one parenthesized tuple literal
   that covers the whole expression, build the tuple type from its
   elements and store it in 'type'.  `Return` zero if it is not, so that
   the caller falls back to deducing the type of the expression.  */
static int destructure_tuple_literal(TokenString *init_str, CType *type)
{
    TokenString *ts, *dup;
    Sym *s;
    Sym **plast;
    int has_comma, nfields = 0, saved_nocode_wanted;

    if (!init_str->len)
        return 0;
    begin_macro(init_str, 0);
    next();                     /* first token of the initializer */
    if (tok != '(') {
        end_macro();
        return 0;
    }
    next();                     /* first token inside the group */
    ts = scan_paren_group(&dup, &has_comma, 0);
    tok_str_free(ts);
    if (dup)
        tok_str_free(dup);
    /* 'tok' is on the closing ')': the group covers the whole initializer
       only if the stream ends right behind it */
    next();
    if (tok != TOK_EOF || !has_comma) {
        end_macro();
        return 0;
    }
    end_macro();

    begin_macro(init_str, 0);
    next();                     /* the '(' of the group */
    next();                     /* first element */
    s = tuple_begin();
    plast = &s->next;
    saved_nocode_wanted = nocode_wanted;
    ++nocode_wanted;
    for (;;) {
        CType el;
        expr_eq();
        el = vtop->type;
        if (!(el.t & VT_ARRAY))
            convert_expression_type(&el);
        vpop();
        tuple_add_field(&plast, &el, nfields++);
        if (tok != ',')
            break;
        next();
        if (tok == ')')         /* (e,) is a 1-tuple */
            break;
    }
    nocode_wanted = saved_nocode_wanted;
    if (tok != ')')
        expect("')'");
    next();
    end_macro();
    type->t = VT_STRUCT;
    type->ref = tuple_layout(s);
    return 1;
}

/* `Parse` and act on a destructuring declaration
   `auto (name1, ..., nameN) = init`, with 'tok' on the '(' of the pattern.
   `Bind` every name to one element of a single tuple temporary, so that
   'init' is evaluated exactly once.  `Return` the token that ends the
   declaration (',' or ';').  */
static int decl_auto_tuple(AttrDef *ad)
{
    TokenString *init_str;
    CType deduced, ttype;
    Sym *f;
    void **names = NULL;
    int nb_names = 0, arity, i, addr, end_tok, ofs, r;
    skip('(');
    for (;;) {
        if (tok < TOK_UIDENT)
            expect("identifier");
        dynarray_add(&names, &nb_names, (void *)(intptr_t)tok);
        next();
        if (tok != ',')
            break;
        next();
        if (tok == ')')         /* (name,) is a 1-element pattern */
            break;
    }
    skip(')');
    if (tok != '=')
        tcc_error("destructuring declaration requires an initializer");
    next();

    init_str = tok_str_alloc();
    end_tok = save_decl_initializer(init_str);

    if (!destructure_tuple_literal(init_str, &deduced)) {
        begin_macro(init_str, 0);
        next();
        expr_type(&deduced, gexpr);
        if (!(deduced.t & VT_ARRAY))
            convert_expression_type(&deduced);
        end_macro();
    }
    if (!is_tuple_type(&deduced)) {
        char buf[256];
        type_to_str(buf, sizeof buf, &deduced, NULL);
        tcc_free(names);
        tcc_error("cannot destructure non-tuple type '%s'", buf);
    }
    arity = tuple_arity(&deduced);
    if (arity != nb_names) {
        tcc_free(names);
        tcc_error("tuple destructuring pattern has %d names but the "
                  "initializer has %d elements", nb_names, arity);
    }

    /* one temporary, initialized from the replayed tokens */
    ttype = deduced;
    ttype.t &= ~VT_QUALIFIER;
    begin_macro(init_str, 1);   /* frees 'init_str' at end_macro() */
    next();
    decl_initializer_alloc(&ttype, ad, VT_LOCAL | VT_LVAL, 1, 0, VT_LOCAL);
    end_macro();
    addr = vtop->c.i;
    vpop();
#ifdef CONFIG_TCC_BCHECK
    if (tcc_state->do_bounds_check) {
        /* The temporary has no symbol, so `add_local_bounds()` never sees
           it; register it here or an aggregate element copy trips the
           checked `memmove` destination.  */
        int talign;
        add_local_bound(addr, type_size(&ttype, &talign));
    }
#endif

    for (i = 0; i < nb_names; ++i) {
        int name = (int)(intptr_t)names[i];
        f = tuple_field(&ttype, i, &ofs);
        r = VT_LOCAL;
        if (!(f->type.t & VT_ARRAY))
            r |= VT_LVAL;
        sym_push(name, &f->type, r, addr + ofs);
    }
    tcc_free(names);
    unget_tok(end_tok);
    return end_tok;
}

/* `Return` a copy of the token string 's'.  */
static TokenString *tok_str_clone(TokenString *s)
{
    TokenString *str = tok_str_alloc();
    int i;

    for (i = 0; i < s->len; ++i)
        tok_str_add(str, s->str[i]);
    return str;
}

/* `Parse` and act on `$foreach (name; tuple) body`: replay 'body' once per
   element of the compile-time constant tuple 'tuple', binding 'name' as an
   integer manifest constant in a fresh scope per iteration.  */
/* `Check` whether element 'i' of 'tc' is a plain integer constant, i.e. one
   that can be bound as a manifest constant in `$foreach`.  */
static int tuple_const_is_int(TupleConst *tc, int i)
{
    int bt = tc->types[i].t & VT_BTYPE;

    if ((tc->nested && tc->nested[i]) || (tc->syms && tc->syms[i]))
        return 0;
    return bt != VT_VOID && bt != VT_FUNC && bt != VT_STRUCT
        && bt != VT_FLOAT && bt != VT_DOUBLE && bt != VT_LDOUBLE
        && bt != VT_AUTO
        && !(tc->types[i].t & VT_ARRAY);
}

/* `Copy` element 'i' of the constant tuple 'tc' into the local at 'addr'.  */
static void gen_const_elem_store(TupleConst *tc, int i, int addr)
{
    CType type = tc->types[i];
    int align;
    (void)type_size(&type, &align);

    if (type.t & VT_ARRAY) {
        int esize, ealign;

        esize = type_size(&type, &ealign);
        tuple_const_push(tc, i);
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound(); /* check would be wrong after gaddrof() */
#endif
        gaddrof();  /* source address */
        vset(&char_pointer_type, VT_LOCAL | VT_LVAL, addr);
        vswap();
        vpushv(vtop - 1);
        vtop->type.t = VT_PTR;
        gaddrof();  /* destination address */
        vswap();
        gen_copy_bytes(esize, ealign);
        vpop();
    } else {
        tuple_const_push(tc, i);
        vset(&type, VT_LOCAL | VT_LVAL, addr);
        vswap();
        vstore();
        vpop();
    }
}

static int check_d_dollar_foreach(int l, int flags)
{
    TokenString *body;
    TupleConst *tc;
    int name, i, n;

    if (!tcc_state->lang_d || (l != VT_CONST && l != VT_LOCAL))
        return 0;
    if (!d_ident_is(tok, "$foreach"))
        return 0;
    next();
    skip('(');
    if (tok < TOK_UIDENT)
        expect("identifier");
    name = tok;
    next();
    skip(';');
    expr_eq();
    if (!is_tuple_type(&vtop->type) || !(tc = tuple_const_of(vtop)))
        tcc_error("'$foreach' requires a constant tuple");
    vpop();
    skip(')');

    n = tc->n;

    body = tok_str_alloc();
    d_scan_body(body, l == VT_CONST);
    tok_str_add(body, TOK_EOF);
    /* `Keep` the token that follows the body in a macro stream so that it
       is restored after the last replay.  */
    unget_tok(0);

    for (i = 0; i < n; ++i) {
        TokenString *run = tok_str_clone(body);
        CType type = tc->types[i];
        int is_int = tuple_const_is_int(tc, i);
        Scope *po = NULL;
        Sym **pstack = NULL;
        Sym *ref = NULL, *s;

        if (l == VT_CONST && !is_int)
            tcc_error("'$foreach': element %d is not an integer constant",
                      i + 1);
        if (l == VT_LOCAL) {
            po = new_scope();
        } else {
            pstack = &global_stack;
        }
        if (is_int) {
            type.t = (type.t & ~VT_STORAGE) | VT_STORAGE_STATIC | VT_ENUM_VAL;
            type.ref = ref = push_enum_ref();
            s = sym_push(name, &type, VT_CONST, 0);
            s->enum_val = tc->vals[i].i;
            if (tc->syms && tc->syms[i]) {
                s->asm_label = tc->syms[i]->asm_label;
                s->c = tc->syms[i]->c;
            }
        } else if (tc->syms && tc->syms[i]) {
            s = sym_push(name, &type, VT_CONST, 0);
            s->asm_label = tc->syms[i]->asm_label;
            s->c = tc->syms[i]->c;
        } else {
            AttrDef ad2;
            int r = VT_LOCAL;

            if (!(type.t & VT_ARRAY))
                r |= VT_LVAL;
            memset(&ad2, 0, sizeof ad2);
            type.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
            decl_initializer_alloc(&type, &ad2, r, 0, name, VT_LOCAL);
            s = local_stack;
            if (!s || s->v != name)
                tcc_internal_error("bad '$foreach' local");
            gen_const_elem_store(tc, i, s->c);
        }

        begin_macro(run, 1);
        next();
        if (l == VT_CONST) {
            decl(VT_CONST);
        } else {
            decl(VT_LOCAL);
            if (tok != TOK_EOF)
                block(flags);
        }
        while (tok != TOK_EOF)
            next();
        end_macro();
        if (po) {
            prev_scope(po, 0);
        } else {
            /* file scope: drop only the bindings, keep the declarations */
            sym_unlink(pstack, s);
            if (ref)
                sym_unlink(pstack, ref);
        }
    }
    tok_str_free(body);
    next();
    return 1;
}

/* 'l' is `VT_LOCAL` or `VT_CONST` to define default storage type
   or `VT_CMP` if parsing old style parameter list
   or `VT_JMP` if parsing c99 for decl: for (int i = 0, ...) */
static int decl(int l)
{
    int v, has_init, r, oldint, end_tok = 0;
    TokenString *init_str;
    CType type, btype;
    Sym *sym, *sa;
    AttrDef ad, adbase;
    ElfSym *esym;

    while (1) {
        int colon_assign = 0;
        if (tok >= TOK_UIDENT) {
            int saved_tok = tok;
            next();
            if (tok == TOK_COLON_ASSIGN) {
                next(); /* consume := */
                btype.t = VT_AUTO;
                btype.ref = NULL;
                memset(&adbase, 0, sizeof(adbase));
                unget_tok(saved_tok);
                colon_assign = 1;
            } else {
                unget_tok(saved_tok);
            }
        }
        memset(&adbase, 0, sizeof(adbase));
        oldint = 0;
        if (check_d_conditional(l, 0))
            continue;
        if (check_d_dollar_if(l, 0))
            continue;
        if (check_d_dollar_foreach(l, 0))
            continue;
        if (check_d_static_assert())
            continue;
        if (check_d_manifest_constant(l))
            continue;
        if (tok == TOK_UNITTEST) {
            if (l != VT_CONST)
                tcc_error("unittest block is only allowed at file scope");
            next();
            if (tok != '{')
                expect("'{'");
            if (tcc_state->do_unittest)
                do_unittest();
            else
                skip_or_save_block(NULL);
            continue;
        }
        if (tok == TOK_ALIAS1) {
            if (l != VT_CONST && l != VT_LOCAL)
                tcc_error("alias declaration is only allowed at file or block scope");
            do_d_alias();
            continue;
        }
        if (!colon_assign && !parse_btype(&btype, &adbase, l == VT_LOCAL)) {
            if (l == VT_JMP)
                return 0;
            /* skip redundant ';' if not in old parameter decl scope */
            if (tok == ';' && l != VT_CMP) {
                next();
                continue;
            }
            if (tok == TOK_STATIC_ASSERT) {
                do_Static_assert();
                continue;
            }
            if (l != VT_CONST) {
                if (tok == '{') {
                    pending_block_safety = adbase.f.func_safety;
                    pending_block_pure = adbase.f.func_pure;
                }
                break;
            }
            if (tok == TOK_ASM1 || tok == TOK_ASM2 || tok == TOK_ASM3) {
                /* global asm block */
                asm_global_instr();
                continue;
            }
            if (tok >= TOK_UIDENT) {
               /* special test for old K&R protos without explicit int
                  type. `Only` accepted when defining global data */
                btype.t = VT_INT;
                oldint = 1;
            } else {
                if (tok != TOK_EOF)
                    expect("declaration");
                break;
            }
        }

        /* D-style array suffixes apply to every declarator of the
           declaration, as in D */
        while (tok == '[')
            d_array_suffix(&btype, btype.t & VT_STORAGE);

        if (tok == ';') {
            if ((btype.t & VT_BTYPE) == VT_STRUCT
                && (btype.ref->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
                ; /* struct decl with named tag */
            else if (IS_ENUM(btype.t))
                ; /* enum decl */
            else
                tcc_warning("useless type defines no instances");
            if (l == VT_JMP)
                return 1;
            next();
            continue;
        }

        while (1) { /* iterate thru each declaration */
            type = btype;
	    ad = adbase;
            init_str = NULL;
            has_init = 0;
            if (has_auto_type(&btype) && tok == '(') {
                TokenString *pats, *pdup;
                int has_comma;

                next();         /* first token inside the group */
                pats = scan_paren_group(&pdup, &has_comma, 0);
                if (pdup)
                    tok_str_free(pdup);
                if (has_comma) {
                    if (l != VT_LOCAL)
                        tcc_error("tuple destructuring is only allowed at "
                                  "block scope");
                    begin_macro(pats, 1);
                    next();     /* the '(' of the pattern */
                    end_tok = decl_auto_tuple(&ad);
                    if (end_tok == ';') {
                        skip(';');
                        break;
                    }
                    next();     /* ',' */
                    continue;
                }
                /* not a pattern: replay the group for type_decl() */
                begin_macro(pats, 1);
                next();         /* the '(' of the declarator */
            }
            type_decl(&type, &ad, &v, l == VT_CMP ? TYPE_DIRECT | TYPE_PARAM : TYPE_DIRECT);
            if (cur_tcc_state && cur_tcc_state->dump_types)
                ptype("TYPE", &type, v);

            if (has_auto_type(&type)) {
                if (!colon_assign) {
                    if (tok != '=')
                        tcc_error("declaration with 'auto' has no initializer");
                    next();
                }
                has_init = 1;
                init_str = tok_str_alloc();
                {
                    int level = 0;
                    while (tok != TOK_EOF) {
                        int t = tok;
                        if (level == 0 && (t == ';' || t == ',')) {
                            end_tok = t;
                            break;
                        }
                        tok_str_add_tok(init_str);
                        if (t == '(' || t == '[' || t == '{')
                            ++level;
                        else if (t == ')' || t == ']' || t == '}') {
                            if (level)
                                --level;
                        }
                        next();
                    }
                    tok_str_add(init_str, TOK_EOF);
                }

                begin_macro(init_str, 1);
                next();
                {
                    CType deduced_type;
                    expr_type(&deduced_type, gexpr);
                    if (!(deduced_type.t & VT_ARRAY))
                        convert_expression_type(&deduced_type);
                    subst_auto_type(&type, &deduced_type);
                }
                end_macro();
            }
            if ((type.t & VT_BTYPE) == VT_FUNC) {
                if ((type.t & VT_STORAGE_STATIC) && (l != VT_CONST))
                    tcc_error("function without file scope cannot be static");
                /* if old style function prototype, we accept a
                   declaration list */
                sym = type.ref;
                if (sym->f.func_type == FUNC_OLD && l == VT_CONST) {
                    Sym *saved_kr_params = kr_params;
                    kr_params = sym;
		    ++local_scope;
                    decl(VT_CMP);
		    --local_scope;
                    kr_params = saved_kr_params;
                }
                if ((type.t & (VT_STORAGE_EXTERN|VT_STORAGE_INLINE)) == (VT_STORAGE_EXTERN|VT_STORAGE_INLINE)) {
                    /* `always_inline` functions must be handled as if they
                       don't generate multiple global defs, even if extern
                       inline, i.e. `GNU` inline semantics for those.  `Rewrite`
                       them into static inline.  */
                    if (tcc_state->gnu89_inline || sym->f.func_alwinl)
                        type.t = (type.t & ~VT_STORAGE_EXTERN) | VT_STORAGE_STATIC;
                    else
                        type.t &= ~VT_STORAGE_INLINE; /* always compile otherwise */
                }

            } else if (oldint) {
                tcc_warning("type defaults to int");
            }

            if (gnu_ext && (tok == TOK_ASM1 || tok == TOK_ASM2 || tok == TOK_ASM3)) {
                ad.asm_label = asm_label_instr();
                /* parse one last attribute list, after asm label */
                parse_attribute(&ad);
            #if 0
                /* gcc does not allow `__asm__`("label") with function definition,
                   but why not ... */
                if (tok == '{')
                    expect(";");
            #endif
            }

#ifdef TCC_TARGET_PE
            pe_check_linkage(&type, &ad);
#endif
            if (tok == '{') {
                if (l != VT_CONST)
                    tcc_error("cannot use local functions");
                if ((type.t & VT_BTYPE) != VT_FUNC)
                    expect("function definition");

                /* apply post-declaraton attributes */
                merge_funcattr(&type.ref->f, &ad.f);
                /* put function symbol */
                type.t &= ~VT_STORAGE_EXTERN;
                sym = external_sym(v, &type, 0, &ad);

                /* reject abstract declarators in function definition
                   make old-style float params double */
                for (sa = sym->type.ref; (sa = sa->next) != NULL;) {
                    if (!(sa->v & ~SYM_FIELD))
                        expect("identifier");
                    if (sa->type.t == VT_FLOAT
                        && sym->type.ref->f.func_type == FUNC_OLD) {
                        sa->type.t = VT_DOUBLE;
                    }
                }

                /* static inline functions are just recorded as a kind
                   of macro. `Their` code will be emitted at the end of
                   the compilation unit only if they are used */
                if (sym->type.t & VT_STORAGE_INLINE) {
                    struct InlineFunc *fn;
                    fn = tcc_malloc(sizeof *fn + strlen(file->filename));
                    strcpy(fn->filename, file->filename);
                    fn->sym = sym;
                    dynarray_add(&tcc_state->inline_fns,
				 &tcc_state->nb_inline_fns, fn);
                    skip_or_save_block(&fn->func_str);
                } else {
                    /* compute text section */
                    cur_text_section = text_section;
                    if (ad.section) {
                        cur_text_section = ad.section;
                        if (ad.new_section)
                            ad.section->sh_flags = text_section->sh_flags;
                    }
                    gen_function(sym);
                }
                break;
            } else {
                has_init = 0;
		if (l == VT_CMP) {
		    /* find parameter in function parameter list */
		    for (sym = kr_params->next; sym; sym = sym->next)
			if ((sym->v & ~SYM_FIELD) == v)
			    goto found;
		    tcc_error("declaration for parameter '%s' but no such parameter",
			      get_tok_str(v, NULL));
                found:
		    if (type.t & VT_STORAGE) /* 'register' is okay */
		        tcc_error("storage class specified for '%s'",
				  get_tok_str(v, NULL));
		    if (!(sym->type.t & VT_STORAGE_EXTERN))
		        tcc_error("redefinition of parameter '%s'",
				  get_tok_str(v, NULL));
		    convert_parameter_type(&type);
		    sym->type = type;
		} else if (type.t & VT_STORAGE_TYPEDEF) {
                    declare_typedef(v, &type, &ad);
		} else if ((type.t & VT_BTYPE) == VT_VOID
			   && !(type.t & VT_STORAGE_EXTERN)) {
		    tcc_error("declaration of void object");
                } else {
                    r = 0;
                    if ((type.t & VT_BTYPE) == VT_FUNC) {
                        /* external function definition */
                        /* specific case for `func_call` attribute */
                        merge_funcattr(&type.ref->f, &ad.f);
                    } else if (!(type.t & VT_ARRAY)) {
                        /* not lvalue if array */
                        r |= VT_LVAL;
                    }

                    if (tok == '=') {
                        int t1 = tok;
                        next();
                        if (tok == TOK_VOID) {
                            next();
                            has_init = 0;
                        } else {
                            unget_tok(tok);
                            unget_tok(t1);
                            next();
                            has_init = 1;
                        }
                    } else if (ad.noinit) {
                        has_init = 0;
                    }

                    if (((type.t & VT_STORAGE_EXTERN) && (!has_init || l != VT_CONST))
		        || (type.t & VT_BTYPE) == VT_FUNC
                        /* as with `GCC`, uninitialized global arrays with no size
                           are considered extern: */
                        || ((type.t & VT_ARRAY) && !has_init
                            && l == VT_CONST && type.ref->c < 0)
                        ) {
                        /* external variable or function */
                        type.t |= VT_STORAGE_EXTERN;
                        external_sym(v, &type, r, &ad);
                    } else {
                        if (l == VT_CONST || (type.t & VT_STORAGE_STATIC))
                            r |= VT_CONST;
                        else
                            r |= VT_LOCAL;
                        type.t &= ~VT_STORAGE_EXTERN;
                        if (init_str) {
                            begin_macro(init_str, 1);
                            next();
                        } else if (has_init)
                            next();
                        else if (l == VT_CONST)
                            /* uninitialized global variables may be overridden */
                            type.t |= VT_STORAGE_EXTERN;
                        decl_initializer_alloc(&type, &ad, r, has_init || (init_str != NULL), v, l);
                        if (init_str) {
                            end_macro();
                            unget_tok(end_tok);
                            tok_str_free(init_str);
                        }
                    }

                    if (ad.alias_target && l == VT_CONST) {
                        /* `Aliases` need to be emitted when their target symbol
                           is emitted, even if perhaps unreferenced.
                           `We` only support the case where the base is already
                           defined, otherwise we would need deferring to emit
                           the aliases until the end of the compile unit.  */
                        esym = elfsym(sym_find(ad.alias_target));
                        if (!esym)
                            tcc_error("unsupported forward __alias__ attribute");
                        put_extern_sym2(sym_find(v), esym->st_shndx,
                                        esym->st_value, esym->st_size, 1);
                    }
                }
                if (tok != ',') {
                    if (l == VT_JMP)
                        return has_init ? v : 1;
                    skip(';');
                    break;
                }
                next();
            }
        }
    }
    return 0;
}

/* `Emit` an element-wise 'a[lo..hi] op= x' for a D slice.  '`slice_type`'
   is the destination slice type, 'elem' its (unqualified) element type,
   'dst' the stack offset of the temporary holding the destination
   slice header and 'op' the binary operator of the compound
   assignment.  `The` scalar right operand is on top of the value
   stack.  */
static void gen_slice_op_assign(CType *slice_type, CType *elem, int dst,
                                int op)
{
    CType len_type, ptr_type;
    int size, align, esize, ealign, rhs_addr, idx_addr;
    int cond, exit;

    len_type = slice_type->ref->next->type;
    ptr_type = slice_type->ref->next->next->type;
    esize = type_size(elem, &ealign);

    /* `Evaluate` the right operand exactly once.  */
    loc = (loc - esize) & -ealign;
    rhs_addr = loc;
    vset(elem, VT_LOCAL | VT_LVAL, rhs_addr);
    vswap();
    vstore();
    vpop();

    if (nocode_wanted)
        return;

    size = type_size(&len_type, &align);
    loc = (loc - size) & -align;
    idx_addr = loc;

    /* idx = 0 */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vpushi(0);
    vstore();
    vpop();

    cond = gind();
    /* idx < dst.length */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vset(&len_type, VT_LOCAL | VT_LVAL, dst);
    gen_op(TOK_LT);
    exit = gvtst(1, 0);

    /* dst.ptr[idx] op= rhs */
    vset(&ptr_type, VT_LOCAL | VT_LVAL, dst + PTR_SIZE);
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    gen_op('+');
    indir();
    /* the read-only location, if any, was already diagnosed above */
    vtop->type.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
    vdup();
    vset(elem, VT_LOCAL | VT_LVAL, rhs_addr);
    gen_op(op);
    vstore();
    vpop();

    /* ++idx */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vpushi(1);
    gen_op('+');
    vstore();
    vpop();

    gjmp_addr(cond);
    gsym(exit);
}

/* `Emit` a D slice assignment, with the destination slice value on top of
   the value stack.  '`assign_tok`' is '=' for a bulk copy or fill, or a
   compound assignment token for an element-wise operation.  */
static void gen_slice_assign(int assign_tok)
{
    CType elem, elem_nc, dest_type, slice_type, rhs_elem;
    int esize, ealign, l, r2;

    dest_type = vtop->type;
    if (!is_slice_type(&dest_type))
        tcc_error("left operand of slice assignment must be a D slice");
    elem = *pointed_type(&dest_type.ref->next->next->type);
    esize = type_size(&elem, &ealign);

    if ((dest_type.t | elem.t) & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE))
        tcc_warning("assignment of read-only location");

    /* `The` temporaries used below are writable; don't inherit the
       qualifiers of the destination elements and warn twice.  */
    elem_nc = elem;
    elem_nc.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);

    if (assign_tok == TOK_A_CAT)
        tcc_error("cannot append to a slice range");
    if (assign_tok != '=' && !tcc_state->lang_d)
        tcc_error("only '=' is supported for slice assignment (use -lang=d)");

    l = copy_slice_to_temp(&dest_type);

    next(); /* consume the assignment operator */

    expr_cond();

    if (assign_tok != '=') {
        gen_slice_op_assign(&dest_type, &elem_nc, l, TOK_ASSIGN_OP(assign_tok));
    } else {
        /* `An` array right operand is bulk-copied, unless it is the
           element type itself.  */
        if (!is_slice_type(&vtop->type)
            && (vtop->type.t & (VT_ARRAY | VT_VLA))) {
            if (!is_compatible_unqualified_types(&vtop->type.ref->type, &elem)
                && !(vtop->type.ref->type.t & VT_QUALIFIER_IMMUTABLE)) {
                cast_error(&vtop->type, &dest_type);
            }
            rhs_elem = vtop->type.ref->type;
            slice_type.t = VT_STRUCT;
            slice_type.ref = make_slice_type(&rhs_elem);
            convert_array_to_slice(&slice_type);
        }

        if (is_slice_type(&vtop->type)) {
            CType *re = pointed_type(&vtop->type.ref->next->next->type);
            int r_slice;

            if (!is_compatible_unqualified_types(re, &elem))
                cast_error(&vtop->type, &dest_type);
            r_slice = copy_slice_to_temp(&vtop->type);

            vpush_helper_func(TOK___darray_copy);
            vset(&char_pointer_type, VT_LOCAL, l);
            vset(&char_pointer_type, VT_LOCAL, r_slice);
            vpushs(esize);
            vpushi(tcc_state->slice_len_mismatch);
            gfunc_call(4);
        } else {
            int elem_addr;

            elem_addr = get_temp_local_var(esize, ealign, &r2);
            vset(&elem_nc, VT_LOCAL | VT_LVAL, elem_addr);
            vswap();
            vstore();
            vpop();

            vpush_helper_func(TOK___darray_fill);
            vset(&char_pointer_type, VT_LOCAL, l);
            vset(&elem_nc, VT_LOCAL | VT_LVAL, elem_addr);
            gaddrof();
            vtop->type = char_pointer_type;
            vpushs(esize);
            gfunc_call(3);
        }
    }
    vset(&dest_type, VT_LOCAL | VT_LVAL, l);
}

/* ------------------------------------------------------------------------- */
#undef gjmp_addr
#undef gjmp
/* ------------------------------------------------------------------------- */
