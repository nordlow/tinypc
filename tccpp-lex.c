/* return the current character, handling end of block if necessary
   (but not stray) */
/**
 * @brief Handle end-of-buffer detection, refilling or erroring the input.
 */
static int handle_eob(void)
{
    BufferedFile *bf = file;
    int len;

    /* only tries to read if really end of buffer */
    if (bf->buf_ptr >= bf->buf_end) {
        if (bf->fd >= 0) {
#if defined(PARSE_DEBUG)
            len = 1;
#else
            len = IO_BUF_SIZE;
#endif
            len = read(bf->fd, bf->buffer, len);
            if (len < 0)
                len = 0;
        } else {
            len = 0;
        }
        total_bytes += len;
        bf->buf_ptr = bf->buffer;
        bf->buf_end = bf->buffer + len;
        *bf->buf_end = CH_EOB;
    }
    if (bf->buf_ptr < bf->buf_end) {
        return bf->buf_ptr[0];
    } else {
        bf->buf_ptr = bf->buf_end;
        return CH_EOF;
    }
}

/* read next char from current input file and handle end of input buffer */
/**
 * @brief Read the next character from the input buffer.
 */
static int next_c(void)
{
    int ch = *++file->buf_ptr;
    /* end of buffer/file handling */
    if (ch == CH_EOB && file->buf_ptr >= file->buf_end)
        ch = handle_eob();
    return ch;
}

/* input with '\[\r]\n' handling. */
static int handle_stray_noerror(int err)
{
    int ch;
    while ((ch = next_c()) == '\\') {
        ch = next_c();
        if (ch == '\n') {
    newl:
            file->line_num++;
        } else {
            if (ch == '\r') {
                ch = next_c();
                if (ch == '\n')
                    goto newl;
                *--file->buf_ptr = '\r';
            }
            if (err)
                tcc_error("stray '\\' in program");
            /* may take advantage of '`BufferedFile`.unget[4}' */
            return *--file->buf_ptr = '\\';
        }
    }
    return ch;
}

#define ninp() handle_stray_noerror(0)

/* handle '\\' in strings, comments and skipped regions */
static int handle_bs(uint8_t **p)
{
    int c;
    file->buf_ptr = *p - 1;
    c = ninp();
    *p = file->buf_ptr;
    return c;
}

/* skip the stray and handle the \\n case. `Output` an error if
   incorrect char after the stray */
static int handle_stray(uint8_t **p)
{
    int c;
    file->buf_ptr = *p - 1;
    c = handle_stray_noerror(!(parse_flags & PARSE_FLAG_ACCEPT_STRAYS));
    *p = file->buf_ptr;
    return c;
}

/* handle the complicated stray case */
#define PEEKC(c, p)\
{\
    c = *++p;\
    if (c == '\\')\
        c = handle_stray(&p); \
}

/**
 * @brief Skip whitespace characters in the input.
 */
static int skip_spaces(void)
{
    int ch;
    --file->buf_ptr;
    do {
        ch = ninp();
    } while (isidnum_table[ch - CH_EOF] & IS_SPC);
    return ch;
}

/* single line C++ comments */
/**
 * @brief Scan a line comment up to its end.
 */
static uint8_t *parse_line_comment(uint8_t *p)
{
    size_t rem;
    uint8_t *p_nl, *p_bs, *stop;
    int c, ch;
    StringSlice slice;
    for(;;) {
        rem = file->buf_end - (p + 1);
        if (rem == 0) {
            /* the comment starts at the end of the buffer: refill it */
            file->buf_ptr = p + 1;
            ch = handle_eob();
            if (ch == CH_EOF)
                break;
            p = file->buf_ptr;
            if (ch == '\n')
                break;
            if (ch == '\\') {
                ch = handle_bs(&p);
                if (ch == CH_EOF)
                    break;
            }
            continue;
        }
        slice.ptr = (const char *)(p + 1);
        slice.len = rem;
        p_nl = (uint8_t *)slice_memchr(slice, '\n');
        p_bs = (uint8_t *)slice_memchr(slice, '\\');
        stop = p + 1 + rem;
        if (p_nl && p_nl < stop) stop = p_nl;
        if (p_bs && p_bs < stop) stop = p_bs;

        p = stop - 1;
        c = *++p;
        if (c == '\n')
            break;
        if (c == '\\') {
            ch = handle_bs(&p);
            if (ch == CH_EOF)
                break;
        }
    }
    return p;
}

