/* 'l' is `VT_LOCAL` or `VT_CONST` to define default storage type
   or `VT_CMP` if parsing old style parameter list
   or `VT_JMP` if parsing c99 for decl: for (int i = 0, ...) */
static int decl(int l)
{
    int v, has_init, r, oldint, end_tok = 0;
    TokenString *init_str;
    CType type, btype;
    Sym *sym, *sa;
    AttrDef ad, adbase;
    ElfSym *esym;

    while (1) {
        int colon_assign = 0;
        if (tok >= TOK_UIDENT) {
            int saved_tok = tok;
            next();
            if (tok == TOK_COLON_ASSIGN) {
                next(); /* consume := */
                btype.t = VT_AUTO;
                btype.ref = NULL;
                memset(&adbase, 0, sizeof(adbase));
                unget_tok(saved_tok);
                colon_assign = 1;
            } else {
                unget_tok(saved_tok);
            }
        }
        memset(&adbase, 0, sizeof(adbase));
        oldint = 0;
        if (check_d_conditional(l, 0))
            continue;
        if (check_d_pragma(l))
            continue;
        if (check_d_dollar_if(l, 0))
            continue;
        if (check_d_dollar_foreach(l, 0))
            continue;
        if (check_d_static_assert())
            continue;
        if (check_d_manifest_constant(l))
            continue;
        if (tok == TOK_UNITTEST) {
            AttrDef ad_unittest = {0};
            if (l != VT_CONST)
                tcc_error("unittest block is only allowed at file scope");
            next();
            if (tok == '@') {
                parse_d_attributes(&ad_unittest);
            }
            if (tok != '{')
                expect("'{'");
            if (tcc_state->do_unittest)
                do_unittest(&ad_unittest);
            else
                skip_or_save_block(NULL);
            continue;
        }
        if (tok == TOK_ALIAS1) {
            if (l != VT_CONST && l != VT_LOCAL)
                tcc_error("alias declaration is only allowed at file or block scope");
            do_d_alias();
            continue;
        }
        if (!colon_assign && !parse_btype(&btype, &adbase, l == VT_LOCAL)) {
            if (l == VT_JMP)
                return 0;
            /* skip redundant ';' if not in old parameter decl scope */
            if (tok == ';' && l != VT_CMP) {
                next();
                continue;
            }
            if (tok == TOK_STATIC_ASSERT) {
                do_Static_assert();
                continue;
            }
            if (l != VT_CONST) {
                if (tok == '{') {
                    pending_block_safety = adbase.f.func_safety;
                    pending_block_pure = adbase.f.func_pure;
                }
                break;
            }
            if (tok == TOK_ASM1 || tok == TOK_ASM2 || tok == TOK_ASM3) {
                /* global asm block */
                asm_global_instr();
                continue;
            }
            if (tok >= TOK_UIDENT) {
               /* special test for old K&R protos without explicit int
                  type. `Only` accepted when defining global data */
                btype.t = VT_INT;
                oldint = 1;
            } else {
                if (tok != TOK_EOF)
                    expect("declaration");
                break;
            }
        }

        /* D-style array suffixes apply to every declarator of the
           declaration, as in D */
        while (tok == '[')
            d_array_suffix(&btype, btype.t & VT_STORAGE);

        if (tok == ';') {
            if ((btype.t & VT_BTYPE) == VT_STRUCT
                && (btype.ref->v & ~SYM_STRUCT) < SYM_FIRST_ANOM)
                ; /* struct decl with named tag */
            else if (IS_ENUM(btype.t))
                ; /* enum decl */
            else
                tcc_warning("useless type defines no instances");
            if (l == VT_JMP)
                return 1;
            next();
            continue;
        }

        while (1) { /* iterate thru each declaration */
            type = btype;
	    ad = adbase;
            init_str = NULL;
            has_init = 0;
            if (has_auto_type(&btype) && tok == '(') {
                TokenString *pats, *pdup;
                int has_comma;

                next();         /* first token inside the group */
                pats = scan_paren_group(&pdup, &has_comma, 0);
                if (pdup)
                    tok_str_free(pdup);
                if (has_comma) {
                    if (l != VT_LOCAL)
                        tcc_error("tuple destructuring is only allowed at "
                                  "block scope");
                    begin_macro(pats, 1);
                    next();     /* the '(' of the pattern */
                    end_tok = decl_auto_tuple(&ad);
                    if (end_tok == ';') {
                        skip(';');
                        break;
                    }
                    next();     /* ',' */
                    continue;
                }
                /* not a pattern: replay the group for type_decl() */
                begin_macro(pats, 1);
                next();         /* the '(' of the declarator */
            }
            type_decl(&type, &ad, &v, l == VT_CMP ? TYPE_DIRECT | TYPE_PARAM : TYPE_DIRECT);
            if (cur_tcc_state && cur_tcc_state->dump_types)
                ptype("TYPE", &type, v);

            if (has_auto_type(&type)) {
                if (!colon_assign) {
                    if (tok != '=')
                        tcc_error("declaration with 'auto' has no initializer");
                    next();
                }
                has_init = 1;
                init_str = tok_str_alloc();
                {
                    int level = 0;
                    while (tok != TOK_EOF) {
                        int t = tok;
                        if (level == 0 && (t == ';' || t == ',')) {
                            end_tok = t;
                            break;
                        }
                        tok_str_add_tok(init_str);
                        if (t == '(' || t == '[' || t == '{')
                            ++level;
                        else if (t == ')' || t == ']' || t == '}') {
                            if (level)
                                --level;
                        }
                        next();
                    }
                    tok_str_add(init_str, TOK_EOF);
                }

                begin_macro(init_str, 1);
                next();
                {
                    CType deduced_type;
                    expr_type(&deduced_type, gexpr);
                    if (!(deduced_type.t & VT_ARRAY))
                        convert_expression_type(&deduced_type);
                    subst_auto_type(&type, &deduced_type);
                }
                end_macro();
            }
            if ((type.t & VT_BTYPE) == VT_FUNC) {
                if ((type.t & VT_STORAGE_STATIC) && (l != VT_CONST))
                    tcc_error("function without file scope cannot be static");
                /* if old style function prototype, we accept a
                   declaration list */
                sym = type.ref;
                if (sym->f.func_type == FUNC_OLD && l == VT_CONST) {
                    Sym *saved_kr_params = kr_params;
                    kr_params = sym;
		    ++local_scope;
                    decl(VT_CMP);
		    --local_scope;
                    kr_params = saved_kr_params;
                }
                if ((type.t & (VT_STORAGE_EXTERN|VT_STORAGE_INLINE)) == (VT_STORAGE_EXTERN|VT_STORAGE_INLINE)) {
                    /* `always_inline` functions must be handled as if they
                       don't generate multiple global defs, even if extern
                       inline, i.e. `GNU` inline semantics for those.  `Rewrite`
                       them into static inline.  */
                    if (tcc_state->gnu89_inline || sym->f.func_alwinl)
                        type.t = (type.t & ~VT_STORAGE_EXTERN) | VT_STORAGE_STATIC;
                    else
                        type.t &= ~VT_STORAGE_INLINE; /* always compile otherwise */
                }

            } else if (oldint) {
                tcc_warning("type defaults to int");
            }

            if (gnu_ext && (tok == TOK_ASM1 || tok == TOK_ASM2 || tok == TOK_ASM3)) {
                ad.asm_label = asm_label_instr();
                /* parse one last attribute list, after asm label */
                parse_attribute(&ad);
            #if 0
                /* gcc does not allow `__asm__`("label") with function definition,
                   but why not ... */
                if (tok == '{')
                    expect(";");
            #endif
            }

#ifdef TCC_TARGET_PE
            pe_check_linkage(&type, &ad);
#endif
            if (tok == '{') {
                if (l != VT_CONST)
                    tcc_error("cannot use local functions");
                if ((type.t & VT_BTYPE) != VT_FUNC)
                    expect("function definition");

                /* apply post-declaraton attributes */
                merge_funcattr(&type.ref->f, &ad.f);
                /* put function symbol */
                type.t &= ~VT_STORAGE_EXTERN;
                sym = external_sym(v, &type, 0, &ad);

                /* reject abstract declarators in function definition
                   make old-style float params double */
                for (sa = sym->type.ref; (sa = sa->next) != NULL;) {
                    if (!(sa->v & ~SYM_FIELD))
                        expect("identifier");
                    if (sa->type.t == VT_FLOAT
                        && sym->type.ref->f.func_type == FUNC_OLD) {
                        sa->type.t = VT_DOUBLE;
                    }
                }

                /* static inline functions are just recorded as a kind
                   of macro. `Their` code will be emitted at the end of
                   the compilation unit only if they are used */
                if (sym->type.t & VT_STORAGE_INLINE) {
                    struct InlineFunc *fn;
                    fn = tcc_malloc(sizeof *fn + strlen(file->filename));
                    strcpy(fn->filename, file->filename);
                    fn->sym = sym;
                    dynarray_add(&tcc_state->inline_fns,
				 &tcc_state->nb_inline_fns, fn);
                    skip_or_save_block(&fn->func_str);
                } else {
                    /* compute text section */
                    cur_text_section = text_section;
                    if (ad.section) {
                        cur_text_section = ad.section;
                        if (ad.new_section)
                            ad.section->sh_flags = text_section->sh_flags;
                    }
                    gen_function(sym);
                }
                break;
            } else {
                has_init = 0;
		if (l == VT_CMP) {
		    /* find parameter in function parameter list */
		    for (sym = kr_params->next; sym; sym = sym->next)
			if ((sym->v & ~SYM_FIELD) == v)
			    goto found;
		    tcc_error("declaration for parameter '%s' but no such parameter",
			      get_tok_str(v, NULL));
                found:
		    if (type.t & VT_STORAGE) /* 'register' is okay */
		        tcc_error("storage class specified for '%s'",
				  get_tok_str(v, NULL));
		    if (!(sym->type.t & VT_STORAGE_EXTERN))
		        tcc_error("redefinition of parameter '%s'",
				  get_tok_str(v, NULL));
		    convert_parameter_type(&type);
		    sym->type = type;
		} else if (type.t & VT_STORAGE_TYPEDEF) {
                    declare_typedef(v, &type, &ad);
		} else if ((type.t & VT_BTYPE) == VT_VOID
			   && !(type.t & VT_STORAGE_EXTERN)) {
		    tcc_error("declaration of void object");
                } else {
                    r = 0;
                    if ((type.t & VT_BTYPE) == VT_FUNC) {
                        /* external function definition */
                        /* specific case for `func_call` attribute */
                        merge_funcattr(&type.ref->f, &ad.f);
                    } else if (!(type.t & VT_ARRAY)) {
                        /* not lvalue if array */
                        r |= VT_LVAL;
                    }

                    if (tok == '=') {
                        int t1 = tok;
                        next();
                        if (tok == TOK_VOID) {
                            next();
                            has_init = 0;
                        } else {
                            unget_tok(tok);
                            unget_tok(t1);
                            next();
                            has_init = 1;
                        }
                    } else if (ad.noinit) {
                        has_init = 0;
                    }

                    if (((type.t & VT_STORAGE_EXTERN) && (!has_init || l != VT_CONST))
		        || (type.t & VT_BTYPE) == VT_FUNC
                        /* as with `GCC`, uninitialized global arrays with no size
                           are considered extern: */
                        || ((type.t & VT_ARRAY) && !has_init
                            && l == VT_CONST && type.ref->c < 0)
                        ) {
                        /* external variable or function */
                        type.t |= VT_STORAGE_EXTERN;
                        external_sym(v, &type, r, &ad);
                    } else {
                        if (l == VT_CONST || (type.t & VT_STORAGE_STATIC))
                            r |= VT_CONST;
                        else
                            r |= VT_LOCAL;
                        type.t &= ~VT_STORAGE_EXTERN;
                        if (init_str) {
                            begin_macro(init_str, 1);
                            next();
                        } else if (has_init)
                            next();
                        else if (l == VT_CONST)
                            /* uninitialized global variables may be overridden */
                            type.t |= VT_STORAGE_EXTERN;
                        decl_initializer_alloc(&type, &ad, r, has_init || (init_str != NULL), v, l);
                        if (init_str) {
                            end_macro();
                            unget_tok(end_tok);
                            tok_str_free(init_str);
                        }
                    }

                    if (ad.alias_target && l == VT_CONST) {
                        /* `Aliases` need to be emitted when their target symbol
                           is emitted, even if perhaps unreferenced.
                           `We` only support the case where the base is already
                           defined, otherwise we would need deferring to emit
                           the aliases until the end of the compile unit.  */
                        esym = elfsym(sym_find(ad.alias_target));
                        if (!esym)
                            tcc_error("unsupported forward __alias__ attribute");
                        put_extern_sym2(sym_find(v), esym->st_shndx,
                                        esym->st_value, esym->st_size, 1);
                    }
                }
                if (tok != ',') {
                    if (l == VT_JMP)
                        return has_init ? v : 1;
                    skip(';');
                    break;
                }
                next();
            }
        }
    }
    return 0;
}

