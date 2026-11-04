#define EXPR_CONST 1
#define EXPR_ANY   2

static void parse_init_elem(int expr_type)
{
    int saved_global_expr;
    switch(expr_type) {
    case EXPR_CONST:
        /* compound literals must be allocated globally in this case */
        saved_global_expr = global_expr;
        global_expr = 1;
        expr_const1();
        global_expr = saved_global_expr;
        /* `NOTE`: symbols are accepted, as well as lvalue for anon symbols
	   (compound literals).  */
        if (((vtop->r & (VT_VALMASK | VT_LVAL)) != VT_CONST
             && ((vtop->r & (VT_SYM|VT_LVAL)) != (VT_SYM|VT_LVAL)
                 || vtop->sym->v < SYM_FIRST_ANOM))
#ifdef TCC_TARGET_PE
                 || ((vtop->r & VT_SYM) && vtop->sym->a.dllimport)
#endif
           )
            tcc_error("initializer element is not constant");
        break;
    case EXPR_ANY:
        expr_eq();
        break;
    }
}

#if 1
static void init_assert(init_params *p, int offset)
{
    if (p->sec ? !NODATA_WANTED && offset > p->sec->data_offset
               : !nocode_wanted && offset > p->local_offset)
        tcc_internal_error("initializer overflow");
}
#else
#define init_assert(sec, offset)
#endif

/* put zeros for variable based init */
static void init_putz(init_params *p, unsigned long c, int size)
{
    init_assert(p, c + size);
    if (p->sec) {
        /* nothing to do because globals are already set to zero */
    } else {
        vpush_helper_func(TOK_memset);
        vseti(VT_LOCAL, c);
        vpushi(0);
        vpushs(size);
#if defined TCC_TARGET_ARM && defined TCC_ARM_EABI
        vswap();  /* using `__aeabi_memset`(void*, `size_t`, int) */
#endif
        gfunc_call(3);
    }
}

#define DIF_FIRST     1
#define DIF_SIZE_ONLY 2
#define DIF_HAVE_ELEM 4
#define DIF_CLEAR     8

/* delete relocations for specified range c ... c + size. `Unfortunatly`
   in very special cases, relocations may occur unordered */
static void decl_design_delrels(Section *sec, int c, int size)
{
    ElfW_Rel *rel, *rel2, *rel_end;
    if (!sec || !sec->reloc)
        return;
    rel = rel2 = (ElfW_Rel*)sec->reloc->data;
    rel_end = (ElfW_Rel*)(sec->reloc->data + sec->reloc->data_offset);
    while (rel < rel_end) {
        if (rel->r_offset >= c && rel->r_offset < c + size) {
            sec->reloc->data_offset -= sizeof *rel;
        } else {
            if (rel2 != rel)
                memcpy(rel2, rel, sizeof *rel);
            ++rel2;
        }
        ++rel;
    }
}

static void decl_design_flex(init_params *p, Sym *ref, int index)
{
    if (ref == p->flex_array_ref) {
        if (index >= ref->c)
            ref->c = index + 1;
    } else if (ref->c < 0 && index >= 0)
        tcc_error("flexible array has zero size in this context");
}

/* t is the array or struct type. c is the array or struct
   address. `cur_field` is the pointer to the current
   field, for arrays the 'c' member contains the current start
   index.  'flags' is as in `decl_initializer`.
   'al' contains the already initialized length of the
   current container (starting at c).  `This` returns the new length of that.  */
static int decl_designator(init_params *p, CType *type, unsigned long c,
                           Sym **cur_field, int flags, int al)
{
    Sym *s, *f;
    int index, index_last, align, l, nb_elems, elem_size;
    unsigned long corig = c;

    elem_size = 0;
    nb_elems = 1;

    if (flags & DIF_HAVE_ELEM)
        goto no_designator;

    if (gnu_ext && tok >= TOK_UIDENT) {
        l = tok, next();
        if (tok == ':')
            goto struct_field;
        unget_tok(l);
    }

    /* `NOTE`: we only support ranges for last designator */
    while (nb_elems == 1 && (tok == '[' || tok == '.')) {
        if (tok == '[') {
            if (!(type->t & VT_ARRAY))
                expect("array type");
            next();
            index = index_last = expr_const();
            if (tok == TOK_DOTS && gnu_ext) {
                next();
                index_last = expr_const();
            }
            skip(']');
            s = type->ref;
            decl_design_flex(p, s, index_last);
            if (index < 0 || index_last >= s->c || index_last < index)
	        tcc_error("index exceeds array bounds or range is empty");
            if (cur_field)
		(*cur_field)->c = index_last;
            type = pointed_type(type);
            elem_size = type_size(type, &align);
            c += index * elem_size;
            nb_elems = index_last - index + 1;
        } else {
            int cumofs;
            next();
            l = tok;
        struct_field:
            next();
	    f = find_field(type, l, &cumofs);
            if (cur_field)
                *cur_field = f;
	    type = &f->type;
            c += cumofs;
        }
        cur_field = NULL;
    }
    if (!cur_field) {
        if (tok == '=') {
            next();
        } else if (!gnu_ext) {
	    expect("=");
        }
    } else {
    no_designator:
        if (type->t & VT_ARRAY) {
	    index = (*cur_field)->c;
            s = type->ref;
            decl_design_flex(p, s, index);
            if (index >= s->c)
                tcc_error("too many initializers");
            type = pointed_type(type);
            elem_size = type_size(type, &align);
            c += index * elem_size;
        } else {
            f = *cur_field;
	    /* `Skip` bitfield padding. `Also` with size 32 and 64. */
	    while (f && (f->v & SYM_FIRST_ANOM) &&
		   is_integer_btype(f->type.t & VT_BTYPE))
	        *cur_field = f = f->next;
            if (!f)
                tcc_error("too many initializers");
	    type = &f->type;
            c += f->c;
        }
    }

    if (!elem_size) /* for structs */
        elem_size = type_size(type, &align);

    /* `Using` designators the same element can be initialized more
       than once.  `In` that case we need to delete possibly already
       existing relocations. */
    if (!(flags & DIF_SIZE_ONLY) && c - corig < al) {
        decl_design_delrels(p->sec, c, elem_size * nb_elems);
        flags &= ~DIF_CLEAR; /* mark stack dirty too */
    }

    decl_initializer(p, type, c, flags & ~DIF_FIRST);

    if (!(flags & DIF_SIZE_ONLY) && nb_elems > 1) {
        Sym aref = {0};
        CType t1;
        int i;
        if (p->sec || (type->t & VT_ARRAY)) {
            /* make `init_putv`/vstore believe it were a struct */
            aref.c = elem_size;
            t1.t = VT_STRUCT, t1.ref = &aref;
            type = &t1;
        }
        if (p->sec)
            vpush_ref(type, p->sec, c, elem_size);
        else
	    vset(type, VT_LOCAL|VT_LVAL, c);
        for (i = 1; i < nb_elems; i++) {
            vdup();
            init_putv(p, type, c + elem_size * i);
	}
        vpop();
    }

    c += nb_elems * elem_size;
    if (c - corig > al)
      al = c - corig;
    return al;
}

