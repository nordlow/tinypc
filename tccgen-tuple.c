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

/* `Return` whether slice indexing must be checked against `.length`.
   An explicit `-f[no-]slice-bounds-check` wins; otherwise the check is
   on in D mode and follows `-b` in C mode.  */
static int slice_bounds_checking(void)
{
    if (tcc_state->slice_bounds_check != 255)
        return tcc_state->slice_bounds_check;
    if (tcc_state->lang_d)
        return 1;
#ifdef CONFIG_TCC_BCHECK
    return tcc_state->do_bounds_check;
#else
    return 0;
#endif
}

/* `Emit` a call to 'helper' when the comparison 'op' of the two size_t
   temporaries 'a_loc' and 'b_loc' is false.  `The` failure path never
   returns, and the value stack is otherwise left unchanged.  */
static void gen_slice_cmp_check(int a_loc, int b_loc, int op,
                                const char *helper, int is_index,
                                int slice_tmp, int lo_loc, int hi_loc,
                                int inclusive, const char *file, int line)
{
    CType size_type;
    int fail, cont;

    size_type.t = VT_SIZE_T;
    size_type.ref = NULL;

    /* values below the check (e.g. earlier call arguments) stay live
       across the failure branch; spill any register-resident ones now,
       before the runtime call in that branch can clobber them */
    save_regs(0);

    vset(&size_type, VT_LOCAL | VT_LVAL, a_loc);
    vset(&size_type, VT_LOCAL | VT_LVAL, b_loc);
    gen_op(op);
    fail = gvtst(1, 0);
    cont = gjmp(0);
    gsym(fail);

    vpush_helper_func(tok_alloc_const(helper));
    vpush_string(file);
    vpushi(line);
    if (is_index) {
        /* index, length */
        vset(&size_type, VT_LOCAL | VT_LVAL, a_loc);
        vset(&size_type, VT_LOCAL | VT_LVAL, slice_tmp);
        gfunc_call(4);
    } else {
        /* lo, hi, length, inclusive */
        vset(&size_type, VT_LOCAL | VT_LVAL, lo_loc);
        vset(&size_type, VT_LOCAL | VT_LVAL, hi_loc);
        vset(&size_type, VT_LOCAL | VT_LVAL, slice_tmp);
        vpushi(inclusive);
        gfunc_call(6);
    }
    gsym(cont);
}

/* `Capture` the tokens of a subscript index, stopping at the closing
   ']' or at a top-level range operator.  `The` index is replayed with
   'begin_macro()' so that the normal expression parser still generates
   the code; the captured form is only inspected to omit a redundant
   bounds comparison.  */
static void scan_subscript_index(TokenString *ts)
{
    int depth = 0;

    for (;;) {
        int t = tok;
        if (t == TOK_EOF)
            break;
        if (depth == 0 && (t == ']' || t == TOK_TWODOTS || t == TOK_DOTS)) {
            /* keep the delimiter in the replay: the token was already
               fetched from the outer stream and would otherwise be lost */
            tok_str_add_tok(ts);
            break;
        }
        tok_str_add_tok(ts);
        if (t == '(' || t == '[' || t == '{')
            ++depth;
        else if (t == ')' || t == ']' || t == '}')
            --depth;
        next();
    }
    tok_str_add(ts, 0);
}

/* `A` cursor over a captured token string.  `Line` markers are skipped;
   payload words of accepted tokens are consumed by the caller.  */
typedef struct {
    int *p, *end;
} SubscriptCursor;

static int subscript_peek(SubscriptCursor *c)
{
    while (c->p < c->end && *c->p == TOK_LINENUM)
        c->p += 2;
    return c->p < c->end ? *c->p : 0;
}

/* `A` value of the form `$ * P / Q`, represented as the rational
   multiplier P/Q applied to the slice length.  `Constant` values are
   tracked separately so that `$ * 0` and friends are rejected.  */
typedef struct {
    unsigned long long p, q;
    unsigned long long constant;
    int is_constant;
} SubscriptFactor;

static int parse_subscript_factor(SubscriptCursor *c, SubscriptFactor *f);