/* `Emit` an element-wise 'a[lo..hi] op= x' for a D slice.  '`slice_type`'
   is the destination slice type, 'elem' its (unqualified) element type,
   'dst' the stack offset of the temporary holding the destination
   slice header and 'op' the binary operator of the compound
   assignment.  `The` scalar right operand is on top of the value
   stack.  */
static void gen_slice_op_assign(CType *slice_type, CType *elem, int dst,
                                int op)
{
    CType len_type, ptr_type;
    int size, align, esize, ealign, rhs_addr, idx_addr;
    int cond, exit;

    len_type = slice_type->ref->next->type;
    ptr_type = slice_type->ref->next->next->type;
    esize = type_size(elem, &ealign);

    /* `Evaluate` the right operand exactly once.  */
    loc = (loc - esize) & -ealign;
    rhs_addr = loc;
    vset(elem, VT_LOCAL | VT_LVAL, rhs_addr);
    vswap();
    vstore();
    vpop();

    if (nocode_wanted)
        return;

    size = type_size(&len_type, &align);
    loc = (loc - size) & -align;
    idx_addr = loc;

    /* idx = 0 */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vpushi(0);
    vstore();
    vpop();

    cond = gind();
    /* idx < dst.length */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vset(&len_type, VT_LOCAL | VT_LVAL, dst);
    gen_op(TOK_LT);
    exit = gvtst(1, 0);

    /* dst.ptr[idx] op= rhs */
    vset(&ptr_type, VT_LOCAL | VT_LVAL, dst + PTR_SIZE);
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    gen_op('+');
    indir();
    /* the read-only location, if any, was already diagnosed above */
    vtop->type.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
    vdup();
    vset(elem, VT_LOCAL | VT_LVAL, rhs_addr);
    gen_op(op);
    vstore();
    vpop();

    /* ++idx */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vpushi(1);
    gen_op('+');
    vstore();
    vpop();

    gjmp_addr(cond);
    gsym(exit);
}