/* C comments */
/**
 * @brief Scan a C comment (line or block) up to its end.
 */
static uint8_t *parse_comment(uint8_t *p)
{
    size_t rem;
    uint8_t *p_nl, *p_star, *p_bs, *stop;
    int c, ch;
    StringSlice slice;
    for(;;) {
        rem = file->buf_end - (p + 1);
        if (rem == 0) {
            /* the comment starts at the end of the buffer: refill it */
            file->buf_ptr = p + 1;
            ch = handle_eob();
            if (ch == CH_EOF)
                tcc_error("unexpected end of file in comment");
            p = file->buf_ptr;
            if (ch == '\n') {
                file->line_num++;
                p++;
                continue;
            }
            if (ch == '\\') {
                ch = handle_bs(&p);
                if (ch == CH_EOF)
                    tcc_error("unexpected end of file in comment");
                if (ch == '\n')
                    file->line_num++;
            }
            continue;
        }
        slice.ptr = (const char *)(p + 1);
        slice.len = rem;
        p_nl = (uint8_t *)slice_memchr(slice, '\n');
        p_star = (uint8_t *)slice_memchr(slice, '*');
        p_bs = (uint8_t *)slice_memchr(slice, '\\');
        stop = p + 1 + rem;
        if (p_nl && p_nl < stop) stop = p_nl;
        if (p_star && p_star < stop) stop = p_star;
        if (p_bs && p_bs < stop) stop = p_bs;

        p = stop - 1;
        c = *++p;
    redo:
        if (c == '\n') {
            file->line_num++;
        } else if (c == '*') {
            do {
                c = *++p;
            } while (c == '*');
            if (c == '\\')
                c = handle_bs(&p);
            if (c == '/')
                break;
            goto check_eof;
        } else {
            c = handle_bs(&p);
        check_eof:
            if (c == CH_EOF)
                tcc_error("unexpected end of file in comment");
            if (c != '\\') {
                if (c == '\n' || c == '*' || c == '\\')
                    goto redo;
            }
        }
    }
    return p + 1;
}

/* parse a string without interpreting escapes */
/**
 * @brief Scan a preprocessor string literal, decoding escapes into `str`.
 */
static uint8_t *parse_pp_string(uint8_t *p, int sep, CString *str)
{
    int c;
    size_t rem;
    uint8_t *p_sep, *p_bs, *p_nl, *next_special;
    size_t len;
    StringSlice slice;
    for(;;) {
        rem = file->buf_end - (p + 1);
        if (rem > 0) {
            slice.ptr = (const char *)(p + 1);
            slice.len = rem;
            p_sep = (uint8_t *)slice_memchr(slice, sep);
            p_bs  = (uint8_t *)slice_memchr(slice, '\\');
            p_nl  = (uint8_t *)slice_memchr(slice, '\n');
            next_special = p + 1 + rem;
            if (p_sep && p_sep < next_special) next_special = p_sep;
            if (p_bs  && p_bs  < next_special) next_special = p_bs;
            if (p_nl  && p_nl  < next_special) next_special = p_nl;

            if (next_special > p + 1) {
                len = next_special - (p + 1);
                if (str)
                    cstr_cat(str, (char *)(p + 1), len);
                p = next_special - 1;
            }
        }
        c = *++p;
    redo:
        if (c == sep) {
            break;
        } else if (c == '\\') {
            c = handle_bs(&p);
            if (c == CH_EOF) {
        unterminated_string:
                /* `XXX`: indicate line number of start of string */
                tok_flags &= ~TOK_FLAG_BOL;
                tcc_error("missing terminating %c character", sep);
            } else if (c == '\\') {
                if (str)
                    cstr_ccat(str, c);
                c = *++p;
                /* add char after '\\' unconditionally */
                if (c == '\\') {
                    c = handle_bs(&p);
                    if (c == CH_EOF)
                        goto unterminated_string;
                }
                goto add_char;
            } else {
                goto redo;
            }
        } else if (c == '\n') {
        add_lf:
            if (ACCEPT_LF_IN_STRINGS) {
                file->line_num++;
                goto add_char;
            } else if (str) { /* not skipping */
                goto unterminated_string;
            } else {
                //tcc_warning("missing terminating %c character", sep);
                return p;
            }
        } else if (c == '\r') {
            c = *++p;
            if (c == '\\')
                c = handle_bs(&p);
            if (c == '\n')
                goto add_lf;
            if (c == CH_EOF)
                goto unterminated_string;
            if (str)
                cstr_ccat(str, '\r');
            goto redo;
        } else {
        add_char:
            if (str)
                cstr_ccat(str, c);
        }
    }
    p++;
    return p;
}

