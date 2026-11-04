#ifndef TCC_ELF_H
#define TCC_ELF_H

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
#endif /* TCC_ELF_H */