static void write_ldouble(unsigned char *d, void *s)
{
    //printf("long double %Lf\n", *(long double*)s);
#ifdef TCC_CROSS_TEST
    if (LDOUBLE_SIZE >= 10) {
        double b = *(long double*)s;
        s = &b;
#else
    if (sizeof (long double) == 8 && LDOUBLE_SIZE >= 10) {
#endif
        /* our 'long double' is a double really (`_WIN32`, `__APPLE__`) */
        uint64_t m = *(uint64_t*)s;
        int e = m >> 48;
        int f = e >> 4 & 0x7FF;
        m <<= 11;
        if (0 == f) {
            if (0 == m)
                goto set;
            for (f = 1; !(m & 1ULL<<63); --f)
                m <<= 1;
        }
        if (f == 0x7ff)
            f = 0x43FF;
        e = (e & 0x8000) | (f + 0x3C00);
        m |= 1ULL<<63;
    set:
    #if (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* double -> extended */
        write64le(d, m);
        write16le(d+8, e);
    #elif LDOUBLE_SIZE == 16
        /* double -> quad */
        write64le(d+6, m << 1);
        write16le(d+14, e);
    #endif
        ;
    } else {
    #if LDOUBLE_SIZE == 8
        /* long double -> double */
        double b = *(long double*)s;
        memcpy(d, &b, 8);
    #elif (__i386__ || __x86_64__) && (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* extended -> extended */
        memcpy(d, s, 10);
    #elif (__i386__ || __x86_64__) && (defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64)
        /* extended -> quad */
        uint64_t m = *(uint64_t*)s;
        int e = *(uint16_t*)((char*)s + 8);
        write64le(d+6, m << 1);
        write16le(d+14, e);
    #elif (__aarch64__ || __riscv) && (defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64)
        /* quad -> extended */
        uint64_t m = read64le((char*)s + 6);
        int e = read16le((char*)s + 14);
        (e & 0x7fff) && (m & 1) && 0 == ++m && ++e;
        write64le(d, m >> 1 | ((e & 0x7fff) ? 1ULL<<63 : 0));
        write16le(d+8, e);
    #else
        /* unknown */
        if (sizeof (long double) == LDOUBLE_SIZE)
            memcpy(d, s, LDOUBLE_SIZE);
    #endif
    }
}

/* store a value or an expression directly in global data or in local array */
static void init_putv(init_params *p, CType *type, unsigned long c)
{
    int bt;
    void *ptr;
    CType dtype;
    int size, align;
    Section *sec = p->sec;
    uint64_t val;

    if (type->t & VT_COMPLEX) {
        CType base_type = *type;
        base_type.t &= ~VT_COMPLEX;
        int base_align, base_size = type_size(&base_type, &base_align);

        int align2, size2 = type_size(type, &align2);
        int r2, loc = get_temp_local_var(size2, align2, &r2);
        vset(type, VT_LOCAL | VT_LVAL, loc); vtop->r2 = r2;
        vswap(); vstore();

        vset(type, VT_LOCAL | VT_LVAL, loc); vtop->r2 = r2; gen_complex_part(0);
        init_putv(p, &base_type, c);

        vset(type, VT_LOCAL | VT_LVAL, loc); vtop->r2 = r2; gen_complex_part(1);
        init_putv(p, &base_type, c + base_size);
        vpop(); /* discard the temporary holding the complex value */
        return;
    }
    dtype = *type;
    /* need to do that to avoid false warning */
    dtype.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);

    size = type_size(type, &align);
    if (type->t & VT_BITFIELD)
        size = (BIT_POS(type->t) + BIT_SIZE(type->t) + 7) / 8;
    init_assert(p, c + size);

    if (sec) {
        /* `XXX`: not portable */
        /* `XXX`: generate error if incorrect relocation */
        gen_assign_cast(&dtype);
        bt = type->t & VT_BTYPE;

        if ((vtop->r & VT_SYM)
            && bt != VT_PTR
            && (bt != (PTR_SIZE == 8 ? VT_LLONG : VT_INT)
                || (type->t & VT_BITFIELD))
            && !((vtop->r & VT_CONST) && vtop->sym->v >= SYM_FIRST_ANOM)
            )
            tcc_error("initializer element is not computable at load time");

        if (NODATA_WANTED) {
            vtop--;
            return;
        }

        ptr = sec->data + c;
        val = vtop->c.i;

	if ((vtop->r & (VT_SYM|VT_CONST)) == (VT_SYM|VT_CONST)
            && vtop->sym->v >= SYM_FIRST_ANOM
            && ((vtop->r & VT_LVAL) /* compound literal */
                || bt == VT_STRUCT /* designator */
                || (type->t & VT_ARRAY) /* string/compound literal */
                )) {
	    /* memcpy stuff over.  */
	    Section *ssec;
	    ElfSym *esym;
	    ElfW_Rel *rel;
	    esym = elfsym(vtop->sym);
	    ssec = tcc_state->sections[esym->st_shndx];
	    memmove (ptr, ssec->data + esym->st_value + (int)vtop->c.i, size);
	    if (ssec->reloc) {
		/* `We` need to copy over all memory contents, and that
		   includes relocations.  `Use` the fact that relocs are
		   created it order, so look from the end of relocs
		   until we hit one before the copied region.  */
                unsigned long relofs = ssec->reloc->data_offset;
		while (relofs >= sizeof(*rel)) {
                    relofs -= sizeof(*rel);
                    rel = (ElfW_Rel*)(ssec->reloc->data + relofs);
		    if (rel->r_offset >= esym->st_value + size)
		      continue;
		    if (rel->r_offset < esym->st_value)
		      break;
		    put_elf_reloca(symtab_section, sec,
				   c + rel->r_offset - esym->st_value,
				   ELFW(R_TYPE)(rel->r_info),
				   ELFW(R_SYM)(rel->r_info),
#if PTR_SIZE == 8
				   rel->r_addend
#else
				   0
#endif
				  );
		}
	    }
	} else {
            if (type->t & VT_BITFIELD) {
                int bit_pos, bit_size, bits, n;
                unsigned char *p, v, m;
                bit_pos = BIT_POS(vtop->type.t);
                bit_size = BIT_SIZE(vtop->type.t);
                p = (unsigned char*)ptr + (bit_pos >> 3);
                bit_pos &= 7, bits = 0;
                while (bit_size) {
                    n = 8 - bit_pos;
                    if (n > bit_size)
                        n = bit_size;
                    v = val >> bits << bit_pos;
                    m = ((1 << n) - 1) << bit_pos;
                    *p = (*p & ~m) | (v & m);
                    bits += n, bit_size -= n, bit_pos = 0, ++p;
                }
            } else
            switch(bt) {
	    case VT_BOOL:
		*(char *)ptr = val != 0;
                break;
	    case VT_BYTE:
		*(char *)ptr = val;
		break;
	    case VT_SHORT:
                write16le(ptr, val);
		break;
	    case VT_FLOAT:
                write32le(ptr, val);
		break;
	    case VT_DOUBLE:
                write64le(ptr, val);
		break;
	    case VT_LDOUBLE:
                write_ldouble(ptr, &vtop->c.ld);
		break;

#if PTR_SIZE == 8
            /* `intptr_t` may need a reloc too, see tcctest.c:`relocation_test`() */
	    case VT_LLONG:
	    case VT_PTR:
	        if (vtop->r & VT_SYM)
	          greloca(sec, vtop->sym, c, R_DATA_PTR, val);
	        else
	          write64le(ptr, val);
	        break;
            case VT_INT:
                write32le(ptr, val);
                break;
#else
	    case VT_LLONG:
                write64le(ptr, val);
                break;
            case VT_PTR:
            case VT_INT:
	        if (vtop->r & VT_SYM)
	          greloc(sec, vtop->sym, c, R_DATA_PTR);
	        write32le(ptr, val);
	        break;
#endif
	    default:
                //tcc_internal_error("unexpected type");
                break;
	    }
	}
        vtop--;
    } else {
        vset(&dtype, VT_LOCAL|VT_LVAL, c);
        vswap();
        vstore();
        vpop();
    }
}

