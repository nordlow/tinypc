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
static void gen_op_complex(int op);
static void gen_complex_part(int is_im);
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
static int slice_bounds_checking(void);
static void gen_slice_index_check(int slice_tmp, const char *file, int line);
static void gen_slice_range_check(int slice_tmp, int inclusive,
                                  const char *file, int line);
static void vpush_string(const char *str);
static void d_array_suffix(CType *type, int storage);
static void end_switch(void);
static int check_d_static_assert(void);
static void do_Static_assert(void);
static void do_d_assert(void);
static int check_d_manifest_constant(int l);
static int check_d_conditional(int l, int flags);
static int check_d_pragma(int l);
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
#ifdef CONFIG_TCC_BCHECK
/* set while the slice access code performs its own length check, so
   that the generic allocation-based pointer check is skipped */
static TCC_TLS int no_ptr_bounds_check;
#endif
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
    if (t & VT_COMPLEX) {
#ifdef REG_FRE2
        return REG_FRE2;
#else
        return VT_CONST;
#endif
    }
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

/* Returns true for two-word types based on R2_RET(t). */
#define USING_TWO_WORDS(t) ((t & VT_COMPLEX) || R2_RET(t) != VT_CONST)
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

#include "tccgen-gen.c"