/* skip block of text until #else, #elif or #endif. skip also pairs of
   #if/#endif */
/**
 * @brief Skip preprocessing tokens until the end of the current conditional block.
 */
static void preprocess_skip(void)
{
    int a, start_of_line, c, in_warn_or_error;
    uint8_t *p;

    p = file->buf_ptr;
    a = 0;
redo_start:
    start_of_line = 1;
    in_warn_or_error = 0;
    for(;;) {
        c = *p;
        switch(c) {
        case ' ':
        case '\t':
        case '\f':
        case '\v':
        case '\r':
            p++;
            continue;
        case '\n':
            file->line_num++;
            p++;
            goto redo_start;
        case '\\':
            c = handle_bs(&p);
            if (c == CH_EOF)
                expect("#endif");
            if (c == '\\')
                ++p;
            continue;
        /* skip strings */
        case '\"':
        case '\'':
            if (in_warn_or_error)
                goto _default;
            tok_flags &= ~TOK_FLAG_BOL;
            p = parse_pp_string(p, c, NULL);
            break;
        /* skip comments */
        case '/':
            if (in_warn_or_error)
                goto _default;
            ++p;
            c = handle_bs(&p);
            if (c == '*') {
                p = parse_comment(p);
            } else if (c == '/') {
                p = parse_line_comment(p);
            }
            continue;
        case '#':
            p++;
            if (start_of_line) {
                file->buf_ptr = p;
                next_nomacro();
                p = file->buf_ptr;
                if (a == 0 && 
                    (tok == TOK_ELSE || tok == TOK_ELIF || tok == TOK_ENDIF))
                    goto the_end;
                if (tok == TOK_IF || tok == TOK_IFDEF || tok == TOK_IFNDEF)
                    a++;
                else if (tok == TOK_ENDIF)
                    a--;
                else if( tok == TOK_ERROR || tok == TOK_WARNING)
                    in_warn_or_error = 1;
                else if (tok == TOK_LINEFEED)
                    goto redo_start;
                else if (parse_flags & PARSE_FLAG_ASM_FILE)
                    p = parse_line_comment(p - 1);
            }
#if !defined(TCC_TARGET_ARM) && !defined(TCC_TARGET_ARM64)
            else if (parse_flags & PARSE_FLAG_ASM_FILE)
                p = parse_line_comment(p - 1);
#else
            /* `ARM`/`ARM64` assembly uses '#' for constants */
#endif
            break;
_default:
        default:
            p++;
            break;
        }
        start_of_line = 0;
    }
 the_end: ;
    file->buf_ptr = p;
}

#if 0
/* return the number of additional 'ints' necessary to store the
   token */