/* `Initialize` a slice object at 'c' from the array expression on top of
   the value stack.  */
static void init_slice_from_array(init_params *p, CType *type, unsigned long c)
{
    CType st = vtop->type;
    Sym *s = type->ref;
    CType *ptr_type = &s->next->next->type;
    int elem_size, align;

    if (!is_compatible_unqualified_types(&st.ref->type,
                                         pointed_type(ptr_type)))
        cast_error(&st, type);

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
    init_putv(p, &s->next->type, c);

    init_putv(p, ptr_type, c + PTR_SIZE);
}

/* 't' contains the type and storage info. 'c' is the offset of the
   object in section 'sec'. `If` 'sec' is `NULL`, it means stack based
   allocation. 'flags & `DIF_FIRST`' is true if array '{' must be read (multi
   dimension implicit array init handling). 'flags & `DIF_SIZE_ONLY`' is true if
   size only evaluation is wanted (only for arrays). */
static void decl_initializer(init_params *p, CType *type, unsigned long c, int flags)
{
    int len, n, no_oblock, i;
    int size1, align1;
    Sym *s, *f;
    Sym indexsym;
    CType *t1;

    /* generate line number info */
    if (debug_modes && !(flags & DIF_SIZE_ONLY) && !p->sec)
        tcc_debug_line(tcc_state), tcc_tcov_check_line (tcc_state, 1);

    if (!(flags & DIF_HAVE_ELEM) && tok != '{' &&
	/* `In` case of strings we have special handling for arrays, so
	   don't consume them as initializer value (which would commit them
	   to some anonymous symbol).  */
	tok != TOK_LSTR && tok != TOK_STR &&
	(!(flags & DIF_SIZE_ONLY)
            /* a struct may be initialized from a struct of same type, as in
                    struct {int x,y;} a = {1,2}, b = {3,4}, c[] = {a,b};
               `In` that case we need to parse the element in order to check
               it for compatibility below */
            || (type->t & VT_BTYPE) == VT_STRUCT)
        ) {
        int ncw_prev = nocode_wanted;
        if ((flags & DIF_SIZE_ONLY) && !p->sec)
            ++nocode_wanted;
        if (is_tuple_type(type))
            tuple_expect = type;
	parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
        nocode_wanted = ncw_prev;
        flags |= DIF_HAVE_ELEM;
        tuple_expect = NULL;
    }

    if (is_slice_type(type) && !(flags & DIF_HAVE_ELEM)
        && (tok == TOK_STR || tok == TOK_LSTR)) {
        /* A string literal initializes a slice.  `Adjacent` literals are
           concatenated, as in C array initializers.  */
        int ncw_prev = nocode_wanted;
        int size1 = (tok == TOK_STR ? 1 : sizeof(nwchar_t));
        cstr_reset(&initstr);
        while (tok == TOK_STR || tok == TOK_LSTR) {
            if ((tok == TOK_STR ? 1 : sizeof(nwchar_t)) != size1)
                tcc_error("unhandled string literal merging");
            if (initstr.size)
                initstr.size -= size1;
            cstr_cat(&initstr, tokc.str.data, tokc.str.size);
            next();
        }
        unget_tok(size1 == 1 ? TOK_STR : TOK_LSTR);
        tokc.str.size = initstr.size;
        tokc.str.data = initstr.data;
        if ((flags & DIF_SIZE_ONLY) && !p->sec)
            ++nocode_wanted;
        parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
        nocode_wanted = ncw_prev;
        if (flags & DIF_SIZE_ONLY)
            vpop();
        else if (vtop->type.t & (VT_ARRAY | VT_VLA))
            init_slice_from_array(p, type, c);
        else {
            /* `The` literal was only part of a larger expression that
               already evaluated to a slice.  */
            if (!is_compatible_unqualified_types(type, &vtop->type))
                cast_error(&vtop->type, type);
            vset(type, VT_LOCAL | VT_LVAL, c);
            vswap();
            vstore();
            vpop();
        }
        return;
    }

    if ((flags & DIF_HAVE_ELEM) && is_slice_type(type)) {
        if (vtop->type.t & (VT_ARRAY | VT_VLA)) {
            if (flags & DIF_SIZE_ONLY)
                vpop();
            else
                init_slice_from_array(p, type, c);
            return;
        }
        if (!is_compatible_unqualified_types(type, &vtop->type))
            cast_error(&vtop->type, type);
    }

    if (type->t & VT_ARRAY) {
        no_oblock = 1;
        if (((flags & DIF_FIRST) && tok != TOK_LSTR && tok != TOK_STR) ||
            tok == '{') {
            skip('{');
            no_oblock = 0;
        }

        s = type->ref;
        n = s->c;
        t1 = pointed_type(type);
        size1 = type_size(t1, &align1);

        /* only parse strings here if correct type (otherwise: handle
           them as ((w)char *) expressions */
        if ((tok == TOK_LSTR &&
#ifdef TCC_TARGET_PE
             (t1->t & VT_BTYPE) == VT_SHORT && (t1->t & VT_UNSIGNED)
#else
             (t1->t & VT_BTYPE) == VT_INT
#endif
            ) || (tok == TOK_STR && (t1->t & VT_BTYPE) == VT_BYTE)) {
	    len = 0;
            cstr_reset(&initstr);
            if (size1 != (tok == TOK_STR ? 1 : sizeof(nwchar_t)))
              tcc_error("unhandled string literal merging");
            while (tok == TOK_STR || tok == TOK_LSTR) {
                if (initstr.size)
                  initstr.size -= size1;
                if (tok == TOK_STR)
                  len += tokc.str.size;
                else
                  len += tokc.str.size / sizeof(nwchar_t);
                len--;
                cstr_cat(&initstr, tokc.str.data, tokc.str.size);
                next();
            }
            if (tok != ')' && tok != '}' && tok != ',' && tok != ';'
                && tok != TOK_EOF) {
                /* `Not` a lone literal but part of a bigger expression.  */
                unget_tok(size1 == 1 ? TOK_STR : TOK_LSTR);
                tokc.str.size = initstr.size;
                tokc.str.data = initstr.data;
                goto do_init_array;
            }

            decl_design_flex(p, s, len);
            if (!(flags & DIF_SIZE_ONLY)) {
                int nb = n, ch;
                if (len < nb)
                    nb = len;
                if (len > nb)
                  tcc_warning("initializer-string for array is too long");
                /* in order to go faster for common case (char
                   string in global variable, we handle it
                   specifically */
                if (p->sec && size1 == 1) {
                    init_assert(p, c + nb);
                    if (!NODATA_WANTED)
                      memcpy(p->sec->data + c, initstr.data, nb);
                } else {
                    for(i=0;i<n;i++) {
                        if (i >= nb) {
                          /* only add trailing zero if enough storage (no
                             warning in this case since it is standard) */
                          if (flags & DIF_CLEAR)
                            break;
                          if (n - i >= 4) {
                            init_putz(p, c + i * size1, (n - i) * size1);
                            break;
                          }
                          ch = 0;
                        } else if (size1 == 1)
                          ch = ((unsigned char *)initstr.data)[i];
                        else
                          ch = ((nwchar_t *)initstr.data)[i];
                        vpushi(ch);
                        init_putv(p, t1, c + i * size1);
                    }
                }
            }
            if (tok == ',' && !no_oblock) /* static const char s[] = { "123", }; */
                next();
        } else {

          do_init_array:
	    indexsym.c = 0;
	    f = &indexsym;

          do_init_list:
            /* zero memory once in advance */
            if (!(flags & (DIF_CLEAR | DIF_SIZE_ONLY))) {
                init_putz(p, c, n*size1);
                flags |= DIF_CLEAR;
            }

	    len = 0;
            /* `GNU` extension: if the initializer is empty for a flex array,
               it's size is zero.  `We` won't enter the loop, so set the size
               now.  */
            decl_design_flex(p, s, len - 1);
	    while (tok != '}' || (flags & DIF_HAVE_ELEM)) {
		len = decl_designator(p, type, c, &f, flags, len);
		flags &= ~DIF_HAVE_ELEM;
		if (type->t & VT_ARRAY) {
		    ++indexsym.c;
		    /* special test for multi dimensional arrays (may not
		       be strictly correct if designators are used at the
		       same time) */
		    if (no_oblock && len >= n*size1)
		        break;
		} else {
		    if (s->type.t == VT_UNION)
		        f = NULL;
		    else
		        f = f->next;
		    if (no_oblock && f == NULL)
		        break;
		}

		if (tok == '}')
		    break;
		skip(',');
	    }
        }
        if (!no_oblock)
            skip('}');

    } else if ((flags & DIF_HAVE_ELEM)
        /* `Use` `i_c_parameter_t`, to strip toplevel qualifiers.
           `The` source type might have `VT_QUALIFIER_CONSTANT` set, which is
           of course assignable to non-const elements.  */
            && is_compatible_unqualified_types(type, &vtop->type)) {
        goto one_elem;

    } else if ((type->t & VT_BTYPE) == VT_STRUCT) {
        /* `An` initializer expression was parsed but does not have the
           struct type, as in `(int, int) t = 1;`.  A braced list never
           reaches this point with DIF_HAVE_ELEM set.  */
        if (is_tuple_type(type) && (flags & DIF_HAVE_ELEM))
            cast_error(&vtop->type, type);
        no_oblock = 1;
        if ((flags & DIF_FIRST) || tok == '{') {
            skip('{');
            no_oblock = 0;
        }
        s = type->ref;
        f = s->next;
        n = s->c;
        size1 = 1;
	goto do_init_list;

    } else if (tok == '{') {
        if (flags & DIF_HAVE_ELEM)
          skip(';');
        next();
        decl_initializer(p, type, c, flags & ~DIF_HAVE_ELEM);
        skip('}');

    } else one_elem: if ((flags & DIF_SIZE_ONLY)) {
	/* `If` we supported only `ISO` C we wouldn't have to accept calling
	   this on anything than an array if `DIF_SIZE_ONLY` (and even then
	   only on the outermost level, so no recursion would be needed),
	   because initializing a flex array member isn't supported.
	   `But` `GNU` C supports it, so we need to recurse even into
	   subfields of structs and arrays when `DIF_SIZE_ONLY` is set.  */
        /* just skip expression */
        if (flags & DIF_HAVE_ELEM)
            vpop();
        else
            skip_or_save_block(NULL);

    } else {
	if (!(flags & DIF_HAVE_ELEM)) {
	    /* `This` should happen only when we haven't parsed
	       the init element above for fear of committing a
	       string constant to memory too early.  */
	    if (tok != TOK_STR && tok != TOK_LSTR)
	      expect("string constant");
	    parse_init_elem(!p->sec ? EXPR_ANY : EXPR_CONST);
	}
        if (!p->sec && (flags & DIF_CLEAR) /* container was already zero'd */
            && (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST
            && vtop->c.i == 0
            && btype_size(type->t & VT_BTYPE) /* not for fp constants */
            )
            vpop();
        else
            init_putv(p, type, c);
    }
}

/* parse an initializer for type 't' if '`has_init`' is non zero, and
   allocate space in local or global data space ('r' is either
   `VT_LOCAL` or `VT_CONST`). `If` 'v' is non zero, then an associated
   variable 'v' of scope 'scope' is declared before initializers
   are parsed. `If` 'v' is zero, then a reference to the new object
   is put in the value stack. `If` '`has_init`' is 2, a special parsing
   is done to handle string constants. */
static void decl_initializer_alloc(CType *type, AttrDef *ad, int r,
                                   int has_init, int v, int scope)
{
    int size, align, addr;
    TokenString *init_str = NULL;

    Section *sec;
    Sym *flexible_array;
    Sym *sym = NULL;
    int saved_nocode_wanted = nocode_wanted;
#ifdef CONFIG_TCC_BCHECK
    int bcheck = tcc_state->do_bounds_check && !NODATA_WANTED;
#endif
    init_params p = {0};

    if (scope == VT_CONST) {
        /* see if a global symbol was already defined */
        sym = sym_find(v);
        if (sym) {
            patch_storage(sym, ad, type);
            if (sym->a.final_var && has_init)
                sym->a.assigned = 1;
            /* we accept several definitions of the same global variable. */
            if (!has_init && sym->c && elfsym(sym)->st_shndx != SHN_UNDEF)
                return;
            type = &sym->type;
        }
    }

    /* `Always` allocate static or global variables */
    if (v && (r & VT_VALMASK) == VT_CONST)
        nocode_wanted |= DATA_ONLY_WANTED;

    flexible_array = NULL;
    size = type_size(type, &align);

    /* exactly one flexible array may be initialized, either the
       toplevel array or the last member of the toplevel struct */

    if (size < 0) {
        // error out except for top-level incomplete arrays
        // (arrays of incomplete types are handled in array parsing)
        if (!(type->t & VT_ARRAY))
            tcc_error("initialization of incomplete type");
        /* `If` the base type itself was an array type of unspecified
           size (like in 'typedef int arr[]; arr x = {1};') then
           we will overwrite the unknown size by the real one for
           this decl.  `We` need to unshare the ref symbol holding
           that size.  */
        if (IS_BT_ARRAY(type->t))
            type->ref = sym_push(SYM_FIELD, &type->ref->type, 0, type->ref->c);
        p.flex_array_ref = type->ref;

    } else if (has_init && (type->t & VT_BTYPE) == VT_STRUCT) {
        Sym *field = type->ref->next;
        if (field) {
            while (field->next)
                field = field->next;
            if (field->type.t & VT_ARRAY && field->type.ref->c < 0) {
                flexible_array = field;
                p.flex_array_ref = field->type.ref;
                size = -1;
            }
        }
    }

    if (size < 0) {
        /* `If` unknown size, do a dry-run 1st pass */
        if (!has_init)
            goto err_size;
        if (has_init == 2) {
            /* only get strings */
            init_str = tok_str_alloc();
            while (tok == TOK_STR || tok == TOK_LSTR) {
                tok_str_add_tok(init_str);
                next();
            }
            tok_str_add(init_str, TOK_EOF);
        } else
            skip_or_save_block(&init_str);
        unget_tok(0);

        /* compute size */
        begin_macro(init_str, 1);
        next();
        decl_initializer(&p, type, 0, DIF_FIRST | DIF_SIZE_ONLY);
        /* prepare second initializer parsing */
        macro_ptr = init_str->str;
        next();

        /* if still unknown size, error */
        size = type_size(type, &align);
        if (size < 0)
    err_size:
            tcc_error("unknown type size");

        /* `If` there's a flex member and it was used in the initializer
           adjust size.  */
        if (flexible_array && flexible_array->type.ref->c > 0)
            size += flexible_array->type.ref->c
                    * pointed_size(&flexible_array->type);
    }

    /* take into account specified alignment if bigger */
    if (ad->a.aligned) {
	int speca = 1 << (ad->a.aligned - 1);
        if (speca > align)
            align = speca;
    } else if (ad->a.packed) {
        align = 1;
    }

    if (!v && NODATA_WANTED)
        size = 0, align = 1;

    if ((r & VT_VALMASK) == VT_LOCAL) {
        sec = NULL;
#ifdef CONFIG_TCC_BCHECK
        if (bcheck && v) {
            /* add padding between stack variables for bound checking */
            loc -= align;
        }
#endif
        loc = (loc - size) & -align;
        addr = loc;
        p.local_offset = addr + size;
#ifdef CONFIG_TCC_BCHECK
        if (bcheck && v) {
            /* add padding between stack variables for bound checking */
            loc -= align;
        }
#endif
        if (v) {
            /* local variable */
#ifdef CONFIG_TCC_ASM
	    if (ad->asm_label) {
		int reg = asm_parse_regvar(ad->asm_label);
		if (reg >= 0)
		    r = (r & ~VT_VALMASK) | reg;
	    }
#endif
            sym = sym_push(v, type, r, addr);
	    if (ad->cleanup_func) {
		Sym *cls = sym_push2(&all_cleanups,
                    SYM_FIELD | ++cur_scope->cl.n, 0, 0);
		cls->cleanup_sym = sym;
		cls->cleanup_func = ad->cleanup_func;
		cls->next = cur_scope->cl.s;
		cur_scope->cl.s = cls;
	    }

            sym->a = ad->a;
            if (sym->a.final_var && has_init)
                sym->a.assigned = 1;
        } else {
            /* push local reference */
            vset(type, r, addr);
        }
    } else {
        CType *tp = type;
        int is_const;

        while ((tp->t & (VT_BTYPE|VT_ARRAY)) == (VT_PTR|VT_ARRAY))
            tp = &tp->ref->type;
        is_const = tp->t & VT_QUALIFIER_CONSTANT;

        /* allocate symbol in corresponding section */
        sec = ad->section;
        if (!sec) {
            if (type->t & VT_STORAGE_TLS) {
                sec = find_section(tcc_state, has_init ? ".tdata" : ".tbss");
                sec->sh_flags = SHF_ALLOC | SHF_WRITE | SHF_TLS;
                sec->sh_type = has_init ? SHT_PROGBITS : SHT_NOBITS;
            } else if (is_const) {
		sec = rodata_section;
            } else if (has_init) {
		sec = data_section;
                /*if (`g_debug` & 4)
                    `tcc_warning`("rw data: %s", `get_tok_str`(v, 0));*/
            } else if (tcc_state->nocommon)
                sec = bss_section;
        } else if (ad->new_section) {
            /* `XXX`: `DWIM` logic: set section flags according to first usage */
            if (!is_const) {
                sec->sh_flags |= SHF_WRITE;
                if (!has_init)
                    sec->sh_type = SHT_NOBITS;
            }
        }

#ifdef CONFIG_TCC_BCHECK
        if (bcheck)
            size = size + 1; /* add padding */
#endif
        if (sec) {
	    addr = section_add(sec, size, align);
        } else {
            addr = align; /* `SHN_COMMON` is special, symbol value is align */
	    sec = common_section;
        }

        if (v) {
            if (!sym) {
                sym = sym_push(v, type, r | VT_SYM, 0);
                patch_storage(sym, ad, NULL);
                if (sym->a.final_var && has_init)
                    sym->a.assigned = 1;
            }
            /* update symbol definition */
	    put_extern_sym(sym, sec, addr, size);
        } else {
            /* push global reference */
            vpush_ref(type, sec, addr, size);
            sym = vtop->sym;
	    vtop->r |= r;
        }

#ifdef CONFIG_TCC_BCHECK
        /* handles bounds now because the symbol must be defined
           before for the relocation */
        if (bcheck) {
            addr_t *bounds_ptr;

            greloca(bounds_section, sym, bounds_section->data_offset, R_DATA_PTR, 0);
            /* then add global bound info */
            bounds_ptr = section_ptr_add(bounds_section, 2 * sizeof(addr_t));
            bounds_ptr[0] = 0; /* relocated */
            bounds_ptr[1] = size - 1;
        }
#endif
    }

    if (type->t & VT_VLA) {
        int a;

        if (has_init)
            tcc_error("variable length array cannot be initialized");

        if (NODATA_WANTED)
            goto no_alloc;

        /* save before-`VLA` stack pointer if needed */
        if (cur_scope->vla.num == 0) {
            if (cur_scope->prev && cur_scope->prev->vla.num) {
                cur_scope->vla.locorig = cur_scope->prev->vla.loc;
            } else {
                gen_vla_sp_save(loc -= PTR_SIZE);
                cur_scope->vla.locorig = loc;
            }
        }

        vpush_type_size(type, &a);
        gen_vla_alloc(type, a);
#if defined TCC_TARGET_PE && defined TCC_TARGET_X86_64
        /* on `_WIN64`, because of the function args scratch area, the
           result of alloca differs from `RSP` and is returned in `RAX`.  */
        gen_vla_result(addr), addr = (loc -= PTR_SIZE);
#endif
        gen_vla_sp_save(addr);
        cur_scope->vla.loc = addr;
        cur_scope->vla.num++;
    } else if (has_init) {
        p.sec = sec;
        decl_initializer(&p, type, addr, DIF_FIRST);
        /* patch flexible array member size back to -1, */
        /* for possible subsequent similar declarations */
        if (flexible_array)
            flexible_array->type.ref->c = -1;
    }

 no_alloc:
    /* restore parse state if needed */
    if (init_str) {
        end_macro();
        next();
    }

    nocode_wanted = saved_nocode_wanted;
}

/* generate vla code saved in `post_type`() */
static void func_vla_arg_code(Sym *arg)
{
    int align;
    TokenString *vla_array_tok = NULL;

    if (arg->type.ref)
        func_vla_arg_code(arg->type.ref);

    if ((arg->type.t & VT_VLA) && arg->type.ref->vla_array_str) {
	loc -= type_size(&int_type, &align);
	loc &= -align;
	arg->type.ref->c = loc;

	unget_tok(0);
	vla_array_tok = tok_str_alloc();
	vla_array_tok->str = arg->type.ref->vla_array_str;
	begin_macro(vla_array_tok, 1);
	next();
	gexpr();
	end_macro();
	next();
	vpush_type_size(&arg->type.ref->type, &align);
	gen_op('*');
	vset(&int_type, VT_LOCAL|VT_LVAL, arg->type.ref->c);
	vswap();
	vstore();
	vpop();
    }
}

static void func_vla_arg(Sym *sym)
{
    Sym *arg;

    for (arg = sym->type.ref->next; arg; arg = arg->next)
        if ((arg->type.t & VT_BTYPE) == VT_PTR && (arg->type.ref->type.t & VT_VLA))
            func_vla_arg_code(arg->type.ref);
}

/* set the local stack address for function parameter from `gfunc_prolog`() */
ST_FUNC Sym *gfunc_set_param(Sym *s, int c, int byref)
{
    s = sym_find(s->v);
    if (!s) /* unnamed parameters, not enabled */
        return NULL;
    s->c = c;
    if (byref)
        s->r = VT_LLOCAL | VT_LVAL; /* otherwise `VT_LOCAL` */
    return s;
}

/* push parameters (and their types), last first */
static void sym_push_params(Sym *ref)
{
    Sym *s = ref;
    while (s->next)
        s = s->next;
    while (s != ref) {
        if ((s->v & ~SYM_STRUCT) < SYM_FIRST_ANOM) {
            Sym *ps = sym_copy(s, &local_stack);
            ps->a.is_param = 1;
        }
        s = s->prev;
    }
}

/* parse a function defined by symbol 'sym' and generate its code in
   '`cur_text_section`' */
static void gen_function(Sym *sym)
{
    Scope *saved_scope = cur_scope, *saved_root = root_scope;
    Scope *saved_loop = loop_scope;
    struct switch_t *saved_switch = cur_switch;
    Sym *saved_local_stack = local_stack;
    Sym *saved_local_label_stack = local_label_stack;
    Sym *saved_global_label_stack = global_label_stack;
    Sym *saved_cleanups = all_cleanups;
    Sym *saved_pending_gotos = pending_gotos;
    Section *saved_text_section = cur_text_section;
    int saved_local_scope = local_scope, saved_rsym = rsym;
    int saved_nb_temp_local_vars = nb_temp_local_vars;
    int saved_loc = loc, saved_ind = ind;
    int saved_nocode_wanted = nocode_wanted;

    nocode_wanted = 0;

    tuple_expect = NULL;

    ind = cur_text_section->data_offset;
    if (sym->a.aligned) {
	size_t newoff = section_add(cur_text_section, 0,
				    1 << (sym->a.aligned - 1));
	gen_fill_nops(newoff - ind);
    }

    root_scope = new_function_scope(sym);

    /* `NOTE`: we patch the symbol size later */
    put_extern_sym(sym, cur_text_section, ind, 0);

    if (sym->type.ref->f.func_ctor)
        add_array (tcc_state, ".init_array", sym->c);
    if (sym->type.ref->f.func_dtor)
        add_array (tcc_state, ".fini_array", sym->c);

    /* put debug symbol */
    tcc_debug_funcstart(tcc_state, sym);

    /* push a dummy symbol to enable local sym storage */
    sym_push2(&local_stack, SYM_FIELD, 0, 0);
    /* push parameters */
    local_scope = 1;
    sym_push_params(sym->type.ref);

    local_scope = 0;
    rsym = 0;
    nb_temp_local_vars = 0;

    gfunc_prolog(sym);
    tcc_debug_prolog_epilog(tcc_state, 0);
    func_vla_arg(sym);
    block(0);
    if (root_scope->fn.pure == FUNC_PURITY_UNKNOWN) {
        root_scope->fn.pure = FUNC_PURITY_INFERRED_TRUE;
    }
    if (sym->type.ref) {
        sym->type.ref->f.func_pure = root_scope->fn.pure;
    }
    if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN) {
        root_scope->fn.safety = FUNC_SAFETY_INFERRED_SAFE;
    }
    if (sym->type.ref) {
        sym->type.ref->f.func_safety = root_scope->fn.safety;
    }
    gsym(rsym);
    nocode_wanted = 0;
    tcc_debug_end_scope(NULL, !func_var);
    tcc_debug_prolog_epilog(tcc_state, 1);
    gfunc_epilog();

    /* end of function */
    tcc_debug_funcend(tcc_state, ind - func_ind);

    /* patch symbol size */
    elfsym(sym)->st_size = ind - func_ind;
    cur_text_section->data_offset = ind;

    sym_pop(&local_stack, saved_local_stack, 0);
    label_pop(&local_label_stack, saved_local_label_stack, 0);
    label_pop(&global_label_stack, saved_global_label_stack, 0);
    sym_pop(&all_cleanups, saved_cleanups, 0);
    pending_gotos = saved_pending_gotos;

    /* pop the function scope and resume the enclosing function */
    cur_scope = saved_scope;
    root_scope = saved_root;
    loop_scope = saved_loop;
    cur_switch = saved_switch;
    --scope_depth;
    local_scope = saved_local_scope;
    rsym = saved_rsym;
    nb_temp_local_vars = saved_nb_temp_local_vars;
    loc = saved_loc;

    if (scope_depth) {
        ind = saved_ind;
        nocode_wanted = saved_nocode_wanted;
        cur_text_section = saved_text_section;
    } else {
        /* `It`'s better to crash than to generate wrong code */
        cur_text_section = NULL;
        ind = 0;
        nocode_wanted = DATA_ONLY_WANTED;
    }
    check_vstack();

    /* do this after funcend debug info */
    next();
}