static int parse_subscript_atom(SubscriptCursor *c, SubscriptFactor *f)
{
    int t = subscript_peek(c);

    if (t == '(') {
        ++c->p;
        f->p = 1;
        f->q = 1;
        f->constant = 0;
        f->is_constant = 0;
        if (!parse_subscript_factor(c, f))
            return 0;
        if (subscript_peek(c) != ')')
            return 0;
        ++c->p;
        return 1;
    }
    if (t >= TOK_UIDENT
        && !strcmp(get_tok_str(t, NULL), "$")) {
        ++c->p;
        f->p = 1;
        f->q = 1;
        f->constant = 0;
        f->is_constant = 0;
        return 1;
    }
    if (t == TOK_CINT || t == TOK_CUINT) {
        if (c->p + 1 >= c->end)
            return 0;
        f->p = 0;
        f->q = 1;
        f->constant = (unsigned)c->p[1];
        f->is_constant = 1;
        c->p += 2;
        return 1;
    }
    return 0;
}

static int parse_subscript_factor(SubscriptCursor *c, SubscriptFactor *f)
{
    SubscriptFactor rhs;

    if (!parse_subscript_atom(c, f))
        return 0;
    for (;;) {
        int op = subscript_peek(c);
        if (op != '*' && op != '/')
            return 1;
        ++c->p;
        if (!parse_subscript_atom(c, &rhs))
            return 0;
        if (op == '*') {
            if (f->is_constant && rhs.is_constant) {
                if (rhs.constant && f->constant > ~0ULL / rhs.constant)
                    return 0;
                f->constant *= rhs.constant;
            } else if (!f->is_constant && !rhs.is_constant) {
                return 0;
            } else if (rhs.is_constant) {
                if (!rhs.constant)
                    return 0;
                if (f->p > ~0ULL / rhs.constant)
                    return 0;
                f->p *= rhs.constant;
            } else {
                if (!f->constant)
                    return 0;
                if (rhs.p > ~0ULL / f->constant)
                    return 0;
                rhs.p *= f->constant;
                *f = rhs;
            }
        } else {
            if (rhs.is_constant) {
                if (!rhs.constant)
                    return 0;
                if (f->is_constant)
                    f->constant /= rhs.constant;
                else if (f->q > ~0ULL / rhs.constant)
                    return 0;
                else
                    f->q *= rhs.constant;
            } else {
                return 0;
            }
        }
    }
}

/* `Return` the divisor k when the captured subscript is exactly
   `$/k` (or `$/(k)`), otherwise zero.  `The` comparison `index <
   length` is then implied by `length != 0`, which is still checked.  */
static int subscript_safe_divisor(TokenString *ts)
{
    SubscriptCursor c;
    SubscriptFactor f;

    c.p = ts->str;
    c.end = ts->str + ts->len;
    int end;

    if (!parse_subscript_factor(&c, &f))
        return 0;
    end = subscript_peek(&c);
    if (end != 0 && end != ']')
        return 0;
    if (f.is_constant || f.p != 1 || f.q < 2)
        return 0;
    return f.q < 0x7fffffffU ? (int)f.q : 0;
}

/* `Check` that the slice at 'slice_tmp' is non-empty.  `This` is the
   whole bounds check for an index of the form `$/k`: such an index is
   below the length for every non-empty slice.  `The` stack layout
   `[ptr, index]` is left unchanged.  */
static void gen_slice_nonempty_check(int slice_tmp, const char *file,
                                     int line)
{
    CType size_type;
    int fail, cont;

    size_type.t = VT_SIZE_T;
    size_type.ref = NULL;

    save_regs(0);
    vset(&size_type, VT_LOCAL | VT_LVAL, slice_tmp);
    vpushi(0);
    gen_op(TOK_GT);
    fail = gvtst(1, 0);
    cont = gjmp(0);
    gsym(fail);

    vpush_helper_func(tok_alloc_const("__slice_index_bounds_error"));
    vpush_string(file);
    vpushi(line);
    vpushi(0);                  /* index is zero exactly when length is */
    vset(&size_type, VT_LOCAL | VT_LVAL, slice_tmp);
    gfunc_call(4);
    gsym(cont);
}

/* `Check` the index on top of the stack against the length of the
   slice stored at local offset 'slice_tmp'.  `The` stack layout
   `[ptr, index]` is preserved, with the index converted to size_t.  */