static inline int tok_size(const int *p)
{
    switch(*p) {
        /* 4 bytes */
    case TOK_CINT:
    case TOK_CUINT:
    case TOK_CCHAR:
    case TOK_LCHAR:
    case TOK_CFLOAT:
    case TOK_LINENUM:
        return 1 + 1;
    case TOK_STR:
    case TOK_LSTR:
    case TOK_PPNUM:
    case TOK_PPSTR:
        return 1 + 1 + (p[1] + 3) / 4;
    case TOK_CLONG:
    case TOK_CULONG:
	return 1 + LONG_SIZE / 4;
    case TOK_CDOUBLE:
    case TOK_CLLONG:
    case TOK_CULLONG:
        return 1 + 2;
    case TOK_CLDOUBLE:
        return 1 + LDOUBLE_WORDS;
    default:
        return 1 + 0;
    }
}
#endif

/* token string handling */
ST_INLN void tok_str_new(TokenString *s)
{
    s->str = NULL;
    s->len = s->need_spc = 0;
    s->allocated_len = 0;
    s->last_line_num = -1;
}

ST_FUNC TokenString *tok_str_alloc(void)
{
    TokenString *str = tal_realloc(&tokstr_alloc, 0, sizeof *str);
    tok_str_new(str);
    return str;
}

ST_FUNC void tok_str_free_str(int *str)
{
    tal_free(&tokstr_alloc, str);
}

ST_FUNC void tok_str_free(TokenString *str)
{
    tok_str_free_str(str->str);
    tal_free(&tokstr_alloc, str);
}

ST_FUNC int *tok_str_realloc(TokenString *s, int new_size)
{
    int *str, size;

    size = s->allocated_len;
    if (size < 16)
        size = 16;
    while (size < new_size)
        size = size * 2;
    if (size > s->allocated_len) {
        str = tal_realloc(&tokstr_alloc, s->str, size * sizeof(int));
        s->allocated_len = size;
        s->str = str;
    }
    return s->str;
}

ST_INLN void tok_str_add(TokenString *s, int t)
{
    int len, *str;

    len = s->len;
    str = s->str;
    if (len >= s->allocated_len)
        str = tok_str_realloc(s, len + 1);
    str[len++] = t;
    s->len = len;
}

ST_FUNC void begin_macro(TokenString *str, int alloc)
{
    str->alloc = alloc;
    str->prev = macro_stack;
    str->prev_ptr = macro_ptr;
    str->save_line_num = file->line_num;
    macro_ptr = str->str;
    macro_stack = str;
}

ST_FUNC TokenString *macro_stack_top(void)
{
    return macro_stack;
}

ST_FUNC void end_macro(void)
{
    TokenString *str = macro_stack;
    macro_stack = str->prev;
    macro_ptr = str->prev_ptr;
    file->line_num = str->save_line_num;
    if (str->alloc == 0) {
        /* matters if str not alloced, may be `tokstr_buf` */
        str->len = str->need_spc = 0;
    } else {
        if (str->alloc == 2)
            str->str = NULL; /* don't free */
        tok_str_free(str);
    }
}

ST_FUNC void tok_str_add2(TokenString *s, int t, CValue *cv)
{
    int len, *str;

    len = s->len;
    str = s->str;

    /* allocate space for worst case */
    if (len + TOK_MAX_SIZE >= s->allocated_len)
        str = tok_str_realloc(s, len + TOK_MAX_SIZE + 1);
    str[len++] = t;
    switch(t) {
    case TOK_CINT:
    case TOK_CUINT:
    case TOK_CCHAR:
    case TOK_LCHAR:
    case TOK_CFLOAT:
    case TOK_LINENUM:
#if LONG_SIZE == 4
    case TOK_CLONG:
    case TOK_CULONG:
#endif
        str[len++] = cv->tab[0];
        break;
    case TOK_PPNUM:
    case TOK_PPSTR:
    case TOK_STR:
    case TOK_LSTR:
        {
            /* `Insert` the string into the int array. */
            size_t nb_words =
                1 + (cv->str.size + sizeof(int) - 1) / sizeof(int);
            if (len + nb_words >= s->allocated_len)
                str = tok_str_realloc(s, len + nb_words + 1);
            str[len] = cv->str.size;
            memcpy(&str[len + 1], cv->str.data, cv->str.size);
            len += nb_words;
        }
        break;
    case TOK_CDOUBLE:
    case TOK_CLLONG:
    case TOK_CULLONG:
#if LONG_SIZE == 8
    case TOK_CLONG:
    case TOK_CULONG:
#endif
        str[len++] = cv->tab[0];
        str[len++] = cv->tab[1];
        break;
    case TOK_CLDOUBLE:
        str[len++] = cv->tab[0];
        str[len++] = cv->tab[1];
        if (LDOUBLE_WORDS >= 3)
        str[len++] = cv->tab[2];
        if (LDOUBLE_WORDS >= 4)
        str[len++] = cv->tab[3];
    default:
        break;
    }
    s->len = len;
}

