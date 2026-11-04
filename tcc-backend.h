#ifndef TCC_BACKEND_H
#define TCC_BACKEND_H

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
#endif /* TCC_BACKEND_H */
