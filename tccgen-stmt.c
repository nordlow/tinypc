/* ------------------------------------------------------------------------- */
/* return from function */

#ifndef TCC_TARGET_ARM64
static void gfunc_return(CType *func_type)
{
    if ((func_type->t & VT_BTYPE) == VT_STRUCT) {
        CType type, ret_type;
        int ret_align, ret_nregs, regsize;
        ret_nregs = gfunc_sret(func_type, func_var, &ret_type,
                               &ret_align, &regsize);
        if (ret_nregs < 0) {
#ifdef TCC_TARGET_RISCV64
            arch_transfer_ret_regs(0);
#endif
        } else if (0 == ret_nregs) {
            /* if returning structure, must copy it to implicit
               first pointer arg location */
            type = *func_type;
            mk_pointer(&type);
            vset(&type, VT_LOCAL | VT_LVAL, func_vc);
            indir();
            vswap();
            /* copy structure value to pointer */
            vstore();
        } else {
            /* returning structure packed into registers */
            int size, addr, align, rc, n;
            size = type_size(func_type,&align);
            if (ret_nregs * regsize > size ||
		((align & (ret_align - 1))
                 && ((vtop->r & VT_VALMASK) < VT_CONST /* pointer to struct */
                     || (vtop->c.i & (ret_align - 1))
                     ))) {
		if (ret_nregs * regsize > size)
		    size = ret_nregs * regsize;
		if (ret_align > align)
		    align = ret_align;
                loc = (loc - size) & -align;
                addr = loc;
                type = *func_type;
                vset(&type, VT_LOCAL | VT_LVAL, addr);
                vswap();
                vstore();
                vpop();
                vset(&ret_type, VT_LOCAL | VT_LVAL, addr);
            }
            vtop->type = ret_type;
            rc = RC_RET(ret_type.t);
            //printf("struct return: n:%d t:%02x rc:%02x\n", ret_nregs, ret_type.t, rc);
            for (n = ret_nregs; --n > 0;) {
                vdup();
                gv(rc);
                vswap();
                incr_offset(regsize);
                /* `We` assume that when a structure is returned in multiple
                   registers, their classes are consecutive values of the
                   suite s(n) = 2^n */
                rc <<= 1;
            }
            gv(rc);
            vtop -= ret_nregs - 1;
        }
    } else {
        gv(RC_RET(func_type->t));
    }
    vtop--; /* `NOT` vpop() because on x86 it would flush the fp stack */
}
#endif

static void check_func_return(void)
{
    if ((func_vt.t & VT_BTYPE) == VT_VOID)
        return;
    if ((!strcmp(funcname, "main") || func_old)
        && (func_vt.t & VT_BTYPE) == VT_INT) {
        /* main returns 0 by default */
        vpushi(0);
        gfunc_return(&func_vt);
    } else {
        tcc_warning("function might return no value: '%s'", funcname);
    }
}

/* ------------------------------------------------------------------------- */
/* switch/case */

static int case_cmp(uint64_t a, uint64_t b)
{
    if (cur_switch->sv.type.t & VT_UNSIGNED)
        return a < b ? -1 : a > b;
    else
        return (int64_t)a < (int64_t)b ? -1 : (int64_t)a > (int64_t)b;
}

static int case_cmp_qs(const void *pa, const void *pb)
{
    return case_cmp((*(struct case_t**)pa)->v1, (*(struct case_t**)pb)->v1);
}

static void case_sort(struct switch_t *sw)
{
    struct case_t **p;
    if (sw->n < 2)
        return;
    qsort(sw->p, sw->n, sizeof *sw->p, case_cmp_qs);
    p = sw->p;
    while (p < sw->p + sw->n - 1) {
        if (case_cmp(p[0]->v2, p[1]->v1) >= 0) {
            int l1 = p[0]->line, l2 = p[1]->line;
            /* using special format "%i:..." to show specific line */
            tcc_error("%i:duplicate case value", l1 > l2 ? l1 : l2);
        } else if (p[0]->v2 + 1 == p[1]->v1 && p[0]->ind == p[1]->ind) {
            /* treat "case 1: case 2: case 3:" like "case 1 ... 3: */
            p[1]->v1 = p[0]->v1;
            tcc_free(p[0]);
            memmove(p, p + 1, (--sw->n - (p - sw->p)) * sizeof *p);
        } else
            ++p;
    }
}

