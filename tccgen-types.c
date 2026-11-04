/* return type size as known at compile time. `Put` alignment at 'a' */
ST_FUNC int type_size(CType *type, int *a)
{
    Sym *s;
    int bt;
    if (type->t & VT_COMPLEX) {
        int base_size, base_align;
        CType base_type = *type;
        base_type.t &= ~VT_COMPLEX;
        base_size = type_size(&base_type, &base_align);
        *a = base_align;
        return 2 * base_size;
    }
    if (type->t & VT_IMAGINARY) {
        int base_size, base_align;
        CType base_type = *type;
        base_type.t &= ~VT_IMAGINARY;
        base_size = type_size(&base_type, &base_align);
        *a = base_align;
        return base_size;
    }
    bt = type->t & VT_BTYPE;
    if (bt == VT_AUTO) {
        CType *tv = type_value_of(type->ref);
        if (tv) {
            int ts = type_size(tv, a);
            return ts;
        }
        *a = 1;
        return 1;
    }
    if (bt == VT_STRUCT) {
        /* struct/union */
        s = type->ref;
        *a = s->r;
        return s->c;
    } else if (bt == VT_PTR) {
        if (type->t & VT_ARRAY) {
            int ts;
            s = type->ref;
            ts = type_size(&s->type, a);
            if (s->c < 0)
                return s->c;
            return ts * s->c;
        } else {
            *a = PTR_SIZE;
            return PTR_SIZE;
        }
    } else if (IS_ENUM(type->t) && type->ref->c < 0) {
        *a = 0;
        return -1; /* incomplete enum */
    } else if (bt == VT_LDOUBLE) {
        *a = LDOUBLE_ALIGN;
        return LDOUBLE_SIZE;
    } else if (bt == VT_DOUBLE || bt == VT_LLONG) {
#if (defined TCC_TARGET_I386 && !defined TCC_TARGET_PE) \
 || (defined TCC_TARGET_ARM && !defined TCC_ARM_EABI)
        *a = 4;
#else
        *a = 8;
#endif
        return 8;
    } else if (bt == VT_INT || bt == VT_FLOAT) {
        *a = 4;
        return 4;
    } else if (bt == VT_SHORT) {
        *a = 2;
        return 2;
    } else if (bt == VT_QLONG || bt == VT_QFLOAT) {
        *a = 8;
        return 16;
    } else if (bt == VT_SLICE) {
        *a = PTR_SIZE;
        return 2 * PTR_SIZE;
    } else {
        /* char, void, function, `_Bool` */
        *a = 1;
        return 1;
    }
}

/* push type size as known at runtime time on top of value stack. `Put`
   alignment at 'a' */
static void vpush_type_size(CType *type, int *a)
{
    if (type->t & VT_VLA) {
        type_size(&type->ref->type, a);
        vset(&int_type, VT_LOCAL|VT_LVAL, type->ref->c);
    } else {
        int size = type_size(type, a);
        if (size < 0)
            tcc_error("unknown type size");
        vpushs(size);
    }
}

/* return the pointed type of t */
static inline CType *pointed_type(CType *type)
{
    return &type->ref->type;
}

/* return true if 'type' is a slice type (T[..]) */
static int is_slice_type(CType *type)
{
    return (type->t & VT_BTYPE) == VT_STRUCT
        && type->ref && type->ref->a.is_slice;
}

/* `Return` true if 'type' is a scalar type, i.e. a single arithmetic,
   pointer or boolean value that can be held in a temporary of its own
   type.  */
static int is_scalar_value_type(CType *type)
{
    switch (type->t & VT_BTYPE) {
    case VT_BYTE: case VT_SHORT: case VT_INT: case VT_LLONG:
    case VT_PTR: case VT_BOOL:
    case VT_FLOAT: case VT_DOUBLE: case VT_LDOUBLE:
        return 1;
    default:
        return 0;
    }
}

/* `Return` the number of elements an array of type 'type' has when it is
   viewed through a slice.  `Arrays` that hold a string literal store the
   terminating null character as well, which is not part of the slice.  */
static int array_slice_length(CType *type)
{
    int n = type->ref->c;
    if (type->ref->a.str_lit && n > 0)
        --n;
    return n;
}

/* return true if token 't' can start a type specifier list */
static int is_type_start(int t)
{
    Sym *s;
    if (tcc_state->lang_d && t >= TOK_IDENT) {
        const char *sstr = get_tok_str(t, NULL);
        if (!strcmp(sstr, "func") || !strcmp(sstr, "proc"))
            return 1;
    }

     if (t >= TOK_UIDENT && get_tok_str(t, NULL)[0] == '$')
         return 0;

     switch (t) {
    case TOK_VOID: case TOK_CHAR: case TOK_SHORT: case TOK_INT:
    case TOK_LONG: case TOK_FLOAT: case TOK_DOUBLE: case TOK_BOOL:
    case TOK_COMPLEX: case TOK_IMAGINARY:
    case TOK_STRUCT: case TOK_UNION: case TOK_ENUM:
    case TOK_SIGNED1: case TOK_SIGNED2: case TOK_SIGNED3:
    case TOK_UNSIGNED: case TOK__Atomic:
    case TOK_CONST1: case TOK_CONST2: case TOK_CONST3:
    case TOK_VOLATILE1: case TOK_VOLATILE2: case TOK_VOLATILE3:
    case TOK_RESTRICT1: case TOK_RESTRICT2: case TOK_RESTRICT3:
    case TOK_TYPEOF1: case TOK_TYPEOF2: case TOK_TYPEOF3:
    case TOK_ALIGNAS: case TOK_EXTENSION:
    case TOK_IMMUTABLE:
        return 1;
    case '@':
        return tcc_state->lang_d;
    case TOK_D_CFLOAT: case TOK_D_CDOUBLE: case TOK_D_CREAL:
    case TOK_D_IFLOAT: case TOK_D_IDOUBLE: case TOK_D_IREAL:
        return tcc_state->lang_d;
    }
    s = sym_find(t);
    return s && (s->type.t & VT_STORAGE_TYPEDEF);
}

static void check_restrict_type(CType *type)
{
    while ((type->t & VT_BTYPE) == VT_PTR) {
        if ((type->t & VT_QUALIFIER_RESTRICT)
            && (pointed_type(type)->t & VT_BTYPE) == VT_FUNC)
            tcc_error("restrict-qualified type must be a pointer to object or incomplete type");
        type = pointed_type(type);
    }
    if (type->t & VT_QUALIFIER_RESTRICT)
        tcc_error("restrict-qualified type must be a pointer to object or incomplete type");
}

/* `Array` qualifiers are represented on the element type. */
static int type_qualifiers(CType *type)
{
    while (type->t & VT_ARRAY)
        type = pointed_type(type);
    return type->t & VT_QUALIFIER;
}

/* modify type so that its it is a pointer to type. */
ST_FUNC void mk_pointer(CType *type)
{
    Sym *s;
    s = sym_push(SYM_FIELD, type, 0, -1);
    type->t = VT_PTR | (type->t & VT_STORAGE);
    type->ref = s;
}

/* return true if type1 and type2 are exactly the same (including
   qualifiers).
*/
static int is_compatible_types(CType *type1, CType *type2)
{
    return compare_types(type1,type2,0);
}

/* return true if type1 and type2 are the same (ignoring qualifiers).
*/
static int is_compatible_unqualified_types(CType *type1, CType *type2)
{
    return compare_types(type1,type2,1);
}

static void cast_error(CType *st, CType *dt)
{
    type_incompatibility_error(st, dt, "cannot convert '%s' to '%s'");
}

/* `Convert` the array expression on top of the value stack into a slice
   value of type 'dt'.  `Return` non-zero if a conversion was performed.  */
static int convert_array_to_slice(CType *dt)
{
    CType st, len_type, ptr_type;
    Sym *s;
    int size, align, r2, l, elem_size;

    st = vtop->type;
    if (!(st.t & (VT_ARRAY | VT_VLA)) || !is_slice_type(dt))
        return 0;

    s = dt->ref;
    ptr_type = s->next->next->type;
    if (!is_compatible_unqualified_types(&st.ref->type,
                                         pointed_type(&ptr_type)))
        cast_error(&st, dt);

    size = type_size(dt, &align);
    l = get_temp_local_var(size, align, &r2);

    /* .length */
    if (st.t & VT_VLA) {
        elem_size = type_size(&st.ref->type, &align);
        vset(&int_type, VT_LOCAL | VT_LVAL, st.ref->c);
        if (elem_size != 1) {
            vpushi(elem_size);
            gen_op('/');
        }
    } else {
        vpushs(array_slice_length(&st));
    }
    len_type = s->next->type;
    vset(&len_type, VT_LOCAL | VT_LVAL, l);
    vswap();
    vstore();
    vpop();

    /* .ptr */
    vset(&ptr_type, VT_LOCAL | VT_LVAL, l + PTR_SIZE);
    vswap();
    vstore();
    vpop();

    vset(dt, VT_LOCAL | VT_LVAL, l);
    vtop->r2 = r2;
    return 1;
}

