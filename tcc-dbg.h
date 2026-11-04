#ifndef TCC_DBG_H
#define TCC_DBG_H

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
#endif /* TCC_DBG_H */