static int gcase(struct case_t **base, int len, int dsym)
{
    struct case_t *p;
    int t, l2, e;

    t = vtop->type.t & VT_BTYPE;
    if (t != VT_LLONG)
        t = VT_INT;
    while (len) {
        /* binary search while len > 8, else linear */
        l2 = len > 8 ? len/2 : 0;
        p = base[l2];
        vdup(), vpush64(t, p->v2);
        if (l2 == 0 && p->v1 == p->v2) {
            gen_op(TOK_EQ); /* jmp to case when equal */
            gsym_addr(gvtst(0, 0), p->ind);
        } else {
            /* case v1 ... v2 */
            gen_op(TOK_GT); /* jmp over when > `V2` */
            if (len == 1) /* last case test jumps to default when false */
                dsym = gvtst(0, dsym), e = 0;
            else
                e = gvtst(0, 0);
            vdup(), vpush64(t, p->v1);
            gen_op(TOK_GE); /* jmp to case when >= `V1` */
            gsym_addr(gvtst(0, 0), p->ind);
            dsym = gcase(base, l2, dsym);
            gsym(e);
        }
        ++l2, base += l2, len -= l2;
    }
    /* jump automagically will suppress more jumps */
    return gjmp(dsym);
}

static void end_switch(void)
{
    struct switch_t *sw = cur_switch;
    dynarray_reset(&sw->p, &sw->n);
    cur_switch = sw->prev;
    tcc_free(sw);
}

/* ------------------------------------------------------------------------- */
/* `__attribute__`((cleanup(fn))) */

/* protect symbol lvalues from further modification  */
static void save_lvalues(void)
{
    SValue *sv = vtop;
    while (sv >= vstack) {
        if (sv->sym && (sv->r & VT_LVAL)) {
            int align, size = type_size(&sv->type, &align);
            int r2, l = get_temp_local_var(size, align, &r2);
            vset(&sv->type, VT_LOCAL | VT_LVAL, l), vtop->r2 = r2;
            vpushv(sv), *sv = vtop[-1], vstore(), --vtop;
        }
        --sv;
    }
}

static void try_call_scope_cleanup(Sym *stop)
{
    Sym *cls = cur_scope->cl.s;
    for (; cls != stop; cls = cls->next) {
	Sym *fs = cls->cleanup_func;
	Sym *vs = cls->cleanup_sym;
        if (!vs) {
            TokenString *dstr = (TokenString *)fs;
            int saved_vtop_idx = vtop - vstack;
            begin_macro(dstr, 0);
            next();
            if (tok == '{') {
                block(0);
            } else {
                gexpr();
                vpop();
            }
            end_macro();
            vtop = vstack + saved_vtop_idx;
        } else {
            save_lvalues();
            vpushsym(&fs->type, fs);
            vset(&vs->type, vs->r, vs->c);
            vtop->sym = vs;
            mk_pointer(&vtop->type);
            gaddrof();
            gfunc_call(1);
        }
    }
}


static void try_call_cleanup_goto(Sym *cleanupstate)
{
    Sym *oc, *cc;
    int ocd, ccd;

    if (!cur_scope->cl.s)
	return;

    /* search `NCA` of both cleanup chains given parents and initial depth */
    ocd = cleanupstate ? cleanupstate->v & ~SYM_FIELD : 0;
    for (ccd = cur_scope->cl.n, oc = cleanupstate; ocd > ccd; --ocd, oc = oc->next)
      ;
    for (cc = cur_scope->cl.s; ccd > ocd; --ccd, cc = cc->next)
      ;
    for (; cc != oc; cc = cc->next, oc = oc->next, --ccd)
      ;

    try_call_scope_cleanup(cc);
}

/* call 'func' for each `__attribute__`((cleanup(func))) */
static void block_cleanup(Scope *o)
{
    int jmp = 0;
    Sym *g, **pg;
    for (pg = &pending_gotos; (g = *pg) && g->c > o->cl.n;) {
        if (g->cleanup_label->r & LABEL_FORWARD) {
            Sym *pcl = g->next;
            if (!jmp)
                jmp = gjmp(0);
            gsym(pcl->jnext);
            try_call_scope_cleanup(o->cl.s);
            pcl->jnext = gjmp(0);
            if (!o->cl.n)
                goto remove_pending;
            g->c = o->cl.n;
            pg = &g->prev;
        } else {
    remove_pending:
            *pg = g->prev;
            sym_free(g);
        }
    }
    gsym(jmp);
    try_call_scope_cleanup(o->cl.s);
}

/* ------------------------------------------------------------------------- */
/* `VLA` */

static void vla_restore(int loc)
{
    if (loc)
        gen_vla_sp_restore(loc);
}

static void vla_leave(Scope *o)
{
    Scope *c = cur_scope, *v = NULL;
    for (; c != o && c; c = c->prev)
      if (c->vla.num)
        v = c;
    if (v)
      vla_restore(v->vla.locorig);
}

/* ------------------------------------------------------------------------- */
/* local scopes */

/* `Reject` an explicit `@system` scope nested inside an `@safe` scope.
   Only `@trusted` may relax the safety level of a `@safe` scope.  */
static void check_system_scope_override(FuncSafety safety)
{
    if (safety == FUNC_SAFETY_SYSTEM
        && cur_scope->fn.safety == FUNC_SAFETY_SAFE)
        tcc_error("@system is not allowed in @safe scope; use @trusted instead");
}