/* verify type compatibility to store vtop in 'dt' type */
static void verify_assign_cast(CType *dt)
{
    CType *st, *type1, *type2;
    int dbt, sbt, qualwarn, lvl;

    st = &vtop->type; /* source type */
    dbt = dt->t & VT_BTYPE;
    sbt = st->t & VT_BTYPE;
    if ((vtop->r & VT_LVAL) && vtop->sym && vtop->sym->a.nocopy)
        tcc_error("cannot copy @nocopy instance '%s'", get_tok_str(vtop->sym->v & ~SYM_FIELD, NULL));
    if (sbt == VT_STRUCT && st->ref && st->ref->a.nocopy)
        tcc_error("rhs struct '%s' is not copyable (or aliasable)", get_tok_str(st->ref->v & ~SYM_STRUCT, NULL));
    if (dt->t & VT_QUALIFIER_CONSTANT)
        tcc_warning("assignment of read-only location");
    switch(dbt) {
    case VT_VOID:
        if (sbt != dbt)
            tcc_error("assignment to void expression");
        break;
    case VT_PTR:
        /* special cases for pointers */
        /* '0' can also be a pointer */
        if (is_null_pointer(vtop))
            break;
        /* accept implicit pointer to integer cast with warning */
        if (is_integer_btype(sbt)) {
            tcc_warning("assignment makes pointer from integer without a cast");
            break;
        }
        type1 = pointed_type(dt);
        if (sbt == VT_PTR)
            type2 = pointed_type(st);
        else if (sbt == VT_FUNC)
            type2 = st; /* a function is implicitly a function pointer */
        else
            goto error;
        if (is_compatible_types(type1, type2))
            break;
        for (qualwarn = lvl = 0;; ++lvl) {
            int discards = (type2->t & ~type1->t) & VT_QUALIFIER;
            /* dropping 'immutable' is only allowed for 'const' */
            if ((discards & VT_QUALIFIER_IMMUTABLE) && !(type1->t & VT_QUALIFIER_CONSTANT))
                tcc_error("assignment discards 'immutable' qualifier");
            if (discards & ~VT_QUALIFIER_IMMUTABLE)
                qualwarn = 1;
            dbt = type1->t & (VT_BTYPE|VT_LONG);
            sbt = type2->t & (VT_BTYPE|VT_LONG);
            if (dbt != VT_PTR || sbt != VT_PTR)
                break;
            type1 = pointed_type(type1);
            type2 = pointed_type(type2);
        }
        if (!is_compatible_unqualified_types(type1, type2)) {
            if ((dbt == VT_VOID || sbt == VT_VOID) && lvl == 0) {
                /* void * can match anything */
            } else if (dbt == sbt
                && is_integer_btype(sbt & VT_BTYPE)
                && IS_ENUM(type1->t) + IS_ENUM(type2->t)
                    + !!((type1->t ^ type2->t) & VT_UNSIGNED) < 2) {
		/* `Like` `GCC` don't warn by default for merely changes
		   in pointer target signedness.  `Do` warn for different
		   base types, though, in particular for unsigned enums
		   and signed int targets.  */
            } else {
                tcc_warning("assignment from incompatible pointer type");
                break;
            }
        }
        if (qualwarn)
            tcc_warning_c(warn_discarded_qualifiers)("assignment discards qualifiers from pointer target type");
        break;
    case VT_BYTE:
    case VT_SHORT:
    case VT_INT:
    case VT_LLONG:
        if (sbt == VT_PTR || sbt == VT_FUNC) {
            tcc_warning("assignment makes integer from pointer without a cast");
        } else if (sbt == VT_STRUCT) {
            goto case_VT_STRUCT;
        }
        /* `XXX`: more tests */
        break;
    case VT_STRUCT:
    case_VT_STRUCT:
        if (!is_compatible_unqualified_types(dt, st)) {
    error:
            cast_error(st, dt);
        }
        break;
    }
}

static void gen_assign_cast(CType *dt)
{
    convert_array_to_slice(dt);
    verify_assign_cast(dt);
    gen_cast(dt);
}

/* check stack address escape to global storage or return */
static void check_stack_address_escape(Sym *dest_sym, Sym *src_sym, int is_return, CType *fn_vt)
{
    int is_safe = (cur_scope->fn.safety == FUNC_SAFETY_SAFE);
    int is_error = is_safe || tcc_state->stack_address_escape_error;
    int is_warn = tcc_state->warn_stack_address_escape;
    if (is_return) {
        if (vtop >= vstack && (fn_vt->t & VT_BTYPE) == VT_PTR &&
            src_sym && src_sym->sym_scope > 0 && !(src_sym->type.t & VT_STORAGE_STATIC) &&
            ((src_sym->type.t & VT_ARRAY) || ((src_sym->type.t & VT_BTYPE) != VT_PTR))) {
            if (is_error)
                tcc_error("returning address of local variable '%s'", get_tok_str(src_sym->v & ~SYM_FIELD, NULL));
            else if (is_warn)
                tcc_warning_c(warn_stack_address_escape)("returning address of local variable '%s'", get_tok_str(src_sym->v & ~SYM_FIELD, NULL));
            else {
                if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                    cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
                if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                    root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            }
        }
    } else {
        if (vtop >= vstack + 1 &&
            (dest_sym && dest_sym->sym_scope == 0) &&
            (src_sym && src_sym->sym_scope > 0 && !(src_sym->type.t & VT_STORAGE_STATIC) &&
             ((src_sym->type.t & VT_ARRAY) || ((src_sym->type.t & VT_BTYPE) != VT_PTR)))) {
            if (is_error)
                tcc_error("assignment of address of stack variable '%s' to global/static storage", get_tok_str(src_sym->v & ~SYM_FIELD, NULL));
            else if (is_warn)
                tcc_warning_c(warn_stack_address_escape)("assignment of address of stack variable '%s' to global/static storage", get_tok_str(src_sym->v & ~SYM_FIELD, NULL));
            else {
                if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                    cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
                if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                    root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            }
        }
    }
}

/* `Copy` 'size' bytes from the source address at vtop to the destination
   address at vtop[-1], leaving the stack below the two addresses intact.
   `Use` the native block copy when available, otherwise `memmove`.  */
static void gen_copy_bytes(int size, int align)
{
#ifdef TCC_TARGET_NATIVE_STRUCT_COPY
    if (1
#ifdef CONFIG_TCC_BCHECK
        && !tcc_state->do_bounds_check
#endif
        ) {
        gen_struct_copy(size);
    } else
#endif
    {
        vpushi(size);
        /* `Use` memmove, rather than memcpy, as dest and src may be same: */
#ifdef TCC_ARM_EABI
        if (!(align & 7))
            vpush_helper_func(TOK_memmove8);
        else if (!(align & 3))
            vpush_helper_func(TOK_memmove4);
        else
#endif
        vpush_helper_func(TOK_memmove);
        vrott(4);
        gfunc_call(3);
    }
}