static void gen_slice_index_check(int slice_tmp, const char *file, int line)
{
    CType size_type, ptr_type;
    int size, align, idx_loc, ptr_loc;

    size_type.t = VT_SIZE_T;
    size_type.ref = NULL;
    ptr_type = vtop[-1].type;

    /* save the index as an unsigned size_t temporary; this also turns
       a negative index into a large unsigned value */
    size = type_size(&size_type, &align);
    loc = (loc - size) & -align;
    idx_loc = loc;
    vset(&size_type, VT_LOCAL | VT_LVAL, idx_loc);
    vswap();
    vstore();
    vtop--;

    /* also spill the pointer: it stays live across the failure branch
       and must not be kept in a register that the runtime call clobbers */
    size = type_size(&ptr_type, &align);
    loc = (loc - size) & -align;
    ptr_loc = loc;
    vset(&ptr_type, VT_LOCAL | VT_LVAL, ptr_loc);
    vswap();
    vstore();
    vtop--;

    gen_slice_cmp_check(idx_loc, slice_tmp, TOK_LT,
                        "__slice_index_bounds_error", 1, slice_tmp,
                        0, 0, 0, file, line);

    /* restore the pointer and the index */
    vset(&ptr_type, VT_LOCAL | VT_LVAL, ptr_loc);
    vset(&size_type, VT_LOCAL | VT_LVAL, idx_loc);
}

/* `Check` the slice range `[lo..hi]` on the value stack against the
   length of the slice stored at local offset 'slice_tmp'.  `The` stack
   `[ptr, lo, hi]` is preserved, with both bounds converted to size_t. */
static void gen_slice_range_check(int slice_tmp, int inclusive,
                                  const char *file, int line)
{
    CType size_type, ptr_type;
    int size, align, lo_loc, hi_loc, ptr_loc;

    size_type.t = VT_SIZE_T;
    size_type.ref = NULL;
    ptr_type = vtop[-2].type;
    size = type_size(&size_type, &align);

    /* save hi, then lo, as unsigned size_t temporaries */
    loc = (loc - size) & -align;
    hi_loc = loc;
    vset(&size_type, VT_LOCAL | VT_LVAL, hi_loc);
    vswap();
    vstore();
    vtop--;

    loc = (loc - size) & -align;
    lo_loc = loc;
    vset(&size_type, VT_LOCAL | VT_LVAL, lo_loc);
    vswap();
    vstore();
    vtop--;

    /* spill the base pointer across the failure branches */
    size = type_size(&ptr_type, &align);
    loc = (loc - size) & -align;
    ptr_loc = loc;
    vset(&ptr_type, VT_LOCAL | VT_LVAL, ptr_loc);
    vswap();
    vstore();
    vtop--;

    /* hi < length (inclusive) or hi <= length (half-open) */
    gen_slice_cmp_check(hi_loc, slice_tmp, inclusive ? TOK_LT : TOK_LE,
                        "__slice_range_bounds_error", 0, slice_tmp,
                        lo_loc, hi_loc, inclusive, file, line);
    /* lo <= hi */
    gen_slice_cmp_check(lo_loc, hi_loc, TOK_LE,
                        "__slice_range_bounds_error", 0, slice_tmp,
                        lo_loc, hi_loc, inclusive, file, line);

    /* restore the base pointer and the bounds as size_t values */
    vset(&ptr_type, VT_LOCAL | VT_LVAL, ptr_loc);
    vset(&size_type, VT_LOCAL | VT_LVAL, lo_loc);
    vset(&size_type, VT_LOCAL | VT_LVAL, hi_loc);
}

/* `Emit` the pointer addition of a slice access whose length has
   already been checked, skipping the allocation-based bound check.  */
static void gen_slice_ptr_add(void)
{
#ifdef CONFIG_TCC_BCHECK
    int save = no_ptr_bounds_check;
    no_ptr_bounds_check = 1;
#endif
    gen_op('+');
#ifdef CONFIG_TCC_BCHECK
    no_ptr_bounds_check = save;
#endif
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

	if (tok == ')') {
	  if (tcc_state->lang_d) {
	    pt.t = VT_VOID;
	    pt.ref = NULL;
	    l = FUNC_NEW;
	  } else {
	    l = 0;
	  }
	}
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
        if (vtop->r & VT_LVAL)
            gv(RC_TYPE(vtop->type.t));
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
        ++n;
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
        tuple_add_field(&plast, &vstack[base + i].type, i);
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

#include "tccgen-expr.c"