/* `Emit` a D slice assignment, with the destination slice value on top of
   the value stack.  '`assign_tok`' is '=' for a bulk copy or fill, or a
   compound assignment token for an element-wise operation.  */
static void gen_slice_assign(int assign_tok)
{
    CType elem, elem_nc, dest_type, slice_type, rhs_elem;
    int esize, ealign, l, r2;

    dest_type = vtop->type;
    if (!is_slice_type(&dest_type))
        tcc_error("left operand of slice assignment must be a D slice");
    elem = *pointed_type(&dest_type.ref->next->next->type);
    esize = type_size(&elem, &ealign);

    if ((dest_type.t | elem.t) & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE))
        tcc_warning("assignment of read-only location");

    /* `The` temporaries used below are writable; don't inherit the
       qualifiers of the destination elements and warn twice.  */
    elem_nc = elem;
    elem_nc.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);

    if (assign_tok == TOK_A_CAT)
        tcc_error("cannot append to a slice range");
    if (assign_tok != '=' && !tcc_state->lang_d)
        tcc_error("only '=' is supported for slice assignment (use -lang=d)");

    l = copy_slice_to_temp(&dest_type);

    next(); /* consume the assignment operator */

    expr_cond();

    if (assign_tok != '=') {
        gen_slice_op_assign(&dest_type, &elem_nc, l, TOK_ASSIGN_OP(assign_tok));
    } else {
        /* `An` array right operand is bulk-copied, unless it is the
           element type itself.  */
        if (!is_slice_type(&vtop->type)
            && (vtop->type.t & (VT_ARRAY | VT_VLA))) {
            if (!is_compatible_unqualified_types(&vtop->type.ref->type, &elem)
                && !(vtop->type.ref->type.t & VT_QUALIFIER_IMMUTABLE)) {
                cast_error(&vtop->type, &dest_type);
            }
            rhs_elem = vtop->type.ref->type;
            slice_type.t = VT_STRUCT;
            slice_type.ref = make_slice_type(&rhs_elem);
            convert_array_to_slice(&slice_type);
        }

        if (is_slice_type(&vtop->type)) {
            CType *re = pointed_type(&vtop->type.ref->next->next->type);
            int r_slice;

            if (!is_compatible_unqualified_types(re, &elem))
                cast_error(&vtop->type, &dest_type);
            r_slice = copy_slice_to_temp(&vtop->type);

            vpush_helper_func(TOK___darray_copy);
            vset(&char_pointer_type, VT_LOCAL, l);
            vset(&char_pointer_type, VT_LOCAL, r_slice);
            vpushs(esize);
            vpushi(tcc_state->slice_len_mismatch);
            gfunc_call(4);
        } else {
            int elem_addr;

            elem_addr = get_temp_local_var(esize, ealign, &r2);
            vset(&elem_nc, VT_LOCAL | VT_LVAL, elem_addr);
            vswap();
            vstore();
            vpop();

            vpush_helper_func(TOK___darray_fill);
            vset(&char_pointer_type, VT_LOCAL, l);
            vset(&elem_nc, VT_LOCAL | VT_LVAL, elem_addr);
            gaddrof();
            vtop->type = char_pointer_type;
            vpushs(esize);
            gfunc_call(3);
        }
    }
    vset(&dest_type, VT_LOCAL | VT_LVAL, l);
}

/* ------------------------------------------------------------------------- */
#undef gjmp_addr
#undef gjmp
/* ------------------------------------------------------------------------- */