/* store vtop in lvalue pushed on stack */
ST_FUNC void vstore(void)
{
    int sbt, dbt, ft, r, size, align, bit_size, bit_pos, delayed_cast;

    convert_array_to_slice(&vtop[-1].type);

    ft = vtop[-1].type.t;
    sbt = vtop->type.t & VT_BTYPE;
    dbt = ft & VT_BTYPE;
    verify_assign_cast(&vtop[-1].type);

    if (vtop >= vstack + 1 &&
        (vtop[-1].r & VT_SYM || (vtop[-1].r & VT_VALMASK) == VT_LOCAL || (vtop[-1].r & VT_VALMASK) == VT_LLOCAL || (vtop[-1].r & VT_LVAL)) &&
        (vtop->r & VT_SYM || (vtop->r & VT_VALMASK) == VT_LOCAL || (vtop->r & VT_VALMASK) == VT_LLOCAL || (vtop->r & VT_LVAL)) &&
        sbt == VT_PTR) {
        check_stack_address_escape(vtop[-1].sym, vtop->sym, 0, NULL);
    }

    if (vtop[-1].r & VT_SLICE_LENGTH) {
        if (cur_scope->fn.safety == FUNC_SAFETY_SAFE)
            tcc_error("modification of slice '.length' not allowed in @safe scope"
                      " (use '.resize(n)' or @trusted code)");
        else {
            if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
        }
    }
    if ((vtop[-1].r & VT_LVAL) && vtop[-1].sym && vtop[-1].sym->a.final_var) {
        if (vtop[-1].sym->a.assigned)
            tcc_error("assignment to final variable '%s'", get_tok_str(vtop[-1].sym->v & ~SYM_FIELD, NULL));
        vtop[-1].sym->a.assigned = 1;
    }
    if (sbt == VT_STRUCT) {
        /* if structure, only generate pointer */
        /* structure assignment : generate memcpy */
        size = type_size(&vtop->type, &align);
        /* destination, keep on stack() as result */
        vpushv(vtop - 1);
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound(); /* check would be wrong after gaddrof() */
#endif
        vtop->type.t = VT_PTR;
        gaddrof();
        /* source */
        vswap();
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound();
#endif
        vtop->type.t = VT_PTR;
        gaddrof();
        gen_copy_bytes(size, align);

    } else if (ft & VT_BITFIELD) {
        /* bitfield store handling */

        /* save lvalue as expression result (example: s.b = s.a = n;) */
        vdup(), vtop[-1] = vtop[-2];

        bit_pos = BIT_POS(ft);
        bit_size = BIT_SIZE(ft);
        /* remove bit field info to avoid loops */
        vtop[-1].type.t = ft & ~VT_STRUCT_MASK;

        if (dbt == VT_BOOL) {
            gen_cast(&vtop[-1].type);
            vtop[-1].type.t = (vtop[-1].type.t & ~VT_BTYPE) | (VT_BYTE | VT_UNSIGNED);
        }
        r = adjust_bf(vtop - 1, bit_pos, bit_size);
        if (dbt != VT_BOOL) {
            gen_cast(&vtop[-1].type);
            dbt = vtop[-1].type.t & VT_BTYPE;
        }
        if (r == VT_STRUCT) {
            store_packed_bf(bit_pos, bit_size);
        } else {
            unsigned long long mask = (1ULL << bit_size) - 1;
            if (dbt != VT_BOOL) {
                /* mask source */
                if (dbt == VT_LLONG)
                    vpushll(mask);
                else
                    vpushi((unsigned)mask);
                gen_op('&');
            }
            /* shift source */
            vpushi(bit_pos);
            gen_op(TOK_SHL);
            vswap();
            /* duplicate destination */
            vdup();
            vrott(3);
            /* load destination, mask and or with source */
            if (dbt == VT_LLONG)
                vpushll(~(mask << bit_pos));
            else
                vpushi(~((unsigned)mask << bit_pos));
            gen_op('&');
            gen_op('|');
            /* store result */
            vstore();
            /* ... and discard */
            vpop();
        }
    } else if (dbt == VT_VOID) {
        --vtop;
    } else {
            /* optimize char/short casts */
            delayed_cast = 0;
            if ((dbt == VT_BYTE || dbt == VT_SHORT)
                && is_integer_btype(sbt)
                ) {
                if ((vtop->r & VT_MUSTCAST)
                    && btype_size(dbt) > btype_size(sbt)
                    )
                    force_charshort_cast();
                delayed_cast = 1;
            } else {
                gen_cast(&vtop[-1].type);
            }

#ifdef CONFIG_TCC_BCHECK
            /* bound check case */
            if (vtop[-1].r & VT_MUSTBOUND) {
                vswap();
                gbound();
                vswap();
            }
#endif
            gv(RC_TYPE(dbt)); /* generate value */

            if (delayed_cast) {
                vtop->r |= BFVAL(VT_MUSTCAST, (sbt == VT_LLONG) + 1);
                //tcc_warning("deley cast %x -> %x", sbt, dbt);
                vtop->type.t = ft & VT_TYPE;
            }

            /* if lvalue was saved on stack, must read it */
            if ((vtop[-1].r & VT_VALMASK) == VT_LLOCAL) {
                SValue sv;
                r = get_reg(RC_INT);
                sv.type.t = VT_PTRDIFF_T;
                sv.r = VT_LOCAL | VT_LVAL;
                sv.c.i = vtop[-1].c.i;
		sv.sym = NULL;
                load(r, &sv);
                vtop[-1].r = r | VT_LVAL;
            }

            r = vtop->r & VT_VALMASK;
            if (ft & VT_COMPLEX) {
                int base_t = dbt;
                CType base_type; base_type.t = base_t; base_type.ref = NULL;
                int b_align, b_size = type_size(&base_type, &b_align);
                vtop[-1].type.t = base_t;
                store(r, vtop - 1);
                vswap();
                incr_offset(b_size);
                vswap();
                store(vtop->r2, vtop - 1);
            } else if (USING_TWO_WORDS(dbt)) {
                int load_type = (dbt == VT_QFLOAT) ? VT_DOUBLE : VT_PTRDIFF_T;
                vtop[-1].type.t = load_type;
                store(r, vtop - 1);
                vswap();
                incr_offset(PTR_SIZE);
                vswap();
                /* XXX: it works because r2 is spilled last ! */
                store(vtop->r2, vtop - 1);
            } else {
                /* single word */
                store(r, vtop - 1);
            }
        vswap();
        vtop--; /* `NOT` vpop() because on x86 it would flush the fp stack */
    }
}

/* post defines `POST`/`PRE` add. c is the token ++ or -- */
ST_FUNC void inc(int post, int c)
{
    test_lvalue();
    if ((vtop->type.t & VT_BTYPE) == VT_PTR) {
        if (cur_scope->fn.safety == FUNC_SAFETY_SAFE)
            tcc_error("pointer arithmetic not allowed in @safe scope");
        else {
            if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
        }
    }
    vdup(); /* save lvalue */
    if (post) {
        gv_dup(); /* duplicate value */
        vrotb(3);
        vrotb(3);
    }
    /* add constant */
    vpushi(c - TOK_MID);
    gen_op('+');
    vstore(); /* store value */
    if (post)
        vpop(); /* if post op, return saved value */
}

ST_FUNC CString* parse_mult_str (const char *msg)
{
    /* read the string */
    if (tok != TOK_STR)
        expect(msg);
    cstr_reset(&initstr);
    while (tok == TOK_STR) {
        /* `XXX`: add \0 handling too ? */
        cstr_cat(&initstr, tokc.str.data, -1);
        next();
    }
    cstr_ccat(&initstr, '\0');
    return &initstr;
}

/* `If` I is >= 1 and a power of two, returns log2(i)+1.
   `If` I is 0 returns 0.  */
ST_FUNC int exact_log2p1(int i)
{
  int ret;
  if (!i)
    return 0;
  for (ret = 1; i >= 1 << 8; ret += 8)
    i >>= 8;
  if (i >= 1 << 4)
    ret += 4, i >>= 4;
  if (i >= 1 << 2)
    ret += 2, i >>= 2;
  if (i >= 1 << 1)
    ret++;
  return ret;
}

