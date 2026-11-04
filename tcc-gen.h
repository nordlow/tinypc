#ifndef TCC_GEN_H
#define TCC_GEN_H

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
    CType vt; /**< `Function` return type descriptor */
    const char *name; /**< `Function` name for diagnostics and `__func__` */
    int vc; /**< `Stack` address of implicit struct return */
    int ind; /**< `Function` start code address */
    FuncSafety safety; /**< `Function` memory safety level */
    FuncPurity pure; /**< `Function` purity level */
    unsigned char variadic; /**< `Non`-zero if function is variadic */
    unsigned char old_style; /**< `Non`-zero if K&R old-style parameter declaration */
} FuncState;

/**
 * @brief `Lexical` scope descriptor for symbol resolution and block-level attributes.
 */
typedef struct Scope {
    struct Scope *prev; /**< `Enclosing` parent scope */
    struct {
        int loc; /**< `Variable`-length array location offset */
        int locorig; /**< `Original` variable-length array location offset */
        int num; /**< `Number` of active variable-length arrays */
    } vla; /**< `Variable`-length array tracking */
    struct { Sym *s; int n; } cl; /**< `Cleanup` functions */
    int *bsym, *csym; /**< `Break` and continue jump target symbol stacks */
    Sym *lstk, *llstk; /**< `Local` symbol stack */
    FuncState fn; /**< `Function` state for this scope */
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
#endif /* TCC_GEN_H */
