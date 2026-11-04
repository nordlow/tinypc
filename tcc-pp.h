#ifndef TCC_PP_H
#define TCC_PP_H

/* ------------ tccpp.c ------------ */

ST_DATA struct BufferedFile *file;
ST_DATA int tok;
ST_DATA CValue tokc;
ST_DATA const int *macro_ptr;
ST_DATA int parse_flags;
ST_DATA int tok_flags;
ST_DATA CString tokcstr; /* current parsed string, if any */
ST_DATA int tok_is_imaginary;

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
#endif /* TCC_PP_H */