static Scope *push_scope(void)
{
    Scope *o;

    if (scope_depth >= scope_depth_limit)
        tcc_error("too many nested scopes (max %d)", scope_depth_limit);
    o = &scope_stack[++scope_depth];
    o->prev = &scope_stack[scope_depth - 1];
    return o;
}

static Scope *new_scope(void)
{
    Scope *o = push_scope();

    /* copy and link previous scope */
    *o = *cur_scope;
    o->prev = &scope_stack[scope_depth - 1];
    cur_scope = o;
    cur_scope->vla.num = 0;

    /* record local declaration stack position */
    o->lstk = local_stack;
    o->llstk = local_label_stack;
    ++local_scope;

    if (pending_block_safety) {
        check_system_scope_override(pending_block_safety);
        cur_scope->fn.safety = pending_block_safety;
        pending_block_safety = 0;
    }
    if (pending_block_pure) {
        cur_scope->fn.pure = pending_block_pure;
        pending_block_pure = 0;
    }
    return o;
}

/* a function frame does not inherit block state from its enclosing
   scope, only the enclosing scope as its parent */
static Scope *new_function_scope(Sym *sym)
{
    Scope *o = push_scope();

    memset(o, 0, sizeof *o);
    o->prev = &scope_stack[scope_depth - 1];
    if (sym->type.ref) {
        o->fn.vt = sym->type.ref->type;
        o->fn.variadic = sym->type.ref->f.func_type == FUNC_ELLIPSIS;
        o->fn.old_style = sym->type.ref->f.func_type == FUNC_OLD;
        o->fn.safety = sym->type.ref->f.func_safety;
        o->fn.pure = sym->type.ref->f.func_pure;
    } else {
        o->fn.vt = sym->type;
        o->fn.safety = FUNC_SAFETY_UNKNOWN;
        o->fn.pure = FUNC_PURITY_UNKNOWN;
    }
    o->fn.name = get_tok_str(sym->v, NULL);
    o->fn.ind = ind;
    cur_scope = o;
    return o;
}
static void prev_scope(Scope *o, int is_expr)
{
    if (o != cur_scope)
        tcc_error("internal error: unbalanced scope");

    vla_leave(o->prev);

    if (o->cl.s != o->prev->cl.s)
        block_cleanup(o->prev);

    if (debug_modes)
        tcc_debug_end_scope(o->lstk, !is_expr);

    /* pop locally defined labels */
    label_pop(&local_label_stack, o->llstk, is_expr);

    /* `In` the `is_expr` case (a statement expression is finished here),
       vtop might refer to symbols on the `local_stack`.  `Either` via the
       type or via vtop->sym.  `We` can't pop those nor any that in turn
       might be referred to.  `To` make it easier we don't roll back
       any symbols in that case; some upper level call to block() will
       do that.  `We` do have to remove such symbols from the lookup
       tables, though.  `sym_pop` will do that.  */

    /* pop locally defined symbols */
    sym_pop(&local_stack, o->lstk, is_expr);
    cur_scope = o->prev;
    --scope_depth;
    --local_scope;
}

/* leave a scope via break/continue(/goto) */
static void leave_scope(Scope *o)
{
    if (!o)
        return;
    try_call_scope_cleanup(o->cl.s);
    vla_leave(o);
}

/* short versiona for scopes with 'if/do/while/switch' which can
   declare only types (of struct/union/enum) */
static void new_scope_s(Scope *o)
{
    o->lstk = local_stack;
    ++local_scope;
}

static void prev_scope_s(Scope *o)
{
    sym_pop(&local_stack, o->lstk, 0);
    --local_scope;
}

/* ------------------------------------------------------------------------- */
/* call block from 'for do while' loops */

static void lblock(int *bsym, int *csym)
{
    Scope *lo = loop_scope, *co = cur_scope;
    int *b = co->bsym, *c = co->csym;
    if (csym) {
        co->csym = csym;
        loop_scope = co;
    }
    co->bsym = bsym;
    block(0);
    co->bsym = b;
    if (csym) {
        co->csym = c;
        loop_scope = lo;
    }
}

/* D-style foreach loop over an array or slice:

     foreach (v; a) ...          value only
     foreach (i, v; a) ...       index and value
     `foreach_reverse` (...) ...   iterate from the end

   `The` aggregate is evaluated once and converted to a slice, so that
   arrays and slices are handled uniformly.  */