/* add the current parse token in token string 's' */
ST_FUNC void tok_str_add_tok(TokenString *s)
{
    CValue cval;

    /* save line number info */
    if (file->line_num != s->last_line_num) {
        s->last_line_num = file->line_num;
        cval.i = s->last_line_num;
        tok_str_add2(s, TOK_LINENUM, &cval);
    }
    tok_str_add2(s, tok, &tokc);
}

/* like `tok_str_add2`(), add a space if needed */
static void tok_str_add2_spc(TokenString *s, int t, CValue *cv)
{
    if (s->need_spc == 3)
        tok_str_add(s, ' ');
    s->need_spc = 2;
    tok_str_add2(s, t, cv);
}

/* get a token from an integer array and increment pointer. */
static inline void tok_get(int *t, const int **pp, CValue *cv)
{
    const int *p = *pp;
    int n, *tab;

    tab = cv->tab;
    switch(*t = *p++) {
#if LONG_SIZE == 4
    case TOK_CLONG:
#endif
    case TOK_CINT:
    case TOK_CCHAR:
    case TOK_LCHAR:
    case TOK_LINENUM:
        cv->i = *p++;
        break;
#if LONG_SIZE == 4
    case TOK_CULONG:
#endif
    case TOK_CUINT:
        cv->i = (unsigned)*p++;
        break;
    case TOK_CFLOAT:
	tab[0] = *p++;
	break;
    case TOK_STR:
    case TOK_LSTR:
    case TOK_PPNUM:
    case TOK_PPSTR:
        cv->str.size = *p++;
        cv->str.data = (char*)p;
        p += (cv->str.size + sizeof(int) - 1) / sizeof(int);
        break;
    case TOK_CDOUBLE:
    case TOK_CLLONG:
    case TOK_CULLONG:
#if LONG_SIZE == 8
    case TOK_CLONG:
    case TOK_CULONG:
#endif
        n = 2;
        goto copy;
    case TOK_CLDOUBLE:
        n = LDOUBLE_WORDS;
    copy:
        do
            *tab++ = *p++;
        while (--n);
        break;
    default:
        break;
    }
    *pp = p;
}

#if 0
# define TOK_GET(t,p,c) tok_get(t,p,c)
#else
# define TOK_GET(t,p,c) do { \
    int _t = **(p); \
    if (TOK_HAS_VALUE(_t)) \
        tok_get(t, p, c); \
    else \
        *(t) = _t, ++*(p); \
    } while (0)
#endif

/**
 * @brief Check whether two macro token streams are equal.
 */
static int macro_is_equal(const int *a, const int *b)
{
    CValue cv;
    int t;

    if (!a || !b)
        return 1;

    while (*a && *b) {
        cstr_reset(&tokcstr);
        TOK_GET(&t, &a, &cv);
        cstr_cat(&tokcstr, get_tok_str(t, &cv), 0);
        TOK_GET(&t, &b, &cv);
        if (strcmp(tokcstr.data, get_tok_str(t, &cv)))
            return 0;
    }
    return !(*a || *b);
}