/* `Parse` `__attribute__`((...)) `GNUC` extension. */
static void parse_attribute(AttrDef *ad)
{
    int t, n;
    char *astr;
    AttrDef ad_tmp;

redo:
    if (tok != TOK_ATTRIBUTE1 && tok != TOK_ATTRIBUTE2)
        return;
    if (NULL == ad) /* skip over / ignore attributes */
        ad = &ad_tmp;

    next();
    skip('(');
    skip('(');
    while (tok != ')') {
        if (tok < TOK_IDENT)
            expect("attribute name");
        t = tok;
        next();
        switch(t) {
	case TOK_CLEANUP1:
	case TOK_CLEANUP2:
	{
	    Sym *s;

	    skip('(');
	    s = sym_find(tok);
	    if (!s) {
	        tcc_warning_c(warn_implicit_function_declaration)(
                    "implicit declaration of function '%s'", get_tok_str(tok, &tokc));
	        s = external_global_sym(tok, &func_old_type);
            } else if ((s->type.t & VT_BTYPE) != VT_FUNC)
                tcc_error("'%s' is not declared as function", get_tok_str(tok, &tokc));
	    ad->cleanup_func = s;
	    next();
            skip(')');
	    break;
	}
        case TOK_CONSTRUCTOR1:
        case TOK_CONSTRUCTOR2:
            ad->f.func_ctor = 1;
            break;
        case TOK_DESTRUCTOR1:
        case TOK_DESTRUCTOR2:
            ad->f.func_dtor = 1;
            break;
        case TOK_ALWAYS_INLINE1:
        case TOK_ALWAYS_INLINE2:
            ad->f.func_alwinl = 1;
            break;
        case TOK_NOCOPY1:
        case TOK_NOCOPY2:
            ad->a.nocopy = 1;
            break;
        case TOK_SECTION2:
            skip('(');
	    astr = parse_mult_str("section name")->data;
            n = tcc_state->nb_sections;
            ad->section = find_section(tcc_state, astr);
            ad->new_section = n < tcc_state->nb_sections;
            skip(')');
            break;
        case TOK_ALIAS1:
        case TOK_ALIAS2:
            skip('(');
	    astr = parse_mult_str("alias(\"target\")")->data;
            /* save string as token, for later */
            ad->alias_target = tok_alloc_const(astr);
            skip(')');
            break;
	case TOK_VISIBILITY1:
	case TOK_VISIBILITY2:
            skip('(');
	    astr = parse_mult_str("visibility(\"default|hidden|internal|protected\")")->data;
	    if (!strcmp (astr, "default"))
	        ad->a.visibility = STV_DEFAULT;
	    else if (!strcmp (astr, "hidden"))
	        ad->a.visibility = STV_HIDDEN;
	    else if (!strcmp (astr, "internal"))
	        ad->a.visibility = STV_INTERNAL;
	    else if (!strcmp (astr, "protected"))
	        ad->a.visibility = STV_PROTECTED;
	    else
                expect("visibility(\"default|hidden|internal|protected\")");
            skip(')');
            break;
        case TOK_ALIGNED1:
        case TOK_ALIGNED2:
            if (tok == '(') {
                next();
                n = expr_const();
                if (n <= 0 || (n & (n - 1)) != 0)
                    tcc_error("alignment must be a positive power of two");
                skip(')');
            } else {
                n = MAX_ALIGN;
            }
            ad->a.aligned = exact_log2p1(n);
	    if (n != 1 << (ad->a.aligned - 1))
	      tcc_error("alignment of %d is larger than implemented", n);
            break;
        case TOK_PACKED1:
        case TOK_PACKED2:
            ad->a.packed = 1;
            break;
        case TOK_WEAK1:
        case TOK_WEAK2:
            ad->a.weak = 1;
            break;
        case TOK_NODEBUG1:
        case TOK_NODEBUG2:
            ad->a.nodebug = 1;
            break;
        case TOK_USED1:
        case TOK_USED2:
        case TOK_UNUSED1:
        case TOK_UNUSED2:
            ad->a.used = 1;
            break;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
        case TOK_PURE1:
        case TOK_PURE2:
            ad->f.func_pure = FUNC_PURITY_TRUE;
            break;
        case TOK_NOINLINE:
	    /* ignored */
	    break;
        case TOK_FORMAT1:
        case TOK_FORMAT2:
	    /* ignored */
            goto skip_param;
        case TOK_NORETURN1:
        case TOK_NORETURN2:
            ad->f.func_noreturn = 1;
            break;
        case TOK_CDECL1:
        case TOK_CDECL2:
        case TOK_CDECL3:
            ad->f.func_call = FUNC_CDECL;
            break;
        case TOK_STDCALL1:
        case TOK_STDCALL2:
        case TOK_STDCALL3:
            ad->f.func_call = FUNC_STDCALL;
            break;
#ifdef TCC_TARGET_I386
        case TOK_REGPARM1:
        case TOK_REGPARM2:
            skip('(');
            n = expr_const();
            if (n > 3)
                n = 3;
            else if (n < 0)
                n = 0;
            if (n > 0)
                ad->f.func_call = FUNC_FASTCALL1 + n - 1;
            skip(')');
            break;
        case TOK_FASTCALL1:
        case TOK_FASTCALL2:
        case TOK_FASTCALL3:
            ad->f.func_call = FUNC_FASTCALLW;
            break;
        case TOK_THISCALL1:
        case TOK_THISCALL2:
        case TOK_THISCALL3:
            ad->f.func_call = FUNC_THISCALL;
            break;
#endif
        case TOK_MODE:
            skip('(');
            switch(tok) {
                case TOK_MODE_DI:
                    ad->attr_mode = VT_LLONG + 1;
                    break;
                case TOK_MODE_QI:
                    ad->attr_mode = VT_BYTE + 1;
                    break;
                case TOK_MODE_HI:
                    ad->attr_mode = VT_SHORT + 1;
                    break;
                case TOK_MODE_SI:
                case TOK_MODE_word:
                    ad->attr_mode = VT_INT + 1;
                    break;
                default:
                    tcc_warning("__mode__(%s) not supported\n", get_tok_str(tok, NULL));
                    break;
            }
            next();
            skip(')');
            break;
        case TOK_DLLEXPORT:
            ad->a.dllexport = 1;
            break;
        case TOK_NODECORATE:
            ad->a.nodecorate = 1;
            break;
        case TOK_DLLIMPORT:
            ad->a.dllimport = 1;
            break;
        default:
            tcc_warning_c(warn_unsupported)("'%s' attribute ignored", get_tok_str(t, NULL));
            /* skip parameters */
skip_param:
            if (tok == '(') {
                int parenthesis = 0;
                do {
                    if (tok == '(')
                        parenthesis++;
                    else if (tok == ')')
                        parenthesis--;
                    next();
                } while (parenthesis && tok != -1);
            }
            break;
        }
        if (tok != ',')
            break;
        next();
    }
    skip(')');
    skip(')');
    goto redo;
}

static Sym * find_field_opt (CType *type, int v, int *cumofs)
{
    int bt = type->t & VT_BTYPE;
    Sym *s;
    int v1 = v | SYM_FIELD;
    int tok_ptr, tok_len;
    static TCC_TLS Sym f_ptr;
    static TCC_TLS Sym f_len;

    if (bt == VT_SLICE) {
        tok_ptr = TOK_ptr | SYM_FIELD;
        tok_len = TOK_length | SYM_FIELD;
        if (v1 == tok_ptr) {
            *cumofs = PTR_SIZE;
            f_ptr.type = type->ref ? type->ref->type : int_type;
            mk_pointer(&f_ptr.type);
            f_ptr.c = PTR_SIZE;
            return &f_ptr;
        } else if (v1 == tok_len) {
            *cumofs = 0;
            f_len.type.t = VT_SIZE_T;
            f_len.type.ref = NULL;
            f_len.c = 0;
            return &f_len;
        }
        return NULL;
    }

    if (bt != VT_STRUCT)
        return NULL;
    s = type->ref;
    if (!s || s->c < 0)
        return NULL;
    while ((s = s->next) != NULL) {
        if (s->v == v1) {
            *cumofs = s->c;
            return s;
        }
        if ((s->type.t & VT_BTYPE) == VT_STRUCT
          && s->v >= (SYM_FIRST_ANOM | SYM_FIELD)) {
            Sym *ret = find_field_opt (&s->type, v1, cumofs);
            if (ret) {
                *cumofs += s->c;
                return ret;
            }
        }
    }
    return NULL;
}

static Sym * find_field (CType *type, int v, int *cumofs)
{
    int bt = type->t & VT_BTYPE;
    Sym *s = type->ref;
    int v1 = v | SYM_FIELD;
    int tok_ptr, tok_len;
    static TCC_TLS Sym f_ptr;
    static TCC_TLS Sym f_len;

    if (bt == VT_SLICE) {
        tok_ptr = TOK_ptr | SYM_FIELD;
        tok_len = TOK_length | SYM_FIELD;
        if (v1 == tok_ptr) {
            *cumofs = PTR_SIZE;
            f_ptr.type = type->ref ? type->ref->type : int_type;
            mk_pointer(&f_ptr.type);
            f_ptr.c = PTR_SIZE;
            return &f_ptr;
        } else if (v1 == tok_len) {
            *cumofs = 0;
            f_len.type.t = VT_SIZE_T;
            f_len.type.ref = NULL;
            f_len.c = 0;
            return &f_len;
        }
        if (!(v & SYM_FIELD))
            tcc_error("field not found: %s", get_tok_str(v, NULL));
        return NULL;
    }
    if (!(v & SYM_FIELD)) { /* top-level call */
        if (bt != VT_STRUCT)
            expect("struct or union");
        if (v < TOK_UIDENT)
            expect("field name");
        if (s->c < 0)
            tcc_error("dereferencing incomplete type '%s'",
                get_tok_str(s->v & ~SYM_STRUCT, 0));
    }
    while ((s = s->next) != NULL) {
        if (s->v == v1) {
            *cumofs = s->c;
            return s;
        }
        if ((s->type.t & VT_BTYPE) == VT_STRUCT
          && s->v >= (SYM_FIRST_ANOM | SYM_FIELD)) {
            /* try to find field in anonymous sub-struct/union */
            Sym *ret = find_field (&s->type, v1, cumofs);
            if (ret) {
                *cumofs += s->c;
                return ret;
            }
        }
    }
    if (!(v & SYM_FIELD))
        tcc_error("field not found: %s", get_tok_str(v, NULL));
    return s;
}

static void check_fields (CType *type, int check)
{
    Sym *s = type->ref;

    while ((s = s->next) != NULL) {
        int v = s->v & ~SYM_FIELD;
        if (v < SYM_FIRST_ANOM) {
            TokenSym *ts = table_ident[v - TOK_IDENT];
            if (check && (ts->tok & SYM_FIELD))
                tcc_error("duplicate member '%s'", get_tok_str(v, NULL));
            ts->tok ^= SYM_FIELD;
        } else if ((s->type.t & VT_BTYPE) == VT_STRUCT)
            check_fields (&s->type, check);
    }
}