static void gen_inline_functions(TCCState *s)
{
    Sym *sym;
    int inline_generated, i;
    struct InlineFunc *fn;

    tcc_open_bf(s, ":inline:", 0);
    /* iterate while inline function are referenced */
    do {
        inline_generated = 0;
        for (i = 0; i < s->nb_inline_fns; ++i) {
            fn = s->inline_fns[i];
            sym = fn->sym;
            if (sym && (sym->c || !(sym->type.t & VT_STORAGE_INLINE))) {
                /* the function was used or forced (and then not internal):
                   generate its code and convert it to a normal function */
                fn->sym = NULL;
                tccpp_putfile(fn->filename);
                begin_macro(fn->func_str, 1);
                next();
                cur_text_section = text_section;
                gen_function(sym);
                end_macro();

                inline_generated = 1;
            }
        }
    } while (inline_generated);
    tcc_close();
}

static void free_inline_functions(TCCState *s)
{
    int i;
    /* free tokens of unused inline functions */
    for (i = 0; i < s->nb_inline_fns; ++i) {
        struct InlineFunc *fn = s->inline_fns[i];
        if (fn->sym)
            tok_str_free(fn->func_str);
    }
    dynarray_reset(&s->inline_fns, &s->nb_inline_fns);
}

#include "tccgen-d.c"