static void do_foreach(int reverse)
{
    Scope *o;
    AttrDef ad, adbase, adbases[2];
    CType btype, type, agg_type, slice_type, elem_type, len_type, ptr_type;
    CType vtypes[2];
    int names[2], vaddr[2], nvars, explicit[2], i;
    int slice_addr, len_addr, ptr_addr, idx_addr;
    int size, align, a, b, c, d, e;

    o = new_scope();
    skip('(');

    nvars = 0;
    memset(&adbases[0], 0, sizeof(adbases));
    btype.t = VT_INT;
    btype.ref = NULL;

    for (;;) {
        memset(&adbase, 0, sizeof(adbase));
        if (is_type_start(tok)) {
            parse_btype(&btype, &adbase, 0);
            type = btype;
            ad = adbase;
            type_decl(&type, &ad, &names[nvars], TYPE_DIRECT);
            vtypes[nvars] = type;
            adbases[nvars] = adbase;
            explicit[nvars] = 1;
        } else {
            if (tok < TOK_UIDENT)
                expect("identifier");
            names[nvars] = tok;
            explicit[nvars] = 0;
            memset(&adbases[nvars], 0, sizeof(AttrDef));
            next();
        }
        ++nvars;
        if (tok != ',')
            break;
        if (nvars == 2)
            tcc_error("too many variables in 'foreach'");
        next();
    }
    skip(';');

    gexpr();

    if (tok == TOK_TWODOTS || tok == TOK_DOTS) {
        int inclusive = 0;
        CType start_type, end_type;
        int start_addr, end_addr;

        next(); /* consume '..' */
        if (tok == '=') {
            inclusive = 1;
            next();
        }
        if (nvars != 1)
            tcc_error("range 'foreach' requires a single loop variable");

        start_type = vtop->type;
        size = type_size(&start_type, &align);
        loc = (loc - size) & -align;
        start_addr = loc;
        vset(&start_type, VT_LOCAL | VT_LVAL, start_addr);
        vswap();
        vstore();
        vpop();

        gexpr();
        skip(')');

        end_type = vtop->type;
        size = type_size(&end_type, &align);
        loc = (loc - size) & -align;
        end_addr = loc;
        vset(&end_type, VT_LOCAL | VT_LVAL, end_addr);
        vswap();
        vstore();
        vpop();

        if (!explicit[0])
            vtypes[0] = start_type;

        decl_initializer_alloc(&vtypes[0], &adbases[0], VT_LOCAL | VT_LVAL, 0,
                               names[0], 0);
        vaddr[0] = sym_find(names[0])->c;

        /* i = reverse ? (inclusive ? end : end - 1) : start */
        {
            CType mut_vtype = vtypes[0];
            mut_vtype.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
            vset(&mut_vtype, VT_LOCAL | VT_LVAL, vaddr[0]);
            if (reverse) {
                vset(&end_type, VT_LOCAL | VT_LVAL, end_addr);
                if (!inclusive) {
                    vpushi(1);
                    gen_op('-');
                }
                gen_assign_cast(&mut_vtype);
            } else {
                vset(&start_type, VT_LOCAL | VT_LVAL, start_addr);
                gen_assign_cast(&mut_vtype);
            }
            vstore();
            vpop();
        }

        a = b = 0;
        c = gind();
        /* condition: forward: inclusive ? i <= end : i < end; reverse: i >= start */
        vset(&vtypes[0], VT_LOCAL | VT_LVAL, vaddr[0]);
        if (reverse) {
            vset(&start_type, VT_LOCAL | VT_LVAL, start_addr);
            gen_op(TOK_GE);
        } else {
            vset(&end_type, VT_LOCAL | VT_LVAL, end_addr);
            gen_op(inclusive ? TOK_LE : TOK_LT);
        }
        a = gvtst(1, 0);
        e = gjmp(0);
        d = gind();

        /* increment: ++i, or --i when iterating in reverse */
        {
            CType mut_vtype = vtypes[0];
            mut_vtype.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
            vset(&mut_vtype, VT_LOCAL | VT_LVAL, vaddr[0]);
            vset(&mut_vtype, VT_LOCAL | VT_LVAL, vaddr[0]);
            vpushi(1);
            gen_op(reverse ? '-' : '+');
            vstore();
            vpop();
        }
        gjmp_addr(c);
        gsym(e);

        lblock(&a, &b);
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
        prev_scope(o, 0);
        return;
    }

    skip(')');

    agg_type = vtop->type;
    if (is_slice_type(&agg_type)) {
        slice_type = agg_type;
    } else if (agg_type.t & (VT_ARRAY | VT_VLA)) {
        CType elem_type_arr = agg_type.ref->type;
        elem_type_arr.t |= agg_type.t & (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
        slice_type.t = VT_STRUCT;
        slice_type.ref = make_slice_type(&elem_type_arr);
        convert_array_to_slice(&slice_type);
    } else {
        tcc_error("'foreach' requires an array or slice");
    }

    elem_type = *pointed_type(&slice_type.ref->next->next->type);
    len_type = slice_type.ref->next->type;
    ptr_type = slice_type.ref->next->next->type;

    /* an array element is bound to a slice, as arrays are not
       assignable values in C */
    if (elem_type.t & (VT_ARRAY | VT_VLA)) {
        CType st;
        st.t = VT_STRUCT;
        st.ref = make_slice_type(&elem_type.ref->type);
        elem_type = st;
    }

    /* keep the slice in a temporary local so that both members can be
       loaded from it */
    slice_addr = copy_slice_to_temp(&slice_type);

    size = type_size(&len_type, &align);
    loc = (loc - size) & -align;
    len_addr = loc;
    size = type_size(&ptr_type, &align);
    loc = (loc - size) & -align;
    ptr_addr = loc;
    size = type_size(&len_type, &align);
    loc = (loc - size) & -align;
    idx_addr = loc;

    /* len = slice.length; ptr = slice.ptr */
    vset(&len_type, VT_LOCAL | VT_LVAL, slice_addr);
    vset(&len_type, VT_LOCAL | VT_LVAL, len_addr);
    vswap();
    vstore();
    vpop();
    vset(&ptr_type, VT_LOCAL | VT_LVAL, slice_addr + PTR_SIZE);
    vset(&ptr_type, VT_LOCAL | VT_LVAL, ptr_addr);
    vswap();
    vstore();
    vpop();

    /* declare the loop variables */
    for (i = 0; i < nvars; ++i) {
        if (!explicit[i])
            vtypes[i] = (nvars == 2 && i == 0) ? len_type : elem_type;
        decl_initializer_alloc(&vtypes[i], &adbases[i], VT_LOCAL | VT_LVAL, 0,
                               names[i], 0);
        vaddr[i] = sym_find(names[i])->c;
    }

    /* idx = reverse ? len : 0 */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    if (reverse)
        vset(&len_type, VT_LOCAL | VT_LVAL, len_addr);
    else
        vpushi(0);
    vstore();
    vpop();

    a = b = 0;
    c = gind();
    /* condition: idx < len, or idx > 0 when iterating in reverse */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    if (reverse) {
        vpushi(0);
        gen_op(TOK_GT);
    } else {
        vset(&len_type, VT_LOCAL | VT_LVAL, len_addr);
        gen_op(TOK_LT);
    }
    a = gvtst(1, 0);
    e = gjmp(0);
    d = gind();
    /* increment: ++idx, or --idx when iterating in reverse */
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
    vpushi(1);
    gen_op(reverse ? '-' : '+');
    vstore();
    vpop();
    gjmp_addr(c);
    gsym(e);

    /* index variable */
    if (nvars == 2) {
        CType mut_vtype0 = vtypes[0];
        mut_vtype0.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
        vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
        if (reverse) {
            vpushi(1);
            gen_op('-');
        }
        gen_assign_cast(&mut_vtype0);
        vset(&mut_vtype0, VT_LOCAL | VT_LVAL, vaddr[0]);
        vswap();
        vstore();
        vpop();
    }

    /* value variable */
    i = nvars - 1;
    {
        CType mut_vtypei = vtypes[i];
        mut_vtypei.t &= ~(VT_QUALIFIER_CONSTANT | VT_QUALIFIER_IMMUTABLE);
        vset(&ptr_type, VT_LOCAL | VT_LVAL, ptr_addr);
        vset(&len_type, VT_LOCAL | VT_LVAL, idx_addr);
        if (reverse) {
            vpushi(1);
            gen_op('-');
        }
        gen_op('+');
        indir();
        gen_assign_cast(&mut_vtypei);
        vset(&mut_vtypei, VT_LOCAL | VT_LVAL, vaddr[i]);
        vswap();
        vstore();
        vpop();
    }

    lblock(&a, &b);
    gjmp_addr(d);
    gsym_addr(b, d);
    gsym(a);
    prev_scope(o, 0);
}

/* c2y if/switch declaration */
static void gexpr_decl(void)
{
    int v = decl(VT_JMP);
    if (v > 1 && tok != ';') {
        Sym *s = sym_find(v);
        vset(&s->type, s->r, (s->r & VT_SYM) ? 0 : s->c);
        vtop->sym = s;
    } else {
        if (v)
            skip(';');
        gexpr();
    }
}

static void do_defer(void)
{
    TokenString *dstr;
    Sym *cls;

    dstr = tok_str_alloc();
    if (tok == '{') {
        int braces = 1;
        tok_str_add2(dstr, tok, &tokc);
        next();
        while (braces > 0 && tok != TOK_EOF) {
            if (tok == '{')
                braces++;
            else if (tok == '}')
                braces--;
            tok_str_add2(dstr, tok, &tokc);
            if (braces > 0)
                next();
        }
        if (braces == 0)
            next();
    } else {
        while (tok != ';' && tok != TOK_EOF) {
            tok_str_add2(dstr, tok, &tokc);
            next();
        }
        tok_str_add2(dstr, ';', NULL);
        skip(';');
    }
    tok_str_add(dstr, -1);
    cls = sym_push2(&all_cleanups, SYM_FIELD | ++cur_scope->cl.n, 0, 0);
    cls->cleanup_sym = NULL;
    cls->cleanup_func = (Sym *)dstr;
    cls->next = cur_scope->cl.s;
    cur_scope->cl.s = cls;
}

static void block(int flags)
{
    int a, b, c, d, e, t;
    Scope o, *p;
    AttrDef ad;
    Sym *s;
again:
    memset(&ad, 0, sizeof ad);
    if (check_d_conditional(VT_LOCAL, flags))
        return;
    if (check_d_pragma(VT_LOCAL))
        return;
    if (check_d_dollar_if(VT_LOCAL, flags))
        return;
    if (check_d_dollar_foreach(VT_LOCAL, flags))
        return;
    t = tok;
    /* `If` the token carries a value, next() might destroy it. `Only` with
       invalid code such as f(){"123"4;} */
    if (TOK_HAS_VALUE(t))
        goto expr;
    next();

    if (debug_modes)
        tcc_tcov_check_line (tcc_state, 0), tcc_tcov_block_begin (tcc_state);
    parse_statement:
    if (t == TOK_IF) {
        new_scope_s(&o);
        skip('(');
        gexpr_decl();
        a = gvtst(1, 0);
        skip(')');
        block(0);
        if (tok == TOK_ELSE) {
            d = gjmp(0);
            gsym(a);
            next();
            block(0);
            gsym(d); /* patch else jmp */
        } else {
            gsym(a);
        }
        prev_scope_s(&o);

    } else if (t == TOK_WHILE) {
        new_scope_s(&o);
        d = gind();
        skip('(');
        gexpr();
        a = gvtst(1, 0);
        skip(')');
        b = 0;
        lblock(&a, &b);
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
        prev_scope_s(&o);

    } else if (t == '@') {
        unget_tok('@');
        parse_d_attributes(&ad);
        check_system_scope_override(ad.f.func_safety);
        if (tok == '{') {
            t = tok;
            next();
            goto parse_compound_block;
        }
        if (tok == TOK_FOR || tok == TOK_FOREACH || tok == TOK_FOREACH_REVERSE || tok == TOK_WHILE || tok == TOK_DO || tok == TOK_SWITCH || tok == TOK_IF) {
            pending_block_safety = ad.f.func_safety;
            pending_block_pure = ad.f.func_pure;
            t = tok;
            next();
            goto parse_statement;
        }
        if (flags & STMT_EXPR) {
            vpop();
            gexpr();
        } else {
            gexpr();
            vpop();
        }
        skip(';');

    } else if (t == '{') {
    parse_compound_block:
        if (debug_modes)
            tcc_debug_stabn(tcc_state, N_LBRAC, ind - func_ind);
        p = new_scope();
        if (pending_block_safety) {
            cur_scope->fn.safety = pending_block_safety;
            pending_block_safety = 0;
        }
        if (pending_block_pure) {
            cur_scope->fn.pure = pending_block_pure;
            pending_block_pure = 0;
        }
        if (ad.f.func_safety) {
            check_system_scope_override(ad.f.func_safety);
            cur_scope->fn.safety = ad.f.func_safety;
        }
        if (ad.f.func_pure)
            cur_scope->fn.pure = ad.f.func_pure;

        /* handle local labels declarations */
        while (tok == TOK_LABEL) {
            do {
                next();
                if (tok < TOK_UIDENT)
                    expect("label identifier");
                label_push(&local_label_stack, tok, LABEL_DECLARED);
                next();
            } while (tok == ',');
            skip(';');
        }

        while (tok != '}') {
	    decl(VT_LOCAL);
            if (tok != '}') {
                block(flags | STMT_COMPOUND);
            }
        }

        prev_scope(p, flags & STMT_EXPR);
        if (debug_modes)
            tcc_debug_stabn(tcc_state, N_RBRAC, ind - func_ind);
        if (local_scope)
            next();
        else if (!nocode_wanted)
            check_func_return();


    } else if (t == TOK_RETURN) {
        b = (func_vt.t & VT_BTYPE) != VT_VOID;
        if (tok != ';') {
            CType *saved_expect = tuple_expect;
            if (is_tuple_type(&func_vt))
                tuple_expect = &func_vt;
            gexpr();
            tuple_expect = saved_expect;
            if (b) {
                if (vtop >= vstack &&
                    (vtop->r & VT_SYM || (vtop->r & VT_VALMASK) == VT_LOCAL || (vtop->r & VT_VALMASK) == VT_LLOCAL || (vtop->r & VT_LVAL))) {
                    check_stack_address_escape(NULL, vtop->sym, 1, &func_vt);
                }
                gen_assign_cast(&func_vt);
            } else {
                if (vtop->type.t != VT_VOID)
                    tcc_warning("void function returns a value");
                vtop--;
            }
        } else if (b && func_old && (func_vt.t & VT_BTYPE) == VT_INT) {
            vpushi(0);
        } else if (b) {
            tcc_warning("'return' with no value");
            b = 0;
        }
        leave_scope(root_scope);
        if (b)
            gfunc_return(&func_vt);
        skip(';');
        /* jump unless last stmt in top-level block */
        if (tok != '}' || local_scope != 1)
            rsym = gjmp(rsym);
        if (debug_modes)
	    tcc_tcov_block_end (tcc_state, -1);
        CODE_OFF();

    } else if (t == TOK_BREAK) {
        /* compute jump */
        if (!cur_scope->bsym)
            tcc_error("cannot break");
        if (cur_switch && cur_scope->bsym == cur_switch->bsym)
            leave_scope(cur_switch->scope);
        else
            leave_scope(loop_scope);
        *cur_scope->bsym = gjmp(*cur_scope->bsym);
        skip(';');

    } else if (t == TOK_CONTINUE) {
        /* compute jump */
        if (!cur_scope->csym)
            tcc_error("cannot continue");
        leave_scope(loop_scope);
        *cur_scope->csym = gjmp(*cur_scope->csym);
        skip(';');

    } else if (t == TOK_DEFER) {
        do_defer();
    } else if (t == TOK_ASSERT) {
        do_d_assert();
    } else if (tcc_state->lang_d && tok == '('
               && t >= TOK_UIDENT
               && !strcmp(get_tok_str(t, &tokc), "scope")) {
        next(); /* skip '(' */
        if (tok >= TOK_UIDENT) {
            const char *name = get_tok_str(tok, &tokc);
            if (!strcmp(name, "success") || !strcmp(name, "failure")) {
                tcc_error("scope(success) / scope(failure) requires error status tracking");
            }
        }
        if (!(tok >= TOK_UIDENT
              && !strcmp(get_tok_str(tok, &tokc), "exit")))
            tcc_error("only 'scope(exit)' is supported");
        next();
        skip(')');
        do_defer();
    } else if (t == TOK_FOREACH || t == TOK_FOREACH_REVERSE) {
        do_foreach(t == TOK_FOREACH_REVERSE);
    } else if (t == TOK_FOR) {
        p = new_scope();

        skip('(');
        if (tok != ';') {
            /* c99 for-loop init decl? */
            if (!decl(VT_JMP)) {
                /* no, regular for-loop init expr */
                gexpr();
                vpop();
            }
        }
        skip(';');
        a = b = 0;
        c = d = gind();
        if (tok != ';') {
            gexpr();
            a = gvtst(1, 0);
        }
        skip(';');
        if (tok != ')') {
            e = gjmp(0);
            d = gind();
            gexpr();
            vpop();
            gjmp_addr(c);
            gsym(e);
        }
        skip(')');
        lblock(&a, &b);
        gjmp_addr(d);
        gsym_addr(b, d);
        gsym(a);
        prev_scope(p, 0);

    } else if (t == TOK_DO) {
        new_scope_s(&o);
        a = b = 0;
        d = gind();
        lblock(&a, &b);
        gsym(b);
        skip(TOK_WHILE);
        skip('(');
	gexpr();
        c = gvtst(0, 0);
        skip(')');
        skip(';');
	gsym_addr(c, d);
        gsym(a);
        prev_scope_s(&o);

    } else if (t == TOK_SWITCH) {
        struct switch_t *sw;

        sw = tcc_mallocz(sizeof *sw);
        sw->bsym = &a;
        sw->scope = cur_scope;
        sw->prev = cur_switch;
        sw->nocode_wanted = nocode_wanted;
        cur_switch = sw;

        new_scope_s(&o);
        skip('(');
        gexpr_decl();
        if (!is_integer_btype(vtop->type.t & VT_BTYPE))
            tcc_error("switch value not an integer");
        skip(')');
        sw->sv = *vtop--; /* save switch value */
        a = 0;
        b = gjmp(0); /* jump to first case */
        lblock(&a, NULL);
        a = gjmp(a); /* add implicit break */
        /* case lookup */
        gsym(b);
        prev_scope_s(&o);
        if (sw->nocode_wanted)
            goto skip_switch;
        case_sort(sw);
        sw->bsym = NULL; /* marker for 32bit:`gen_opl`() */
        vpushv(&sw->sv);
        gv(RC_INT);
        d = gcase(sw->p, sw->n, 0);
        vpop();
        if (sw->def_sym)
            gsym_addr(d, sw->def_sym);
        else
            gsym(d);
    skip_switch:
        /* break label */
        gsym(a);
        end_switch();

    } else if (t == TOK_CASE) {
        struct case_t *cr;
        if (!cur_switch)
            expect("switch");
        cr = tcc_malloc(sizeof(struct case_t));
        dynarray_add(&cur_switch->p, &cur_switch->n, cr);
        t = cur_switch->sv.type.t;
        cr->v1 = cr->v2 = value64(expr_const64(), t);
        if (tok == TOK_DOTS && gnu_ext) {
            next();
            cr->v2 = value64(expr_const64(), t);
            if (case_cmp(cr->v2, cr->v1) < 0)
                tcc_warning("empty case range");
        }
        /* case and default are unreachable from a switch under `nocode_wanted` */
        if (!cur_switch->nocode_wanted)
            cr->ind = gind();
        cr->line = file->line_num;
        skip(':');
        goto block_after_label;

    } else if (t == TOK_DEFAULT) {
        if (!cur_switch)
            expect("switch");
        if (cur_switch->def_sym)
            tcc_error("too many 'default'");
        cur_switch->def_sym = cur_switch->nocode_wanted ? -1 : gind();
        skip(':');
        goto block_after_label;

    } else if (t == TOK_GOTO) {
        vla_restore(cur_scope->vla.locorig);
        if (tok == '*' && gnu_ext) {
            /* computed goto */
            next();
            gexpr();
            if ((vtop->type.t & VT_BTYPE) != VT_PTR)
                expect("pointer");
            ggoto();

        } else if (tok >= TOK_UIDENT) {
	    s = label_find(tok);
	    /* put forward definition if needed */
            if (!s)
              s = label_push(&global_label_stack, tok, LABEL_FORWARD);
            else if (s->r == LABEL_DECLARED)
              s->r = LABEL_FORWARD;

	    if (s->r & LABEL_FORWARD) {
		/* start new goto chain for cleanups, linked via label->next */
		if (cur_scope->cl.s && !nocode_wanted) {
                    sym_push2(&pending_gotos, SYM_FIELD, 0, cur_scope->cl.n);
                    pending_gotos->cleanup_label = s;
                    s = sym_push2(&s->next, SYM_FIELD, 0, 0);
                    pending_gotos->next = s;
                }
		s->jnext = gjmp(s->jnext);
	    } else {
		try_call_cleanup_goto(s->cleanupstate);
		gjmp_addr(s->jind);
	    }
	    next();

        } else {
            expect("label identifier");
        }
        skip(';');

    } else if (t == TOK_ASM1 || t == TOK_ASM2 || t == TOK_ASM3) {
        if (cur_scope->fn.safety == FUNC_SAFETY_SAFE)
            tcc_error("inline assembly not allowed in @safe scope");
        else {
            if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
        }
        asm_instr();

    } else {
        if (tok == ':' && t >= TOK_UIDENT) {
            /* label case */
	    next();
            s = label_find(t);
            if (s) {
                if (s->r == LABEL_DEFINED)
                    tcc_error("duplicate label '%s'", get_tok_str(s->v, NULL));
                s->r = LABEL_DEFINED;
		if (s->next) {
		    Sym *pcl; /* pending cleanup goto */
		    for (pcl = s->next; pcl; pcl = pcl->prev)
		      gsym(pcl->jnext);
		    sym_pop(&s->next, NULL, 0);
		} else
		  gsym(s->jnext);
            } else {
                s = label_push(&global_label_stack, t, LABEL_DEFINED);
            }
            s->jind = gind();
            s->cleanupstate = cur_scope->cl.s;

    block_after_label:
            /* `Accept` attributes after labels (e.g. 'unused') */
            parse_attribute(NULL);

            if (debug_modes)
                tcc_tcov_reset_ind(tcc_state);
            vla_restore(cur_scope->vla.loc);

            if (tok != '}') {
                if (0 == (flags & STMT_COMPOUND))
                    goto again;
                /* `C23`: insert implicit null-statement whithin compound statement */
            } else {
                /* we accept this, but it is a mistake */
                tcc_warning_c(warn_all)("deprecated use of label at end of compound statement");
            }
        } else {
            /* expression case */
            if (t != ';') {
                unget_tok(t);
    expr:
                if (flags & STMT_EXPR) {
                    vpop();
                    gexpr();
                } else {
                    gexpr();
                    vpop();
                }
                skip(';');
            }
        }
    }

    if (debug_modes)
        tcc_tcov_check_line (tcc_state, 0), tcc_tcov_block_end (tcc_state, 0);
}

/* `This` skips over a stream of tokens containing balanced {} and ()
   pairs, stopping at outer ',' ';' and '}' (or matching '}' if we started
   with a '{').  `If` `STR` then allocates and stores the skipped tokens
   in *`STR`.  `This` doesn't check if () and {} are nested correctly,
   i.e. "({)}" is accepted.  */
static void skip_or_save_block(TokenString **str)
{
    int braces = tok == '{';
    int level = 0;
    if (str)
      *str = tok_str_alloc();

    while (1) {
	int t = tok;
        if (level == 0
            && (t == ','
             || t == ';'
             || t == '}'
             || t == ')'
             || t == ']'))
             break;
	if (t == TOK_EOF) {
	     if (str || level > 0)
	       tcc_error("unexpected end of file");
	     else
	       break;
	}
	if (str)
	  tok_str_add_tok(*str);
	next();
	if (t == '{' || t == '(' || t == '[') {
	    level++;
	} else if (t == '}' || t == ')' || t == ']') {
	    level--;
	    if (level == 0 && braces && t == '}')
	      break;
	}
    }
    if (str)
	tok_str_add(*str, TOK_EOF);
}

#include "tccgen-init.c"