static void struct_layout(CType *type, AttrDef *ad)
{
    int size, align, maxalign, offset, c, bit_pos, bit_size;
    int packed, a, bt, prevbt, prev_bit_size;
    int pcc = !tcc_state->ms_bitfields;
    int pragma_pack = *tcc_state->pack_stack_ptr;
    Sym *f;

    maxalign = 1;
    offset = 0;
    c = 0;
    bit_pos = 0;
    prevbt = VT_STRUCT; /* make it never match */
    prev_bit_size = 0;

//#define BF_DEBUG

    for (f = type->ref->next; f; f = f->next) {
        if (f->type.t & VT_BITFIELD)
            bit_size = BIT_SIZE(f->type.t);
        else
            bit_size = -1;
        size = type_size(&f->type, &align);
        a = f->a.aligned ? 1 << (f->a.aligned - 1) : 0;
        packed = 0;

        if (pcc && bit_size == 0) {
            /* in pcc mode, packing does not affect zero-width bitfields */

        } else {
            /* in pcc mode, attribute packed overrides if set. */
            if (pcc && (f->a.packed || ad->a.packed))
                align = packed = 1;

            /* pragma pack overrides align if lesser and packs bitfields always */
            if (pragma_pack) {
                packed = 1;
                if (pragma_pack < align)
                    align = pragma_pack;
                /* in pcc mode pragma pack also overrides individual align */
                if (pcc && pragma_pack < a)
                    a = 0;
            }
        }
        /* some individual align was specified */
        if (a)
            align = a;

        if (type->ref->type.t == VT_UNION) {
	    if (pcc && bit_size >= 0)
	        size = (bit_size + 7) >> 3;
	    offset = 0;
	    if (size > c)
	        c = size;

	} else if (bit_size < 0) {
            if (pcc)
                c += (bit_pos + 7) >> 3;
	    c = (c + align - 1) & -align;
	    offset = c;
	    if (size > 0)
	        c += size;
	    bit_pos = 0;
	    prevbt = VT_STRUCT;
	    prev_bit_size = 0;

	} else {
	    /* A bit-field.  `Layout` is more complicated.  `There` are two
	       options: `PCC` (`GCC`) compatible and `MS` compatible */
            if (pcc) {
		/* `In` `PCC` layout a bit-field is placed adjacent to the
                   preceding bit-fields, except if:
                   - it has zero-width
                   - an individual alignment was given
                   - it would overflow its base type container and
                     there is no packing */
                if (bit_size == 0) {
            new_field:
		    c = (c + ((bit_pos + 7) >> 3) + align - 1) & -align;
		    bit_pos = 0;
                } else if (f->a.aligned) {
                    goto new_field;
                } else if (!packed) {
                    int a8 = align * 8;
	            int ofs = ((c * 8 + bit_pos) % a8 + bit_size + a8 - 1) / a8;
                    if (ofs > size / align)
                        goto new_field;
                }

                /* in pcc mode, long long bitfields have type int if they fit */
                if (size == 8 && bit_size <= 32)
                    f->type.t = (f->type.t & ~VT_BTYPE) | VT_INT, size = 4;

                while (bit_pos >= align * 8)
                    c += align, bit_pos -= align * 8;
                offset = c;

		/* `In` `PCC` layout named bit-fields influence the alignment
		   of the containing struct using the base types alignment,
		   except for packed fields (which here have correct align).  */
		if (f->v & SYM_FIRST_ANOM
                    // && bit_size // ??? gcc on ARM/rpi does that
                    )
		    align = 1;

	    } else {
		bt = f->type.t & VT_BTYPE;
		if ((bit_pos + bit_size > size * 8)
                    || (bit_size > 0) == (bt != prevbt)
                    ) {
		    c = (c + align - 1) & -align;
		    offset = c;
		    bit_pos = 0;
		    /* `In` `MS` bitfield mode a bit-field run always uses
		       at least as many bits as the underlying type.
		       `To` start a new run it's also required that this
		       or the last bit-field had non-zero width.  */
		    if (bit_size || prev_bit_size)
		        c += size;
		}
		/* `In` `MS` layout the records alignment is normally
		   influenced by the field, except for a zero-width
		   field at the start of a run (but by further zero-width
		   fields it is again).  */
		if (bit_size == 0 && prevbt != bt)
		    align = 1;
		prevbt = bt;
                prev_bit_size = bit_size;
	    }

	    f->type.t = (f->type.t & ~(0x3f << VT_STRUCT_SHIFT))
		        | (bit_pos << VT_STRUCT_SHIFT);
	    bit_pos += bit_size;
	}
	if (align > maxalign)
	    maxalign = align;

#ifdef BF_DEBUG
	printf("set field %s offset %-2d size %-2d align %-2d",
	       get_tok_str(f->v & ~SYM_FIELD, NULL), offset, size, align);
	if (f->type.t & VT_BITFIELD) {
	    printf(" pos %-2d bits %-2d",
                    BIT_POS(f->type.t),
                    BIT_SIZE(f->type.t)
                    );
	}
	printf("\n");
#endif

        f->c = offset;
	f->r = 0;
    }

    if (pcc)
        c += (bit_pos + 7) >> 3;

    /* store size and alignment */
    type->ref->a.nocopy = ad->a.nocopy;
    a = bt = ad->a.aligned ? 1 << (ad->a.aligned - 1) : 1;
    if (a < maxalign)
        a = maxalign;
    type->ref->r = a;
    if (pragma_pack && pragma_pack < maxalign && 0 == pcc) {
        /* can happen if individual align for some member was given.  `In`
           this case `MSVC` ignores maxalign when aligning the size */
        a = pragma_pack;
        if (a < bt)
            a = bt;
    }
    c = (c + a - 1) & -a;
    type->ref->c = c;

#ifdef BF_DEBUG
    printf("struct size %-2d align %-2d\n\n", c, a), fflush(stdout);
#endif

    /* check whether we can access bitfields by their type */
    for (f = type->ref->next; f; f = f->next) {
        int s, px, cx, c0;
        CType t;

        if (0 == (f->type.t & VT_BITFIELD))
            continue;
        f->type.ref = f;
        f->auxtype = -1;
        bit_size = BIT_SIZE(f->type.t);
        if (bit_size == 0)
            continue;
        bit_pos = BIT_POS(f->type.t);
        size = type_size(&f->type, &align);

        if (bit_pos + bit_size <= size * 8 && f->c + size <= c
#ifdef TCC_TARGET_ARM
            && !(f->c & (align - 1))
#endif
            )
            continue;

        /* try to access the field using a different type */
        c0 = -1, s = align = 1;
        t.t = VT_BYTE;
        for (;;) {
            px = f->c * 8 + bit_pos;
            cx = (px >> 3) & -align;
            px = px - (cx << 3);
            if (c0 == cx)
                break;
            s = (px + bit_size + 7) >> 3;
            if (s > 4) {
                t.t = VT_LLONG;
            } else if (s > 2) {
                t.t = VT_INT;
            } else if (s > 1) {
                t.t = VT_SHORT;
            } else {
                t.t = VT_BYTE;
            }
            s = type_size(&t, &align);
            c0 = cx;
        }

        if (px + bit_size <= s * 8 && cx + s <= c
#ifdef TCC_TARGET_ARM
            && !(cx & (align - 1))
#endif
            ) {
            /* update offset and bit position */
            f->c = cx;
            bit_pos = px;
	    f->type.t = (f->type.t & ~(0x3f << VT_STRUCT_SHIFT))
		        | (bit_pos << VT_STRUCT_SHIFT);
            if (s != size)
                f->auxtype = t.t;
#ifdef BF_DEBUG
            printf("FIX field %s offset %-2d size %-2d align %-2d "
                "pos %-2d bits %-2d\n",
                get_tok_str(f->v & ~SYM_FIELD, NULL),
                cx, s, align, px, bit_size);
#endif
        } else {
            /* fall back to load/store single-byte wise */
            f->auxtype = VT_STRUCT;
#ifdef BF_DEBUG
            printf("FIX field %s : load byte-wise\n",
                 get_tok_str(f->v & ~SYM_FIELD, NULL));
#endif
        }
    }
}

/* `Does` 'n' fit into integer type 't' ? */
static int in_range(long long n, int t)
{
    unsigned long long m = (1ULL << (btype_size(t & VT_BTYPE) * 8 - 1)) - 1;
    if (t & VT_UNSIGNED)
        return n <= (m << 1) + 1;
    return n >= -(long long)m - 1 && n <= (long long)m;
}

