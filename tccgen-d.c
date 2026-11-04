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
    TokenString *names = tok_str_alloc();
    int n, i;
    CType type;
    Sym *s;

    skip('(');
    for (;;) {
        if (tok < TOK_UIDENT)
            expect("identifier");
        tok_str_add(names, tok);
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
    if (n != names->len) {
        tcc_error("manifest constant destructuring pattern has %d names "
                  "but the initializer has %d elements", names->len, n);
    }
    for (i = 0; i < n; ++i) {
        type = tc->types[i];
        type.t = (type.t & ~VT_STORAGE) | VT_STORAGE_STATIC | VT_ENUM_VAL;
        type.ref = push_enum_ref();
        s = sym_push(names->str[i], &type, VT_CONST, 0);
        s->enum_val = tc->vals[i].i;
    }
    tok_str_free(names);
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

static void do_unittest(AttrDef *ad)
{
    CType type;
    Sym *sym, *sr;
    AttrDef ad_sym;
    char name[64];

    memset(&ad_sym, 0, sizeof ad_sym);
    if (ad) {
        ad_sym.f.func_pure = ad->f.func_pure;
        ad_sym.f.func_safety = ad->f.func_safety;
    }
    snprintf(name, sizeof name, "__unittest_%d", nb_unittest++);

    /* build the type of 'static void `__unittest_N`(void)' */
    type.t = VT_VOID;
    type.ref = NULL;
    sr = sym_push(SYM_FIELD, &type, 0, 0);
    sr->f.func_type = FUNC_NEW;
    sr->f.func_call = FUNC_CDECL;
    sr->f.func_args = 0;
    sr->f.func_ctor = 1;
    sr->f.func_pure = ad_sym.f.func_pure;
    sr->f.func_safety = ad_sym.f.func_safety;
    sr->next = NULL;
    type.t = VT_FUNC | VT_STORAGE_STATIC;
    type.ref = sr;

    sym = external_sym(tok_alloc(name, strlen(name))->tok, &type, 0, &ad_sym);
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

/* `Parse` and act on a D `pragma(msg, ...)` directive.  `Returns`
   nonzero when the current token started such a construct.  */
/* `Return` the stream used for `pragma(msg)` output.  `Reuse` the
   preprocessing stream when one is set, otherwise use stderr.  */
static FILE *d_msg_stream(void)
{
    return tcc_state->ppfp ? tcc_state->ppfp : stderr;
}

static int check_d_pragma(int l)
{
    FILE *fp;
    int is_msg, is_alias;

    if (!tcc_state->lang_d || (l != VT_CONST && l != VT_LOCAL))
        return 0;
    is_alias = d_ident_is(tok, "$msg");
    if (!is_alias && tok != TOK_PRAGMA)
        return 0;
    next();
    skip('(');
    if (is_alias) {
        is_msg = 1;
    } else if (tok >= TOK_UIDENT) {
        is_msg = !strcmp(get_tok_str(tok, &tokc), "msg");
    } else {
        is_msg = 0;
    }
    if (!is_msg) {
        /* Ignore unknown pragmas after consuming their argument list.  */
        int depth = 1;
        while (depth > 0 && tok != TOK_EOF) {
            if (tok == '(')
                depth++;
            else if (tok == ')')
                depth--;
            if (depth > 0)
                next();
        }
        skip(')');
        if (tok == ';')
            next();
        return 1;
    }
    if (!is_alias)
        next();
    if (tok == ',')
        next();
    fp = d_msg_stream();
    while (tok != ')' && tok != TOK_EOF) {
        if (tok == TOK_STR) {
            fputs((const char *)tokc.str.data, fp);
            next();
        } else if (tok == TOK_LSTR) {
            next();
        } else if (d_tok_is_int(tok)) {
            fprintf(fp, "%lld", (long long)tokc.i);
            next();
        } else if (tok == TOK_CFLOAT) {
            fprintf(fp, "%g", (double)tokc.f);
            next();
        } else if (tok == TOK_CDOUBLE) {
            fprintf(fp, "%g", tokc.d);
            next();
        } else if (tok == TOK_CLDOUBLE) {
            fprintf(fp, "%Lg", tokc.ld);
            next();
        } else if (tok == TOK_CCHAR || tok == TOK_LCHAR) {
            fprintf(fp, "%lld", (long long)tokc.i);
            next();
        } else if (tok >= TOK_IDENT) {
            fputs(get_tok_str(tok, &tokc), fp);
            next();
        } else {
            tcc_error("unsupported pragma(msg) argument");
        }
        if (tok == ',')
            next();
        else
            break;
    }
    skip(')');
    if (tok == ';')
        next();
    fputc('\n', fp);
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
    TokenString *names = tok_str_alloc();
    int arity, i, addr, end_tok, ofs, r;
    skip('(');
    for (;;) {
        if (tok < TOK_UIDENT)
            expect("identifier");
        tok_str_add(names, tok);
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
        tcc_error("cannot destructure non-tuple type '%s'", buf);
    }
    arity = tuple_arity(&deduced);
    if (arity != names->len) {
        tcc_error("tuple destructuring pattern has %d names but the "
                  "initializer has %d elements", names->len, arity);
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

    for (i = 0; i < names->len; ++i) {
        int name = names->str[i];
        f = tuple_field(&ttype, i, &ofs);
        r = VT_LOCAL;
        if (!(f->type.t & VT_ARRAY))
            r |= VT_LVAL;
        sym_push(name, &f->type, r, addr + ofs);
    }
    tok_str_free(names);
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

#include "tccgen-decl.c"
