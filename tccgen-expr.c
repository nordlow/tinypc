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
        if (tok_is_imaginary) {
            type.t |= VT_IMAGINARY;
            tok_is_imaginary = 0;
        }
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
        /* In order to force cast, we add zero, except for floating point
	   where we really need an noop (otherwise -0.0 will be transformed
	   into +0.0).  */
	if (!is_float(vtop->type.t)) {
	    vpushi(0);
	    gen_op('+');
	}
        break;
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
        if (tok >= TOK_UIDENT) {
            const char *id = get_tok_str(tok, &tokc);
            if (!strcmp(id, "__real__")) {
                next();
                unary();
                gen_complex_part(0);
                break;
            } else if (!strcmp(id, "__imag__")) {
                next();
                unary();
                gen_complex_part(1);
                break;
            }
        }
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
            if (!is_arrow && tok >= TOK_UIDENT) {
                const char *member_name = get_tok_str(tok, NULL);
                if (!strcmp(member_name, "re") || !strcmp(member_name, "im")) {
                    int is_im = !strcmp(member_name, "im");
                    next();
                    gen_complex_part(is_im);
                    continue;
                }
            }
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
                    vtop->r |= VT_SLICE_LENGTH;
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
            int base_is_slice, slice_check = 0, slice_tmp = 0;
            int safe_divisor = 0;
            const char *check_file = NULL;
            int check_line = 0;
            int missing_lower = 0, inclusive = 0;
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
            slice_check = base_is_slice && slice_bounds_checking();
            if (slice_check) {
                check_file = tcc_basename(file->filename);
                check_line = file->line_num;
            }
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
            if (tok == TOK_TWODOTS || tok == TOK_DOTS) {
                next();
                if (tok == ']') {
                    /* full slice: a[..] */
                    if (nocode_wanted & DATA_ONLY_WANTED)
                        tcc_error("slice expression is not computable at load time");
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
                missing_lower = 1;
                vpushi(0);
                if (tok == '=') {
                    inclusive = 1;
                    next();
                }
            } else if (tcc_state->lang_d && tok == ']') {
                /* full slice: a[] in D mode */
                if (nocode_wanted & DATA_ONLY_WANTED)
                    tcc_error("slice expression is not computable at load time");
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
                if (slice_check) {
                    /* keep a copy of the slice header so that its
                       length is still available after the '$' and
                       index expressions have been evaluated */
                    slice_tmp = copy_slice_to_temp(&base_type);
                    vset(&base_type, VT_LOCAL | VT_LVAL, slice_tmp);
                }
                slice_to_ptr();
            } else if (base_type.t & (VT_ARRAY | VT_VLA)) {
                elem_type = base_type.ref->type;
                elem_type.t |= base_type.t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
            } else if ((base_type.t & VT_BTYPE) == VT_PTR) {
                elem_type = *pointed_type(&base_type);
            } else {
                elem_type = base_type;
            }

            if (missing_lower) {
                /* lower bound already pushed as 0 */
            } else if (slice_check && tok >= TOK_UIDENT
                && !strcmp(get_tok_str(tok, NULL), "$")) {
                TokenString *saved_macro = macro_stack_top();
                TokenString *idx_ts = tok_str_alloc();

                scan_subscript_index(idx_ts);
                safe_divisor = subscript_safe_divisor(idx_ts);
                begin_macro(idx_ts, 1);
                next();
                gexpr();
                while (macro_stack_top() != saved_macro)
                    end_macro();
            } else {
                gexpr();
            }
            if (missing_lower || tok == TOK_TWODOTS || tok == TOK_DOTS) {
                if (!missing_lower) {
                    next();
                    if (tok == '=') {
                        inclusive = 1;
                        next();
                    }
                }
                if (nocode_wanted & DATA_ONLY_WANTED)
                    tcc_error("slice expression is not computable at load time");
                if (tok == ']') {
                    /* missing upper bound: V[L..] => V[L..$] */
                    gen_dollar();
                } else {
                    gexpr();
                }
                skip(']');
                nb_dollar_ctx--;
                if (slice_check)
                    gen_slice_range_check(slice_tmp, inclusive,
                                          check_file, check_line);
                /* [base, lo, hi] -> [base + lo, hi - lo] */
                vpushv(vtop - 1);   /* lo */
                gen_op('-');        /* hi - lo */
                if (inclusive) {
                    vpushi(1);
                    gen_op('+');
                }
                vrott(3);           /* [len, base, lo] */
                if (slice_check)
                    gen_slice_ptr_add();
                else
                    gen_op('+');    /* [len, base + lo] */
                vswap();            /* [ptr, len] */
                gen_slice_value(&elem_type);
                slice_range_target = 1;
            } else {
                if (slice_check) {
                    if (safe_divisor >= 2)
                        gen_slice_nonempty_check(slice_tmp, check_file,
                                                 check_line);
                    else
                        gen_slice_index_check(slice_tmp, check_file,
                                              check_line);
                    gen_slice_ptr_add();
                } else {
                    gen_op('+');
                }
                indir();
                /* the element's length check replaces the allocation
                   check of the generic pointer bounds checker */
                if (slice_check)
                    vtop->r &= ~VT_MUSTBOUND;
                skip(']');
                nb_dollar_ctx--;
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
    if (vtop->type.t & (VT_COMPLEX | VT_IMAGINARY))
        vtop->r |= VT_NONCONST;
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

#include "tccgen-stmt.c"