/* enum/struct/union declaration. u is `VT_ENUM`/`VT_STRUCT`/`VT_UNION` */
static void struct_decl(CType *type, int u)
{
    int v, c, size, align, flexible;
    int bit_size, bsize, bt, ut;
    Sym *s, *ss, **ps;
    AttrDef ad1;
    CType type1, btype;

    AttrDef ad = {0};
    next();
    parse_attribute(&ad);

    v = 0;
    if (tok >= TOK_IDENT) /* struct/enum tag */
        v = tok, next();

    bt = ut = 0;
    if (u == VT_ENUM) {
        ut = VT_INT;
        if (tok == ':') { /* `C2x` enum : <type> ... */
            next();
            if (!parse_btype(&btype, &ad1, 0)
             || !is_integer_btype(btype.t & VT_BTYPE))
                expect("enum type");
            bt = ut = btype.t & (VT_BTYPE|VT_LONG|VT_UNSIGNED|VT_DEFSIGN);
        }
    }

    if (v) {
        /* struct already defined ? return it */
        s = struct_find(v);
        if (s && (s->sym_scope == local_scope || (tok != '{' && tok != ';'))) {
            if (u == s->type.t)
                goto do_decl;
            if (u == VT_ENUM && IS_ENUM(s->type.t)) /* `XXX`: check integral types */
                goto do_decl;
            tcc_error("redeclaration of '%s'", get_tok_str(v, NULL));
        }
    } else {
        if (tok != '{')
            expect("struct/union/enum name");
        v = anon_sym++;
    }
    /* `Record` the original enum/struct/union token.  */
    type1.t = u | ut;
    type1.ref = NULL;
    /* we put an undefined size for struct/union */
    s = sym_push(v | SYM_STRUCT, &type1, 0, bt ? 0 : -1);
    s->r = 0; /* default alignment is zero as gcc */
do_decl:
    type->t = s->type.t;
    type->ref = s;

    if (tok == '{') {
        next();
        if (s->c != -1
            && !(u == VT_ENUM && s->c == 0)) /* not yet defined typed enum */
            tcc_error("struct/union/enum already defined");
        s->c = -2;
        /* cannot be empty */
        /* non empty enums are not allowed */
        ps = &s->next;
        if (u == VT_ENUM) {
            long long ll = 0, pl = 0, nl = 0;
	    CType t;
            t.ref = s;
            s->sym_scope = local_scope; /* anonymous symbol won't have set */
            /* enum symbols have static storage */
            t.t = VT_INT|VT_STORAGE_STATIC|VT_ENUM_VAL;
            if (bt)
                t.t = bt|VT_STORAGE_STATIC|VT_ENUM_VAL;
            for(;;) {
                v = tok;
                if (v < TOK_UIDENT)
                    expect("identifier");
                next();
                if (tok == '=') {
                    next();
		    ll = expr_const64();
                }
                if (bt && !in_range(ll, t.t))
		    tcc_error("enumerator '%s' out of range of its type",
			get_tok_str(v, NULL));
                if (tcc_state->lang_d) {
                    ss = sym_calloc();
                    ss->v = v;
                    ss->type = t;
                    ss->enum_val = ll;
                    *ps = ss;
                    ps = &ss->next;
                } else {
                    ss = sym_push(v, &t, VT_CONST, 0);
                    ss->enum_val = ll;
                    *ps = ss, ps = &ss->next;
                }

                if (ll < nl)
                    nl = ll;
                if (ll > pl)
                    pl = ll;
                if (tok != ',')
                    break;
                next();
                ll++;
                /* `NOTE`: we accept a trailing comma */
                if (tok == '}')
                    break;
            }
            skip('}');

            if (bt) {
                t.t = bt;
                s->c = 2;
                goto enum_done;
            }

            /* set integral type of the enum */
            t.t = VT_INT;
            if (nl >= 0) {
                if (pl != (unsigned)pl)
                    t.t = (LONG_SIZE==8 ? VT_LLONG|VT_LONG : VT_LLONG);
                t.t |= VT_UNSIGNED;
            } else if (pl != (int)pl || nl != (int)nl)
                t.t = (LONG_SIZE==8 ? VT_LLONG|VT_LONG : VT_LLONG);

            /* set type for enum members */
            for (ss = s->next; ss; ss = ss->next) {
                ll = ss->enum_val;
                if (ll == (int)ll) /* default is int if it fits */
                    continue;
                if (t.t & VT_UNSIGNED) {
                    ss->type.t |= VT_UNSIGNED;
                    if (ll == (unsigned)ll)
                        continue;
                }
                ss->type.t = (ss->type.t & ~VT_BTYPE)
                    | (LONG_SIZE==8 ? VT_LLONG|VT_LONG : VT_LLONG);
            }
            s->c = 1;
        enum_done:
            s->type.t = type->t = t.t | VT_ENUM;
            s->type.ref = s;

        } else {
            c = 0;
            flexible = 0;
            while (tok != '}') {
                if (check_d_static_assert())
                    continue;
                if (!parse_btype(&btype, &ad1, 0)) {
                    if (tok == TOK_STATIC_ASSERT) {
                        do_Static_assert();
                        continue;
                    }
		    skip(';');
		    continue;
		}
                /* D-style array suffixes apply to every member */
                while (tok == '[')
                    d_array_suffix(&btype, 0);
                while (1) {
		    if (flexible)
		        tcc_error("flexible array member '%s' not at the end of struct",
                              get_tok_str(v, NULL));
                    bit_size = -1;
                    v = 0;
                    type1 = btype;
                    if (tok != ':') {
			if (tok != ';')
                            type_decl(&type1, &ad1, &v, TYPE_DIRECT);
                        if (v == 0) {
                    	    if ((type1.t & VT_BTYPE) != VT_STRUCT)
                        	expect("identifier");
                    	    else {
				int v = btype.ref->v;
				if ((v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
				    if (tcc_state->ms_extensions == 0)
                        		expect("identifier");
				}
                    	    }
                        }
                        if (type_size(&type1, &align) < 0) {
			    if ((u == VT_STRUCT) && (type1.t & VT_ARRAY) && c)
			        flexible = 1;
			    else
			        tcc_error("field '%s' has incomplete type",
                                      get_tok_str(v, NULL));
                        }
                        if ((type1.t & VT_BTYPE) == VT_FUNC ||
			    (type1.t & VT_BTYPE) == VT_VOID ||
                            (type1.t & VT_STORAGE))
                            tcc_error("invalid type for '%s'",
                                  get_tok_str(v, NULL));
                    }
                    if (tok == ':') {
                        next();
                        bit_size = expr_const();
                        /* `XXX`: handle v = 0 case for messages */
                        if (bit_size < 0)
                            tcc_error("negative width in bit-field '%s'",
                                  get_tok_str(v, NULL));
                        if (v && bit_size == 0)
                            tcc_error("zero width for bit-field '%s'",
                                  get_tok_str(v, NULL));
			parse_attribute(&ad1);
                    }
                    size = type_size(&type1, &align);
                    if (bit_size >= 0) {
                        bt = type1.t & VT_BTYPE;
                        if (bt != VT_INT &&
                            bt != VT_BYTE &&
                            bt != VT_SHORT &&
                            bt != VT_BOOL &&
                            bt != VT_LLONG)
                            tcc_error("bitfields must have scalar type");
                        bsize = size * 8;
                        if (bit_size > bsize) {
                            tcc_error("width of '%s' exceeds its type",
                                  get_tok_str(v, NULL));
                        } else if (bit_size == bsize
				    && !*tcc_state->pack_stack_ptr
                                    && !ad.a.packed && !ad1.a.packed) {
                            /* no need for bit fields */
                            ;
                        } else if (bit_size == 64) {
                            tcc_error("field width 64 not implemented");
                        } else {
                            type1.t = (type1.t & ~VT_STRUCT_MASK)
                                | VT_BITFIELD
                                | ((unsigned)bit_size << (VT_STRUCT_SHIFT + 6));
                        }
                    }
                    if (v != 0 || (type1.t & VT_BTYPE) == VT_STRUCT) {
                        /* `Remember` we've seen a real field to check
			   for placement of flexible array member. */
			c = 1;
                    }
		    /* `If` member is a struct or bit-field, enforce
		       placing into the struct (as anonymous).  */
                    if (v == 0 &&
			((type1.t & VT_BTYPE) == VT_STRUCT ||
			 bit_size >= 0)) {
		        v = anon_sym++;
		    }
                    if (v) {
                        ss = sym_push(v | SYM_FIELD, &type1, 0, 0);
                        ss->a = ad1.a;
                        *ps = ss;
                        ps = &ss->next;
                    }
                    if (tok == ';' || tok == TOK_EOF)
                        break;
                    skip(',');
                }
                skip(';');
            }
            skip('}');
	    parse_attribute(&ad);
            if (ad.cleanup_func) {
                tcc_warning("attribute '__cleanup__' ignored on type");
            }
	    check_fields(type, 1);
	    check_fields(type, 0);
            struct_layout(type, &ad);
        }
        if (debug_modes)
            tcc_debug_fix_forw(tcc_state, type);
    }
}

static void sym_to_attr(AttrDef *ad, Sym *s)
{
    merge_symattr(&ad->a, &s->a);
    merge_funcattr(&ad->f, &s->f);
}

/* `Add` type qualifiers to a type. `If` the type is an array then the qualifiers
   are added to the element type, copied because it could be a typedef. */
static void parse_btype_qualify(CType *type, int qualifiers)
{
    while (type->t & VT_ARRAY) {
        type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        type = &type->ref->type;
    }
    if (qualifiers & VT_QUALIFIER_RESTRICT)
        check_restrict_type(type);
    type->t |= qualifiers;
}

/* return 0 if no type declaration. otherwise, return the basic type
   and skip it.
 */
static int parse_btype(CType *type, AttrDef *ad, int ignore_label)
{
    int t, u, bt, st, type_found, typespec_found, g, n, is_complex_or_imaginary = 0;
    Sym *s;
    CType type1;
    memset(ad, 0, sizeof(AttrDef));
    type_found = 0;
    typespec_found = 0;
    t = VT_INT;
    bt = st = -1;
    type->ref = NULL;
    while(1) {
        /* `$tupleof(T)`: the member types of a struct/union as a tuple
           type.  Only meaningful in `-lang=d`.  */
        if (tcc_state->lang_d && tok >= TOK_UIDENT
            && !strcmp(get_tok_str(tok, &tokc), "$tupleof")
            && !typespec_found) {
            Sym *ts;
            next();         /* `$tupleof` */
            skip('(');
            if (!parse_btype(&type1, ad, 0))
                tcc_error("expected type");
            type_decl(&type1, ad, &n, TYPE_ABSTRACT);
            skip(')');
            if ((type1.t & VT_BTYPE) != VT_STRUCT)
                tcc_error("$tupleof requires a struct or union type");
            ts = tuple_begin();
            {
                Sym **plast = &ts->next;
                Sym *f;
                int idx = 0;
                for (f = type1.ref->next; f; f = f->next) {
                    tuple_add_field(&plast, &f->type, idx++);
                }

            }
            type->t = VT_STRUCT | (t & (VT_STORAGE | VT_QUALIFIER));
            type->ref = tuple_layout(ts);
            return 1;
        }
        /* `Type` tuple `(T1, ..., Tn)`.  `parse_tuple_type_opt()` leaves
           the token stream unchanged and returns `NULL` if the group is
           not a tuple type.  */
        if (tok == '(' && !typespec_found) {
            Sym *ts = parse_tuple_type_opt(type, ad);
            if (ts) {
                type->t = VT_STRUCT | (t & (VT_STORAGE | VT_QUALIFIER));
                type->ref = ts;
                return 1;
            }
        }
        switch(tok) {
        case TOK_EXTENSION:
            /* currently, we really ignore extension */
            next();
            continue;

            /* basic types */
        case TOK_CHAR:
            u = VT_BYTE;
        basic_type:
            next();
        basic_type1:
            if (u == VT_SHORT || u == VT_LONG) {
                if (st != -1 || (bt != -1 && bt != VT_INT))
                    tmbt: tcc_error("too many basic types");
                st = u;
            } else {
                if (bt != -1 || (st != -1 && u != VT_INT))
                    goto tmbt;
                if ((t & VT_DEFSIGN) && (u == VT_VOID || u > VT_LLONG))
                    goto tmbt;
                bt = u;
            }
            if (u != VT_INT)
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            typespec_found = 1;
            break;
        case TOK_VOID:
            u = VT_VOID;
            goto basic_type;
        case TOK_SHORT:
            u = VT_SHORT;
            goto basic_type;
        case TOK_INT:
            u = VT_INT;
            goto basic_type;
        case TOK_ALIGNAS:
            { int n;
              next();
              skip('(');
              AttrDef ad1 = {0};
              if (parse_btype(&type1, &ad1, 0)) {
                  type_decl(&type1, &ad1, &n, TYPE_ABSTRACT);
                  if (ad1.a.aligned)
                    n = 1 << (ad1.a.aligned - 1);
                  else
                    type_size(&type1, &n);
              } else {
                  n = expr_const();
                  if (n < 0 || (n & (n - 1)) != 0)
                    tcc_error("alignment must be a positive power of two");
              }
              skip(')');
              ad->a.aligned = exact_log2p1(n);
            }
            continue;
        case TOK_LONG:
            if ((t & VT_BTYPE) == VT_DOUBLE) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LLONG;
            } else {
                u = VT_LONG;
                goto basic_type;
            }
            next();
            break;
        case TOK_BOOL:
            u = VT_BOOL;
            goto basic_type;
        case TOK_COMPLEX:
            if (t & VT_COMPLEX)
                tcc_error("duplicate _Complex specifier");
            if (t & VT_IMAGINARY)
                tcc_error("both _Complex and _Imaginary specified");
            t |= VT_COMPLEX;
            is_complex_or_imaginary = 1;
            next();
            break;
        case TOK_IMAGINARY:
            if (t & VT_IMAGINARY)
                tcc_error("duplicate _Imaginary specifier");
            if (t & VT_COMPLEX)
                tcc_error("both _Complex and _Imaginary specified");
            t |= VT_IMAGINARY;
            is_complex_or_imaginary = 1;
            next();
            break;
        case TOK_D_CFLOAT:
            if (!tcc_state->lang_d) return 0;
            u = VT_FLOAT;
            t |= VT_COMPLEX;
            is_complex_or_imaginary = 1;
            goto basic_type;
        case TOK_D_CDOUBLE:
            if (!tcc_state->lang_d) return 0;
            u = VT_DOUBLE;
            t |= VT_COMPLEX;
            is_complex_or_imaginary = 1;
            goto basic_type;
        case TOK_D_CREAL:
            if (!tcc_state->lang_d) return 0;
            u = VT_LDOUBLE;
            t |= VT_COMPLEX;
            is_complex_or_imaginary = 1;
            goto basic_type;
        case TOK_D_IFLOAT:
            if (!tcc_state->lang_d) return 0;
            u = VT_FLOAT;
            t |= VT_IMAGINARY;
            is_complex_or_imaginary = 1;
            goto basic_type;
        case TOK_D_IDOUBLE:
            if (!tcc_state->lang_d) return 0;
            u = VT_DOUBLE;
            t |= VT_IMAGINARY;
            is_complex_or_imaginary = 1;
            goto basic_type;
        case TOK_D_IREAL:
            if (!tcc_state->lang_d) return 0;
            u = VT_LDOUBLE;
            t |= VT_IMAGINARY;
            is_complex_or_imaginary = 1;
            goto basic_type;
        case TOK_FLOAT:
            u = VT_FLOAT;
            goto basic_type;
        case TOK_DOUBLE:
            if ((t & (VT_BTYPE|VT_LONG)) == VT_LONG) {
                t = (t & ~(VT_BTYPE|VT_LONG)) | VT_LDOUBLE;
            } else {
                u = VT_DOUBLE;
                goto basic_type;
            }
            next();
            break;
        case TOK_ENUM:
            struct_decl(&type1, VT_ENUM);
        basic_type2:
            u = type1.t;
            type->ref = type1.ref;
            goto basic_type1;
        case TOK_STRUCT:
            struct_decl(&type1, VT_STRUCT);
            goto basic_type2;
        case TOK_UNION:
            struct_decl(&type1, VT_UNION);
            goto basic_type2;

            /* type modifiers */
        case TOK__Atomic:
            next();
            type->t = t;
            parse_btype_qualify(type, VT_ATOMIC);
            t = type->t;
            if (tok == '(') {
                parse_expr_type(&type1);
                /* remove all storage modifiers except typedef */
                type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
                if (type1.ref)
                    sym_to_attr(ad, type1.ref);
                goto basic_type2;
            }
            break;
        case TOK_CONST1:
        case TOK_CONST2:
        case TOK_CONST3:
            type->t = t;
            parse_btype_qualify(type, VT_QUALIFIER_CONSTANT);
            t = type->t;
            next();
            break;
        case TOK_IMMUTABLE:
            /* D-style immutable qualifier.  `It` implies 'const' but unlike
               plain 'const' it is transitive: members of an immutable
               aggregate, and the objects referenced by an immutable
               pointer, are immutable as well.  */
            type->t = t;
            parse_btype_qualify(type, VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
            t = type->t;
            next();
            if (tok == '(') {
                parse_expr_type(&type1);
                type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
                if (type1.ref)
                    sym_to_attr(ad, type1.ref);
                type1.t |= type->t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
                type->t = type1.t;
                type->ref = type1.ref;
                goto basic_type2;
            }
            break;
        case TOK_VOLATILE1:
        case TOK_VOLATILE2:
        case TOK_VOLATILE3:
            type->t = t;
            parse_btype_qualify(type, VT_QUALIFIER_VOLATILE);
            t = type->t;
            next();
            break;
        case TOK_SIGNED1:
        case TOK_SIGNED2:
        case TOK_SIGNED3:
            if ((t & (VT_DEFSIGN|VT_UNSIGNED)) == (VT_DEFSIGN|VT_UNSIGNED))
                tcc_error("signed and unsigned modifier");
            t |= VT_DEFSIGN;
            next();
            typespec_found = 1;
            break;
        case TOK_RESTRICT1:
        case TOK_RESTRICT2:
        case TOK_RESTRICT3:
            t |= VT_QUALIFIER_RESTRICT;
            next();
            break;
        case TOK_REGISTER:
            next();
            break;
        case TOK_AUTO:
            u = VT_AUTO;
            goto basic_type;
        case TOK_UNSIGNED:
            if ((t & (VT_DEFSIGN|VT_UNSIGNED)) == VT_DEFSIGN)
                tcc_error("signed and unsigned modifier");
            t |= VT_DEFSIGN | VT_UNSIGNED;
            next();
            typespec_found = 1;
            break;

            /* storage */
        case TOK_EXTERN:
            g = VT_STORAGE_EXTERN;
            goto storage;
        case TOK_STATIC:
            g = VT_STORAGE_STATIC;
            goto storage;
        case TOK_TYPEDEF:
            g = VT_STORAGE_TYPEDEF;
            goto storage;
       storage:
            if (t & (VT_STORAGE_EXTERN|VT_STORAGE_STATIC|VT_STORAGE_TYPEDEF) & ~g)
                tcc_error("multiple storage classes");
            t |= g;
            next();
            break;
        case TOK_INLINE1:
        case TOK_INLINE2:
        case TOK_INLINE3:
            t |= VT_STORAGE_INLINE;
            next();
            break;
        case TOK_NORETURN3:
            next();
            ad->f.func_noreturn = 1;
            break;
        case '@':
            {
                const uint8_t *saved_ptr = file->buf_ptr;
                next();
                if (tok == TOK_IMMUTABLE || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "immutable"))) {
                    type->t = t;
                    parse_btype_qualify(type, VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
                    t = type->t;
                    next();
                    if (tok == '(') {
                        parse_expr_type(&type1);
                        type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
                        if (type1.ref)
                            sym_to_attr(ad, type1.ref);
                        type1.t |= type->t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
                        type->t = type1.t;
                        type->ref = type1.ref;
                        goto basic_type2;
                    }
                    break;
                } else if (tok == TOK_CONST1 || tok == TOK_CONST2 || tok == TOK_CONST3 || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "const"))) {
                    type->t = t;
                    parse_btype_qualify(type, VT_QUALIFIER_CONSTANT);
                    t = type->t;
                    next();
                    if (tok == '(') {
                        parse_expr_type(&type1);
                        type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
                        if (type1.ref)
                            sym_to_attr(ad, type1.ref);
                        type1.t |= type->t & VT_QUALIFIER_CONSTANT;
                        type->t = type1.t;
                        type->ref = type1.ref;
                        goto basic_type2;
                    }
                    break;
                } else if (saved_ptr) {
                    file->buf_ptr = (uint8_t *)saved_ptr;
                    next();
                }
            }
            {
                if (tok == TOK_STATIC || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "static"))) {
                    ad->section = rodata_section;
                    next();
                } else if (tok == TOK_PURE1 || tok == TOK_PURE2 || (tok >= TOK_IDENT && !strcmp(get_tok_str(tok, &tokc), "pure"))) {
                    ad->f.func_pure = FUNC_PURITY_TRUE;
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
                    int t_tok = tok;
                    if (t_tok >= TOK_IDENT) {
                        tcc_error("unknown attribute '@%s'", get_tok_str(t_tok, &tokc));
                    } else {
                        tcc_error("expected attribute name after '@'");
                    }
                }
            }
            continue;
        case TOK_ATTRIBUTE1:
        case TOK_ATTRIBUTE2:
            parse_attribute(ad);
            if (ad->attr_mode) {
                u = ad->attr_mode -1;
                t = (t & ~(VT_BTYPE|VT_LONG)) | u;
            }
            continue;
            /* `GNUC` typeof */
        case TOK_TYPEOF1:
        case TOK_TYPEOF2:
        case TOK_TYPEOF3:
            next();
            parse_expr_type(&type1);
            /* remove all storage modifiers except typedef */
            type1.t &= ~(VT_STORAGE&~VT_STORAGE_TYPEDEF);
            if (type1.ref) {
                sym_to_attr(ad, type1.ref);
                if (type1.t & VT_ARRAY)
                    type1.t |= VT_BT_ARRAY;
            }
            goto basic_type2;
        case TOK_THREAD_LOCAL:
        case TOK___thread:
            if (t & VT_STORAGE_TLS)
                tcc_error("multiple thread-local storage specifiers");
            t |= VT_STORAGE_TLS;
            next();
            break;
        default:
            if (tcc_state->lang_d && tok >= TOK_IDENT) {
                const char *sstr = get_tok_str(tok, &tokc);
                if (!strcmp(sstr, "func")) {
                    ad->f.func_pure = FUNC_PURITY_TRUE;
                    next();
                    typespec_found = 1;
                    break;
                } else if (!strcmp(sstr, "proc")) {
                    next();
                    typespec_found = 1;
                    break;
                }
            }
            if (typespec_found)
                goto the_end;
            s = sym_find(tok);
            if (!s || !(s->type.t & VT_STORAGE_TYPEDEF))
                goto the_end;
            s->a.used = 1;
            n = tok, next();
            if (tok == ':' && ignore_label) {
                /* ignore if it's a label */
                unget_tok(n);
                goto the_end;
            }

            t &= ~(VT_BTYPE|VT_LONG);
            u = t & ~VT_QUALIFIER, t ^= u;
            type->t = (s->type.t & ~VT_STORAGE_TYPEDEF) | u;
            type->ref = s->type.ref;
            if (t)
                parse_btype_qualify(type, t);
            t = type->t;
            if (t & VT_ARRAY)
                t |= VT_BT_ARRAY;
            /* get attributes from typedef */
            sym_to_attr(ad, s);
            typespec_found = 1;
            st = bt = -2;
            break;
        }
        type_found = 1;
    }
the_end:
    if (is_complex_or_imaginary) {
        int bt_val = t & VT_BTYPE;
        if (bt == -1) {
            t = (t & ~VT_BTYPE) | VT_DOUBLE;
            bt_val = VT_DOUBLE;
        }
        if (bt_val != VT_FLOAT && bt_val != VT_DOUBLE && bt_val != VT_LDOUBLE) {
            tcc_error("complex/imaginary type must be floating point");
        }
    }
    if (tcc_state->char_is_unsigned) {
        if ((t & (VT_DEFSIGN|VT_BTYPE)) == VT_BYTE)
            t |= VT_UNSIGNED;
    }
    /* `VT_LONG` is used just as a modifier for `VT_INT` / `VT_LLONG` */
    bt = t & (VT_BTYPE|VT_LONG);
    if (bt == VT_LONG)
        t |= LONG_SIZE == 8 ? VT_LLONG : VT_INT;
#ifdef TCC_USING_DOUBLE_FOR_LDOUBLE
    if (bt == VT_LDOUBLE)
        t = (t & ~(VT_BTYPE|VT_LONG)) | (VT_DOUBLE|VT_LONG);
#endif
    type->t = t;
    check_restrict_type(type);
    return type_found;
}

/* convert a function parameter type (array to pointer and function to
   function pointer) */
static inline void convert_parameter_type(CType *pt)
{
    /* array must be transformed to pointer according to `ANSI` C */
    pt->t &= ~(VT_ARRAY | VT_VLA);
    if ((pt->t & VT_BTYPE) == VT_FUNC) {
        mk_pointer(pt);
    }
}

/* apply the conversions required for an expression value */
static inline void convert_expression_type(CType *pt)
{
    pt->t &= ~VT_QUALIFIER;
    convert_parameter_type(pt);
}

ST_FUNC CString* parse_asm_str(void)
{
    skip('(');
    return parse_mult_str("string constant");
}

/* `Parse` an asm label and return the token */
static int asm_label_instr(void)
{
    int v;
    char *astr;

    next();
    astr = parse_asm_str()->data;
    skip(')');
#ifdef ASM_DEBUG
    printf("asm_alias: \"%s\"\n", astr);
#endif
    v = tok_alloc_const(astr);
    return v;
}

#include "tccgen-tuple.c"
