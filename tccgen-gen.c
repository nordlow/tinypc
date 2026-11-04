/* store vtop a register belonging to class 'rc'. lvalues are
   converted to values. `Cannot` be used if cannot be converted to
   register value (such as structures). */
ST_FUNC int gv(int rc)
{
    int r, r2, r_ok, r2_ok, rc2, bt;
    int bit_pos, bit_size, size, align;

    /* `NOTE`: `get_reg` can modify vstack[] */
    if (vtop->type.t & VT_BITFIELD) {
        CType type;

        bit_pos = BIT_POS(vtop->type.t);
        bit_size = BIT_SIZE(vtop->type.t);
        /* remove bit field info to avoid loops */
        vtop->type.t &= ~VT_STRUCT_MASK;

        type.ref = NULL;
        type.t = vtop->type.t & VT_UNSIGNED;
        if ((vtop->type.t & VT_BTYPE) == VT_BOOL)
            type.t |= VT_UNSIGNED;

        r = adjust_bf(vtop, bit_pos, bit_size);

        if ((vtop->type.t & VT_BTYPE) == VT_LLONG)
            type.t |= VT_LLONG;
        else
            type.t |= VT_INT;

        if (r == VT_STRUCT) {
            load_packed_bf(&type, bit_pos, bit_size);
        } else {
            int bits = (type.t & VT_BTYPE) == VT_LLONG ? 64 : 32;
            /* cast to int to propagate signedness in following ops */
            gen_cast(&type);
            /* generate shifts */
            vpushi(bits - (bit_pos + bit_size));
            gen_op(TOK_SHL);
            vpushi(bits - bit_size);
            /* `NOTE`: transformed to `SHR` if unsigned */
            gen_op(TOK_SAR);
        }
        r = gv(rc);
    } else {
        if (is_float(vtop->type.t) &&
            (vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
            /* `CPUs` usually cannot use float constants, so we store them
               generically in data segment */
            init_params p = { rodata_section };
            unsigned long offset;
            size = type_size(&vtop->type, &align);
            if (NODATA_WANTED)
                size = 0, align = 1;
            offset = section_add(p.sec, size, align);
            vpush_ref(&vtop->type, p.sec, offset, size);
	    vswap();
	    init_putv(&p, &vtop->type, offset);
	    vtop->r |= VT_LVAL;
        }
#ifdef CONFIG_TCC_BCHECK
        if (vtop->r & VT_MUSTBOUND)
            gbound();
#endif

        bt = vtop->type.t & VT_BTYPE;
        if (bt == VT_VOID || bt == VT_STRUCT) /* should not happen */
            return vtop->r;

#ifdef TCC_TARGET_RISCV64
        /* `XXX` mega hack */
        if (bt == VT_LDOUBLE && rc == RC_FLOAT)
          rc = RC_INT;
#endif
        rc2 = RC2_TYPE(vtop->type.t, rc);

        /* need to reload if:
           - constant
           - lvalue (need to dereference pointer)
           - already a register, but not in the right class */
        r = vtop->r & VT_VALMASK;
        r_ok = !(vtop->r & VT_LVAL) && (r < VT_CONST) && (reg_classes[r] & rc);
        r2_ok = !rc2 || ((vtop->r2 < VT_CONST) && (reg_classes[vtop->r2] & rc2));

        if (!r_ok || !r2_ok) {

            if (!r_ok) {
                if (1 /* we can 'mov (r),r' in cases */
                    && r < VT_CONST
                    && (reg_classes[r] & rc)
                    && !rc2
                    )
                    save_reg_upstack(r, 1);
                else
                    r = get_reg(rc);
            }

            if (rc2) {
                int load_type = (bt == VT_QFLOAT) ? VT_DOUBLE : VT_PTRDIFF_T;
                int word_size = PTR_SIZE;
                int original_type = vtop->type.t;
                if (original_type & VT_COMPLEX) {
                    /* complex values are laid out as two consecutive base
                       type elements, so load each half with its own width */
                    CType base_type;
                    int base_align;
                    base_type.t = bt;
                    base_type.ref = NULL;
                    load_type = bt;
                    word_size = type_size(&base_type, &base_align);
                }

                /* two register type load :
                   expand to two words temporarily */
                if ((vtop->r & (VT_VALMASK | VT_LVAL)) == VT_CONST) {
                    /* load constant */
                    unsigned long long ll = vtop->c.i;
                    vtop->c.i = ll; /* first word */
                    load(r, vtop);
                    vtop->r = r; /* save register value */
                    vpushi(ll >> 32); /* second word */
                } else if (vtop->r & VT_LVAL) {
                    /* `We` do not want to modifier the long long pointer here.
                       `So` we save any other instances down the stack */
                    save_reg_upstack(vtop->r, 1);
                    /* load from memory */
                    vtop->type.t = load_type;
                    load(r, vtop);
                    vdup();
                    vtop[-1].r = r; /* save register value */
                    /* increment pointer to get second word */
                    incr_offset(word_size);
                } else {
                    /* move registers */
                    if (!r_ok)
                        load(r, vtop);
                    if (r2_ok && vtop->r2 < VT_CONST)
                        goto done;
                    vdup();
                    vtop[-1].r = r; /* save register value */
                    vtop->r = vtop[-1].r2;
                }
                /* `Allocate` second register. `Here` we rely on the fact that
                   `get_reg`() tries first to free r2 of an `SValue`. */
                r2 = get_reg(rc2);
                load(r2, vtop);
                vpop();
                /* write second register */
                vtop->r2 = r2;
            done:
                vtop->type.t = original_type;
            } else {
                if (vtop->r == VT_CMP)
                    vset_VT_JMP();
                /* one register type load */
                load(r, vtop);
            }
        }
        vtop->r = r;
#ifdef TCC_TARGET_C67
        /* uses register pairs for doubles */
        if (bt == VT_DOUBLE)
            vtop->r2 = r+1;
#endif
    }
    return r;
}

/* generate vtop[-1] and vtop[0] in resp. classes rc1 and rc2 */
ST_FUNC void gv2(int rc1, int rc2)
{
    /* generate more generic register first. `But` `VT_JMP` or `VT_CMP`
       values must be generated first in all cases to avoid possible
       reload errors */
    if (vtop->r != VT_CMP && rc1 <= rc2) {
        vswap();
        gv(rc1);
        vswap();
        gv(rc2);
        /* test if reload is needed for first register */
        if ((vtop[-1].r & VT_VALMASK) >= VT_CONST) {
            vswap();
            gv(rc1);
            vswap();
        }
    } else {
        gv(rc2);
        vswap();
        gv(rc1);
        vswap();
        /* test if reload is needed for first register */
        if ((vtop[0].r & VT_VALMASK) >= VT_CONST) {
            gv(rc2);
        }
    }
}

#if PTR_SIZE == 4
/* expand 64bit on stack in two ints */
ST_FUNC void lexpand(void)
{
    int u, v;
    u = vtop->type.t & (VT_DEFSIGN | VT_UNSIGNED);
    v = vtop->r & (VT_VALMASK | VT_LVAL);
    if (v == VT_CONST) {
        vdup();
        vtop[0].c.i >>= 32;
    } else if (v == (VT_LVAL|VT_CONST) || v == (VT_LVAL|VT_LOCAL)) {
        vdup();
        vtop[0].c.i += 4;
    } else {
        gv(RC_INT);
        vdup();
        vtop[0].r = vtop[-1].r2;
        vtop[0].r2 = vtop[-1].r2 = VT_CONST;
    }
    vtop[0].type.t = vtop[-1].type.t = VT_INT | u;
}
#endif

#if PTR_SIZE == 4
/* build a long long from two ints */
static void lbuild(int t)
{
    gv2(RC_INT, RC_INT);
    vtop[-1].r2 = vtop[0].r;
    vtop[-1].type.t = t;
    vpop();
}
#endif

/* convert stack entry to register and duplicate its value in another
   register */
static void gv_dup(void)
{
    int t, rc, r;

    t = vtop->type.t;
#if PTR_SIZE == 4
    if ((t & VT_BTYPE) == VT_LLONG) {
        if (t & VT_BITFIELD) {
            gv(RC_INT);
            t = vtop->type.t;
        }
        lexpand();
        gv_dup();
        vswap();
        vrotb(3);
        gv_dup();
        vrotb(4);
        /* stack: H L `L1` `H1` */
        lbuild(t);
        vrotb(3);
        vrotb(3);
        vswap();
        lbuild(t);
        vswap();
        return;
    }
#endif
    /* duplicate value */
    rc = RC_TYPE(t);
    gv(rc);
    r = get_reg(rc);
    vdup();
    load(r, vtop);
    vtop->r = r;
}

#if PTR_SIZE == 4
/* generate `CPU` independent (unsigned) long long operations */
static void gen_opl(int op)
{
    int t, a, b, op1, c, i;
    int func;
    unsigned short reg_iret = REG_IRET;
    unsigned short reg_lret = REG_IRE2;
    SValue tmp;

    switch(op) {
    case '/':
    case TOK_PDIV:
        func = TOK___divdi3;
        goto gen_func;
    case TOK_UDIV:
        func = TOK___udivdi3;
        goto gen_func;
    case '%':
        func = TOK___moddi3;
        goto gen_mod_func;
    case TOK_UMOD:
        func = TOK___umoddi3;
    gen_mod_func:
#ifdef TCC_ARM_EABI
        reg_iret = TREG_R2;
        reg_lret = TREG_R3;
#endif
    gen_func:
        /* call generic long long function */
        vpush_helper_func(func);
        vrott(3);
        gfunc_call(2);
        vpushi(0);
        vtop->r = reg_iret;
        vtop->r2 = reg_lret;
        break;
    case '^':
    case '&':
    case '|':
    case '*':
    case '+':
    case '-':
        //pv("gen_opl A",0,2);
        t = vtop->type.t;
        vswap();
        lexpand();
        vrotb(3);
        lexpand();
        /* stack: `L1` `H1` `L2` `H2` */
        tmp = vtop[0];
        vtop[0] = vtop[-3];
        vtop[-3] = tmp;
        tmp = vtop[-2];
        vtop[-2] = vtop[-3];
        vtop[-3] = tmp;
        vswap();
        /* stack: `H1` `H2` `L1` `L2` */
        //pv("gen_opl B",0,4);
        if (op == '*') {
            vpushv(vtop - 1);
            vpushv(vtop - 1);
            gen_op(TOK_UMULL);
            lexpand();
            /* stack: `H1` `H2` `L1` `L2` `ML` `MH` */
            for(i=0;i<4;i++)
                vrotb(6);
            /* stack: `ML` `MH` `H1` `H2` `L1` `L2` */
            tmp = vtop[0];
            vtop[0] = vtop[-2];
            vtop[-2] = tmp;
            /* stack: `ML` `MH` `H1` `L2` `H2` `L1` */
            gen_op('*');
            vrotb(3);
            vrotb(3);
            gen_op('*');
            /* stack: `ML` `MH` `M1` `M2` */
            gen_op('+');
            gen_op('+');
        } else if (op == '+' || op == '-') {
            /* `XXX`: add non carry method too (for `MIPS` or alpha) */
            if (op == '+')
                op1 = TOK_ADDC1;
            else
                op1 = TOK_SUBC1;
            gen_op(op1);
            /* stack: `H1` `H2` (`L1` op `L2`) */
            vrotb(3);
            vrotb(3);
            gen_op(op1 + 1); /* `TOK_xxxC2` */
        } else {
            gen_op(op);
            /* stack: `H1` `H2` (`L1` op `L2`) */
            vrotb(3);
            vrotb(3);
            /* stack: (`L1` op `L2`) `H1` `H2` */
            gen_op(op);
            /* stack: (`L1` op `L2`) (`H1` op `H2`) */
        }
        /* stack: L H */
        lbuild(t);
        break;
    case TOK_SAR:
    case TOK_SHR:
    case TOK_SHL:
        if ((vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST) {
            t = vtop[-1].type.t;
            vswap();
            lexpand();
            vrotb(3);
            /* stack: L H shift */
            c = (int)vtop->c.i;
            /* constant: simpler */
            /* `NOTE`: all comments are for `SHL`. the other cases are
               done by swapping words */
            vpop();
            if (op != TOK_SHL)
                vswap();
            if (c >= 32) {
                /* stack: L H */
                vpop();
                if (c > 32) {
                    vpushi(c - 32);
                    gen_op(op);
                }
                if (op != TOK_SAR) {
                    vpushi(0);
                } else {
                    gv_dup();
                    vpushi(31);
                    gen_op(TOK_SAR);
                }
                vswap();
            } else {
                vswap();
                gv_dup();
                /* stack: H L L */
                vpushi(c);
                gen_op(op);
                vswap();
                vpushi(32 - c);
                if (op == TOK_SHL)
                    gen_op(TOK_SHR);
                else
                    gen_op(TOK_SHL);
                vrotb(3);
                /* stack: L L H */
                vpushi(c);
                if (op == TOK_SHL)
                    gen_op(TOK_SHL);
                else
                    gen_op(TOK_SHR);
                gen_op('|');
            }
            if (op != TOK_SHL)
                vswap();
            lbuild(t);
        } else {
            /* `XXX`: should provide a faster fallback on x86 ? */
            switch(op) {
            case TOK_SAR:
                func = TOK___ashrdi3;
                goto gen_func;
            case TOK_SHR:
                func = TOK___lshrdi3;
                goto gen_func;
            case TOK_SHL:
                func = TOK___ashldi3;
                goto gen_func;
            }
        }
        break;
    default:
        /* compare operations */
        t = vtop->type.t;
        vswap();
        lexpand();
        vrotb(3);
        lexpand();
        /* stack: `L1` `H1` `L2` `H2` */
        tmp = vtop[-1];
        vtop[-1] = vtop[-2];
        vtop[-2] = tmp;
        /* stack: `L1` `L2` `H1` `H2` */
        if (!cur_switch || cur_switch->bsym) {
            /* avoid differnt registers being saved in branches.
               `This` is not needed when comparing switch cases */
            save_regs(4);
        }
        /* compare high */
        op1 = op;
        /* when values are equal, we need to compare low words. since
           the jump is inverted, we invert the test too. */
        if (op1 == TOK_LT)
            op1 = TOK_LE;
        else if (op1 == TOK_GT)
            op1 = TOK_GE;
        else if (op1 == TOK_ULT)
            op1 = TOK_ULE;
        else if (op1 == TOK_UGT)
            op1 = TOK_UGE;
        a = 0;
        b = 0;
        gen_op(op1);
        if (op == TOK_NE) {
            b = gvtst(0, 0);
        } else {
            a = gvtst(1, 0);
            if (op != TOK_EQ) {
                /* generate non equal test */
                vpushi(0);
                vset_VT_CMP(TOK_NE);
                b = gvtst(0, 0);
            }
        }
        /* compare low. `Always` unsigned */
        op1 = op;
        if (op1 == TOK_LT)
            op1 = TOK_ULT;
        else if (op1 == TOK_LE)
            op1 = TOK_ULE;
        else if (op1 == TOK_GT)
            op1 = TOK_UGT;
        else if (op1 == TOK_GE)
            op1 = TOK_UGE;
        gen_op(op1);
        gvtst_set(1, a);
        gvtst_set(0, b);
        break;
    }
}
#endif

/* normalize values */
static uint64_t value64(uint64_t l1, int t)
{
    if ((t & VT_BTYPE) == VT_LLONG
        || (PTR_SIZE == 8 && (t & VT_BTYPE) == VT_PTR))
        return l1;
    else if (t & VT_UNSIGNED)
        return (uint32_t)l1;
    else
        return (uint32_t)l1 | -(l1 & 0x80000000);
}

static uint64_t gen_opic_sdiv(uint64_t a, uint64_t b)
{
    uint64_t x = (a >> 63 ? -a : a) / (b >> 63 ? -b : b);
    return (a ^ b) >> 63 ? -x : x;
}

static int gen_opic_lt(uint64_t a, uint64_t b)
{
    return (a ^ (uint64_t)1 << 63) < (b ^ (uint64_t)1 << 63);
}

/* handle integer constant optimizations and various machine
   independent opt */
static void gen_opic(int op)
{
    SValue *v1 = vtop - 1;
    SValue *v2 = vtop;
    int t1 = v1->type.t & VT_BTYPE;
    int t2 = v2->type.t & VT_BTYPE;
    int c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    int c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    uint64_t l1 = c1 ? value64(v1->c.i, v1->type.t) : 0;
    uint64_t l2 = c2 ? value64(v2->c.i, v2->type.t) : 0;
    int shm = (t1 == VT_LLONG) ? 63 : 31;
    int r;

    if (c1 && c2) {
        switch(op) {
        case '+': l1 += l2; break;
        case '-': l1 -= l2; break;
        case '&': l1 &= l2; break;
        case '^': l1 ^= l2; break;
        case '|': l1 |= l2; break;
        case '*': l1 *= l2; break;

        case TOK_PDIV:
        case '/':
        case '%':
        case TOK_UDIV:
        case TOK_UMOD:
            /* if division by zero, generate explicit division */
            if (l2 == 0) {
                if (CONST_WANTED && !NOEVAL_WANTED)
                    tcc_error("division by zero in constant");
                goto general_case;
            }
            switch(op) {
            default: l1 = gen_opic_sdiv(l1, l2); break;
            case '%': l1 = l1 - l2 * gen_opic_sdiv(l1, l2); break;
            case TOK_UDIV: l1 = l1 / l2; break;
            case TOK_UMOD: l1 = l1 % l2; break;
            }
            break;
        case TOK_SHL: l1 <<= (l2 & shm); break;
        case TOK_SHR: l1 >>= (l2 & shm); break;
        case TOK_SAR:
            l1 = (l1 >> 63) ? ~(~l1 >> (l2 & shm)) : l1 >> (l2 & shm);
            break;
            /* tests */
        case TOK_ULT: l1 = l1 < l2; break;
        case TOK_UGE: l1 = l1 >= l2; break;
        case TOK_EQ: l1 = l1 == l2; break;
        case TOK_NE: l1 = l1 != l2; break;
        case TOK_ULE: l1 = l1 <= l2; break;
        case TOK_UGT: l1 = l1 > l2; break;
        case TOK_LT: l1 = gen_opic_lt(l1, l2); break;
        case TOK_GE: l1 = !gen_opic_lt(l1, l2); break;
        case TOK_LE: l1 = !gen_opic_lt(l2, l1); break;
        case TOK_GT: l1 = gen_opic_lt(l2, l1); break;
            /* logical */
        case TOK_LAND: l1 = l1 && l2; break;
        case TOK_LOR: l1 = l1 || l2; break;
        default:
            goto general_case;
        }
        v1->c.i = value64(l1, v1->type.t);
        v1->r |= v2->r & VT_NONCONST;
        vtop--;
    } else {
        /* if commutative ops, put c2 as constant */
        if (c1 && (op == '+' || op == '&' || op == '^' ||
                   op == '|' || op == '*' || op == TOK_EQ || op == TOK_NE)) {
            vswap();
            c2 = c1; //c = c1, c1 = c2, c2 = c;
            l2 = l1; //l = l1, l1 = l2, l2 = l;
        }
        if (c1 && ((l1 == 0 &&
                    (op == TOK_SHL || op == TOK_SHR || op == TOK_SAR)) ||
                   (l1 == -1 && op == TOK_SAR))) {
            /* treat (0 << x), (0 >> x) and (-1 >> x) as constant */
            vpop();
        } else if (c2 && ((l2 == 0 && (op == '&' || op == '*')) ||
                          (op == '|' &&
                            (l2 == -1 || (l2 == 0xFFFFFFFF && t2 != VT_LLONG))) ||
                          (l2 == 1 && (op == '%' || op == TOK_UMOD)))) {
            /* treat (x & 0), (x * 0), (x | -1) and (x % 1) as constant */
            if (l2 == 1)
                vtop->c.i = 0;
            vswap();
            vtop--;
        } else if (c2 && (((op == '*' || op == '/' || op == TOK_UDIV ||
                          op == TOK_PDIV) &&
                           l2 == 1) ||
                          ((op == '+' || op == '-' || op == '|' || op == '^' ||
                            op == TOK_SHL || op == TOK_SHR || op == TOK_SAR) &&
                           l2 == 0) ||
                          (op == '&' &&
                            (l2 == -1 || (l2 == 0xFFFFFFFF && t2 != VT_LLONG))))) {
            /* filter out `NOP` operations like x*1, x-0, x&-1... */
            vtop--;
        } else if (c2 && (op == '*' || op == TOK_PDIV || op == TOK_UDIV || op == TOK_UMOD)) {
            /* try to use shifts instead of muls or divs */
            if (l2 > 0 && (l2 & (l2 - 1)) == 0) {
                int n = -1;
                if (op == TOK_UMOD) {
                    vtop->c.i = l2 - 1;
                    op = '&';
                    goto general_case;
                }
                while (l2) {
                    l2 >>= 1;
                    n++;
                }
                vtop->c.i = n;
                if (op == '*')
                    op = TOK_SHL;
                else if (op == TOK_PDIV)
                    op = TOK_SAR;
                else
                    op = TOK_SHR;
            }
            goto general_case;
        } else if (c2 && (op == '+' || op == '-') &&
                   (r = vtop[-1].r & (VT_VALMASK | VT_LVAL | VT_SYM),
                    r == (VT_CONST | VT_SYM) || r == VT_LOCAL)) {
            /* symbol + constant case */
            if (op == '-')
                l2 = -l2;
	    l2 += vtop[-1].c.i;
	    /* `The` backends can't always deal with addends to symbols
	       larger than +-1<<31.  `Don`'t construct such.  */
	    if ((int)l2 != l2)
	        goto general_case;
            vtop--;
            vtop->c.i = l2;
        } else {
        general_case:
                /* call low level op generator */
                if (t1 == VT_LLONG || t2 == VT_LLONG ||
                    (PTR_SIZE == 8 && (t1 == VT_PTR || t2 == VT_PTR)))
                    gen_opl(op);
                else
                    gen_opi(op);
        }
        if (vtop->r == VT_CONST)
            vtop->r |= VT_NONCONST; /* is const, but only by optimization */
    }
}

#if defined TCC_TARGET_X86_64 || defined TCC_TARGET_I386 || defined TCC_TARGET_ARM64
# define gen_negf gen_opf
#elif defined TCC_TARGET_ARM
void gen_negf(int op)
{
    /* arm will detect 0-x and replace by vneg */
    vpushi(0), vswap(), gen_op('-');
}
#else
/* `XXX`: implement in `gen_opf`() for other backends too */
void gen_negf(int op)
{
    /* `In` `IEEE` negate(x) isn't subtract(0,x).  `Without` `NaNs` it's
       subtract(-0, x), but with them it's really a sign flip
       operation.  `We` implement this with bit manipulation and have
       to do some type reinterpretation for this, which `TPC` can do
       only via memory.  */
    int align, size, bt;
    size = type_size(&vtop->type, &align);
    bt = vtop->type.t & VT_BTYPE;
#if defined TCC_TARGET_X86_64 || defined TCC_TARGET_I386
    /* sizeof long double is 12 or 16 here, but it's really the 80bit
       extended float format.  */
    if (bt == VT_LDOUBLE)
        size = 10;
#endif
    if (nocode_wanted) /* `save_reg`() wont work */
        goto gv2;
    save_reg(gv(RC_TYPE(bt)));
    vdup();
    incr_bf_adr(size - 1);
    vdup();
    vpushi(0x80); /* flip sign */
    gen_op('^');
    vstore();
    vpop();
gv2:
    gv(RC_TYPE(bt)); /* -n is not a lvalue */
}
#endif

/* generate a floating point operation with constant propagation */
static void gen_opif(int op)
{
    int c1, c2, i, bt;
    SValue *v1, *v2;
#if defined _MSC_VER && defined __x86_64__
    /* avoid bad optimization with f1 -= f2 for f1:-0.0, f2:0.0 */
    volatile
#endif
    long double f1, f2;

    v1 = vtop - 1;
    v2 = vtop;
    if ((v1->type.t & (VT_COMPLEX | VT_IMAGINARY)) || (v2->type.t & (VT_COMPLEX | VT_IMAGINARY))) {
        gen_op_complex(op);
        return;
    }
    if (op == TOK_NEG)
        v1 = v2;
    bt = v1->type.t & VT_BTYPE;

    /* currently, we cannot do computations with forward symbols */
    c1 = (v1->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    c2 = (v2->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
    if (c1 && c2) {
        if (bt == VT_FLOAT) {
            f1 = v1->c.f;
            f2 = v2->c.f;
        } else if (bt == VT_DOUBLE) {
            f1 = v1->c.d;
            f2 = v2->c.d;
        } else {
            f1 = v1->c.ld;
            f2 = v2->c.ld;
        }
        /* `NOTE`: we only do constant propagation if finite number (not
           `NaN` or infinity) (`ANSI` spec) */
        if (!(ieee_finite(f1) || !ieee_finite(f2)) && !CONST_WANTED)
            goto general_case;
        switch(op) {
        case '+': f1 += f2; break;
        case '-': f1 -= f2; break;
        case '*': f1 *= f2; break;
        case '/':
            if (f2 == 0.0) {
                union { float f; unsigned u; } x1, x2, y;
		/* `If` not in initializer we need to potentially generate
		   `FP` exceptions at runtime, otherwise we want to fold.  */
                if (!CONST_WANTED)
                    goto general_case;
                /* the run-time result of 0.0/0.0 on x87, also of other compilers
                   when used to compile the f1 /= f2 below, would be -nan */
                x1.f = f1, x2.f = f2;
                if (f1 == 0.0)
                    y.u = 0x7fc00000; /* nan */
                else
                    y.u = 0x7f800000; /* infinity */
                y.u |= (x1.u ^ x2.u) & 0x80000000; /* set sign */
                f1 = y.f;
                break;
            }
            f1 /= f2;
            break;
        case TOK_NEG:
            f1 = -f1;
            goto unary_result;
        case TOK_EQ:
            i = f1 == f2;
	make_int:
            vtop -= 2;
            vpushi(i);
            return;
        case TOK_NE:
            i = f1 != f2;
	    goto make_int;
        case TOK_LT:
            i = f1 < f2;
	    goto make_int;
        case TOK_GE:
            i = f1 >= f2;
	    goto make_int;
        case TOK_LE:
            i = f1 <= f2;
	    goto make_int;
        case TOK_GT:
            i = f1 > f2;
	    goto make_int;
        default:
            goto general_case;
        }
        vtop--;
    unary_result:
        /* `XXX`: overflow test ? */
        if (bt == VT_FLOAT) {
            v1->c.f = f1;
        } else if (bt == VT_DOUBLE) {
            v1->c.d = f1;
        } else {
            v1->c.ld = f1;
        }
    } else {
    general_case:
        if (op == TOK_NEG) {
            gen_negf(op);
        } else {
            gen_opf(op);
        }
    }
}

/* print a type. `If` 'varstr' is not `NULL`, then the variable is also
   printed in the type */
/* `XXX`: union */
/* `XXX`: add array and function pointers */
static void type_to_str(char *buf, int buf_size,
                 CType *type, const char *varstr)
{
    int bt, v, t;
    Sym *s, *sa;
    char buf1[256];
    const char *tstr;

    t = type->t;
    bt = t & VT_BTYPE;
    buf[0] = '\0';

    if (t & VT_STORAGE_EXTERN)
        pstrcat(buf, buf_size, "extern ");
    if (t & VT_STORAGE_STATIC)
        pstrcat(buf, buf_size, "static ");
    if (t & VT_STORAGE_TYPEDEF)
        pstrcat(buf, buf_size, "typedef ");
    if (t & VT_STORAGE_INLINE)
        pstrcat(buf, buf_size, "inline ");
    if (bt != VT_PTR) {
        if (t & VT_QUALIFIER_RESTRICT)
            pstrcat(buf, buf_size, "restrict ");
        if (t & VT_QUALIFIER_VOLATILE)
            pstrcat(buf, buf_size, "volatile ");
        if (t & VT_QUALIFIER_CONSTANT)
            pstrcat(buf, buf_size, "const ");
        if (t & VT_QUALIFIER_IMMUTABLE)
            pstrcat(buf, buf_size, "@immutable ");
    }
    if (((t & VT_DEFSIGN) && bt == VT_BYTE)
        || ((t & VT_UNSIGNED)
            && (bt == VT_SHORT || bt == VT_INT || bt == VT_LLONG)
            && !IS_ENUM(t)
            ))
        pstrcat(buf, buf_size, (t & VT_UNSIGNED) ? "unsigned " : "signed ");

    buf_size -= strlen(buf);
    buf += strlen(buf);

    switch(bt) {
    case VT_AUTO: {
        CType *tv = type_value_of(type->ref);
        if (tv) {
            type_to_str(buf, buf_size, tv, varstr);
            return;
        }
        tstr = "symbol";
        goto add_tstr;
    }
    case VT_VOID:
        tstr = "void";
        goto add_tstr;
    case VT_BOOL:
        tstr = "_Bool";
        goto add_tstr;
    case VT_BYTE:
        tstr = "char";
        goto add_tstr;
    case VT_SHORT:
        tstr = "short";
        goto add_tstr;
    case VT_INT:
        tstr = "int";
        goto maybe_long;
    case VT_LLONG:
        tstr = "long long";
    maybe_long:
        if (t & VT_LONG)
            tstr = "long";
        if (!IS_ENUM(t))
            goto add_tstr;
        tstr = "enum ";
        goto tstruct;
    case VT_FLOAT:
        tstr = "float";
        goto add_tstr;
    case VT_DOUBLE:
        tstr = "double";
        if (!(t & VT_LONG))
            goto add_tstr;
    case VT_LDOUBLE:
        tstr = "long double";
    add_tstr:
        pstrcat(buf, buf_size, tstr);
        break;
    case VT_STRUCT:
        if (is_tuple_type(type)) {
            Sym *f;
            pstrcat(buf, buf_size, "(");
            for (f = type->ref->next; f; f = f->next) {
                char buf2[256];
                type_to_str(buf2, sizeof(buf2), &f->type, NULL);
                pstrcat(buf, buf_size, buf2);
                if (f->next)
                    pstrcat(buf, buf_size, ", ");
            }
            if (type->ref->next && !type->ref->next->next)
                pstrcat(buf, buf_size, ",");
            pstrcat(buf, buf_size, ")");
            break;
        }
        if (is_slice_type(type) && type->ref->next && type->ref->next->next) {
            CType *elem = &type->ref->next->next->type.ref->type;
            if (varstr && '*' == *varstr)
                snprintf(buf1, sizeof(buf1), "(%s)[..]", varstr);
            else
                snprintf(buf1, sizeof(buf1), "%s[..]", varstr ? varstr : "");
            type_to_str(buf, buf_size, elem, buf1);
            goto no_var;
        }
        tstr = "struct ";
        if (IS_UNION(t))
            tstr = "union ";
    tstruct:
        pstrcat(buf, buf_size, tstr);
        v = type->ref->v & ~SYM_STRUCT;
        if (v >= SYM_FIRST_ANOM)
            pstrcat(buf, buf_size, "<anonymous>");
        else
            pstrcat(buf, buf_size, get_tok_str(v, NULL));
        break;
    case VT_FUNC:
        s = type->ref;
        buf1[0]=0;
        if (varstr && '*' == *varstr) {
            pstrcat(buf1, sizeof(buf1), "(");
            pstrcat(buf1, sizeof(buf1), varstr);
            pstrcat(buf1, sizeof(buf1), ")");
        }
        pstrcat(buf1, sizeof(buf1), "(");
        sa = s->next;
        while (sa != NULL) {
            char buf2[256];
            type_to_str(buf2, sizeof(buf2), &sa->type, NULL);
            pstrcat(buf1, sizeof(buf1), buf2);
            sa = sa->next;
            if (sa)
                pstrcat(buf1, sizeof(buf1), ", ");
        }
        if (s->f.func_type == FUNC_ELLIPSIS)
            pstrcat(buf1, sizeof(buf1), ", ...");
        pstrcat(buf1, sizeof(buf1), ")");
        type_to_str(buf, buf_size, &s->type, buf1);
        goto no_var;
    case VT_PTR:
        s = type->ref;
        if (t & (VT_ARRAY|VT_VLA)) {
            if (varstr && '*' == *varstr)
                snprintf(buf1, sizeof(buf1), "(%s)[%d]", varstr, s->c);
            else
                snprintf(buf1, sizeof(buf1), "%s[%d]", varstr ? varstr : "", s->c);
            type_to_str(buf, buf_size, &s->type, buf1);
            goto no_var;
        }
        pstrcpy(buf1, sizeof(buf1), "*");
        if (t & VT_QUALIFIER_RESTRICT)
            pstrcat(buf1, sizeof(buf1), "restrict ");
        if (t & VT_QUALIFIER_CONSTANT)
            pstrcat(buf1, sizeof(buf1), "const ");
        if (t & VT_QUALIFIER_IMMUTABLE)
            pstrcat(buf1, sizeof(buf1), "@immutable ");
        if (t & VT_QUALIFIER_VOLATILE)
            pstrcat(buf1, sizeof(buf1), "volatile ");
        if (varstr)
            pstrcat(buf1, sizeof(buf1), varstr);
        type_to_str(buf, buf_size, &s->type, buf1);
        goto no_var;
    }
    if (varstr) {
        pstrcat(buf, buf_size, " ");
        pstrcat(buf, buf_size, varstr);
    }
 no_var: ;
}

static void type_incompatibility_error(CType* st, CType* dt, const char* fmt)
{
    char buf1[256], buf2[256];
    type_to_str(buf1, sizeof(buf1), st, NULL);
    type_to_str(buf2, sizeof(buf2), dt, NULL);
    tcc_error(fmt, buf1, buf2);
}

static void type_incompatibility_warning(CType* st, CType* dt, const char* fmt)
{
    char buf1[256], buf2[256];
    type_to_str(buf1, sizeof(buf1), st, NULL);
    type_to_str(buf2, sizeof(buf2), dt, NULL);
    tcc_warning(fmt, buf1, buf2);
}

static int pointed_size(CType *type)
{
    int align;
    return type_size(pointed_type(type), &align);
}

static inline int is_null_pointer(SValue *p)
{
    if ((p->r & (VT_VALMASK | VT_LVAL | VT_SYM | VT_NONCONST)) != VT_CONST)
        return 0;
    return ((p->type.t & VT_BTYPE) == VT_INT && (uint32_t)p->c.i == 0) ||
        ((p->type.t & VT_BTYPE) == VT_LLONG && p->c.i == 0) ||
        ((p->type.t & VT_BTYPE) == VT_PTR &&
         (PTR_SIZE == 4 ? (uint32_t)p->c.i == 0 : p->c.i == 0) &&
         ((pointed_type(&p->type)->t & VT_BTYPE) == VT_VOID) &&
         0 == (pointed_type(&p->type)->t & VT_QUALIFIER)
         );
}

/* compare function types. `OLD` functions match any new functions */
static int is_compatible_func(CType *type1, CType *type2)
{
    Sym *s1, *s2;

    s1 = type1->ref;
    s2 = type2->ref;
    if (s1->f.func_call != s2->f.func_call)
        return 0;
    if (s1->f.func_type != s2->f.func_type
        && s1->f.func_type != FUNC_OLD
        && s2->f.func_type != FUNC_OLD)
        return 0;
    for (;;) {
        if (!is_compatible_unqualified_types(&s1->type, &s2->type))
            return 0;
        if (s1->f.func_type == FUNC_OLD || s2->f.func_type == FUNC_OLD )
            return 1;
        s1 = s1->next;
        s2 = s2->next;
        if (!s1)
            return !s2;
        if (!s2)
            return 0;
    }
}

/* return true if type1 and type2 are the same.  `If` unqualified is
   true, qualifiers on the types are ignored.
 */
static int compare_types(CType *type1, CType *type2, int unqualified)
{
    int bt1, t1, t2;

    if (IS_ENUM(type1->t)) {
        if (IS_ENUM(type2->t))
            return type1->ref == type2->ref;
        type1 = &type1->ref->type;
    } else if (IS_ENUM(type2->t))
        type2 = &type2->ref->type;

    t1 = type1->t & VT_TYPE;
    t2 = type2->t & VT_TYPE;
    if (unqualified) {
        /* strip qualifiers before comparing */
        t1 &= ~VT_QUALIFIER;
        t2 &= ~VT_QUALIFIER;
    }

    /* `Default` `Vs` explicit signedness only matters for char */
    if ((t1 & VT_BTYPE) != VT_BYTE) {
        t1 &= ~VT_DEFSIGN;
        t2 &= ~VT_DEFSIGN;
    }
    /* `XXX`: bitfields ? */
    if (t1 != t2)
        return 0;

    if ((t1 & VT_ARRAY)
        && !(type1->ref->c < 0
          || type2->ref->c < 0
          || type1->ref->c == type2->ref->c))
            return 0;

    /* test more complicated cases */
    bt1 = t1 & VT_BTYPE;
    if (bt1 == VT_PTR) {
        type1 = pointed_type(type1);
        type2 = pointed_type(type2);
        if (unqualified && (t1 & VT_ARRAY))
            return compare_types(type1, type2, 1);
        return is_compatible_types(type1, type2);
    } else if (bt1 == VT_STRUCT) {
        Sym *s1 = type1->ref, *s2 = type2->ref;
        if (s1 == s2)
            return 1;
        if (s1 && s2 && s1->a.is_tuple && s2->a.is_tuple) {
            Sym *f1 = s1->next, *f2 = s2->next;
            for (; f1 && f2; f1 = f1->next, f2 = f2->next)
                if (!compare_types(&f1->type, &f2->type, unqualified))
                    return 0;
            return f1 == NULL && f2 == NULL;
        }
        if (s1 && s2 && (s1->a.is_tuple || s2->a.is_tuple))
            return 0;
        /* `Slice` types are represented as anonymous structs.  `Compare` their
           element types instead of requiring the same struct symbol.  */
        if (s1 && s2 && s1->a.is_slice && s2->a.is_slice
            && s1->next && s1->next->next && s2->next && s2->next->next
            && (s1->next->next->type.t & VT_BTYPE) == VT_PTR
            && (s2->next->next->type.t & VT_BTYPE) == VT_PTR) {
            CType *pt1 = pointed_type(&s1->next->next->type);
            CType *pt2 = pointed_type(&s2->next->next->type);
            return compare_types(pt1, pt2, unqualified);
        }
        return 0;
    } else if (bt1 == VT_FUNC) {
        return is_compatible_func(type1, type2);
    } else {
        return 1;
    }
}
static int can_compare_array_or_slice_content(CType *type)
{
    if (is_slice_type(type))
        return 1;
    if (type->t & (VT_ARRAY | VT_VLA)) {
        if (type->ref && type->ref->c > 0)
            return 1;
    }
     return 0;
 }
 static void get_array_or_slice_elem(CType *type, CType *elem)
 {
    if (type->t & (VT_ARRAY | VT_VLA))
        *elem = type->ref->type;
    else if (is_slice_type(type))
        *elem = *pointed_type(&type->ref->next->next->type);
    else
        elem->t = VT_VOID;
}

#define CMP_OP 'C'
#define SHIFT_OP 'S'

/* `Check` if `OP1` and `OP2` can be "combined" with operation `OP`, the combined
   type is stored in `DEST` if non-null (except for pointer plus/minus) . */
static int combine_types(CType *dest, SValue *op1, SValue *op2, int op)
{
    CType *type1, *type2, type;
    int t1, t2, bt1, bt2;
    int ret = 1;

    /* for shifts, 'combine' only left operand */
    if (op == SHIFT_OP)
        op2 = op1;

    type1 = &op1->type, type2 = &op2->type;
    t1 = type1->t, t2 = type2->t;
    bt1 = t1 & VT_BTYPE, bt2 = t2 & VT_BTYPE;

    type.t = VT_VOID;
    type.ref = NULL;

     if ((t1 & (VT_COMPLEX | VT_IMAGINARY)) || (t2 & (VT_COMPLEX | VT_IMAGINARY))) {
         int comb_bt = (bt1 > bt2 ? bt1 : bt2);
         int has_complex = (t1 & VT_COMPLEX) || (t2 & VT_COMPLEX);
         int has_imag = (t1 & VT_IMAGINARY) || (t2 & VT_IMAGINARY);
         int is_real1 = !(t1 & (VT_COMPLEX | VT_IMAGINARY));
         int is_real2 = !(t2 & (VT_COMPLEX | VT_IMAGINARY));

         if (op == CMP_OP) {
             type.t = VT_INT;
         } else if (has_complex) {
             type.t = comb_bt | VT_COMPLEX;
         } else if ((is_real1 && has_imag) || (is_real2 && has_imag)) {
             if (op == '+' || op == '-') {
                 type.t = comb_bt | VT_COMPLEX;
             } else {
                 type.t = comb_bt | VT_IMAGINARY;
             }
         } else {
             if (op == '+' || op == '-') {
                 type.t = comb_bt | VT_IMAGINARY;
             } else if (op == '*' || op == '/') {
                 type.t = comb_bt;
             } else {
                 type.t = comb_bt | VT_IMAGINARY;
             }
         }
     } else if (bt1 == VT_VOID || bt2 == VT_VOID) {
         if (op != '?')
             tcc_error("operation on void value");
         type.t = VT_VOID;
     } else if (op == CMP_OP && (is_slice_type(type1) || is_slice_type(type2)) && can_compare_array_or_slice_content(type1) && can_compare_array_or_slice_content(type2)) {
         CType elem1, elem2;
         get_array_or_slice_elem(type1, &elem1);
         get_array_or_slice_elem(type2, &elem2);
         if (!is_compatible_unqualified_types(&elem1, &elem2))
             ret = 0;
         if ((type1->t & VT_ARRAY) && (type2->t & VT_ARRAY)
             && type1->ref->c > 0 && type2->ref->c > 0
             && type1->ref->c != type2->ref->c)
             ret = 0;
         type.t = VT_INT;
     } else if (bt1 == VT_PTR || bt2 == VT_PTR) {
        if (op == '+') {
          if (!is_integer_btype(bt1 == VT_PTR ? bt2 : bt1))
            ret = 0;
        }
        /* http://port70.net/~nsz/c/c99/n1256.html#6.5.15p6 */
        /* `If` one is a null ptr constant the result type is the other.  */
        else if (is_null_pointer (op2)) type = *type1;
        else if (is_null_pointer (op1)) type = *type2;
        else if (bt1 != bt2) {
            /* accept comparison or cond-expr between pointer and integer
               with a warning */
            if ((op == '?' || op == CMP_OP)
                && (is_integer_btype(bt1) || is_integer_btype(bt2)))
              tcc_warning("pointer/integer mismatch in %s",
                          op == '?' ? "conditional expression" : "comparison");
            else if (op != '-' || !is_integer_btype(bt2))
              ret = 0;
            type = *(bt1 == VT_PTR ? type1 : type2);
        } else {
            CType *pt1 = pointed_type(type1);
            CType *pt2 = pointed_type(type2);
            int pbt1 = pt1->t & VT_BTYPE;
            int pbt2 = pt2->t & VT_BTYPE;
            int newquals, copied = 0;
            if (pbt1 != VT_VOID && pbt2 != VT_VOID
                && !compare_types(pt1, pt2, 1/*unqualif*/)) {
                if (op != '?' && op != CMP_OP)
                  ret = 0;
                else
                  type_incompatibility_warning(type1, type2,
                    op == '?'
                     ? "pointer type mismatch in conditional expression ('%s' and '%s')"
                     : "pointer type mismatch in comparison('%s' and '%s')");
            }
            if (op == '?') {
                /* pointers to void get preferred, otherwise the
                   pointed to types minus qualifs should be compatible */
                type = *((pbt1 == VT_VOID) ? type1 : type2);
                /* combine qualifs */
                newquals = type_qualifiers(pt1) | type_qualifiers(pt2);
                if ((~type_qualifiers(pointed_type(&type))) & newquals)
                  {
                    /* copy the pointer target symbol */
                    type.ref = sym_push(SYM_FIELD, &type.ref->type,
                                        0, type.ref->c);
                    copied = 1;
                    parse_btype_qualify(pointed_type(&type), newquals);
                  }
                /* pointers to incomplete arrays get converted to
                   pointers to completed ones if possible */
                if (pt1->t & VT_ARRAY
                    && pt2->t & VT_ARRAY
                    && pointed_type(&type)->ref->c < 0
                    && (pt1->ref->c > 0 || pt2->ref->c > 0))
                  {
                    if (!copied)
                      type.ref = sym_push(SYM_FIELD, &type.ref->type,
                                          0, type.ref->c);
                    pointed_type(&type)->ref =
                        sym_push(SYM_FIELD, &pointed_type(&type)->ref->type,
                                 0, pointed_type(&type)->ref->c);
                    pointed_type(&type)->ref->c =
                        0 < pt1->ref->c ? pt1->ref->c : pt2->ref->c;
                  }
            }
        }
        if (op == CMP_OP)
          type.t = VT_SIZE_T;
    } else if (bt1 == VT_STRUCT || bt2 == VT_STRUCT) {
        if (op != '?' || !compare_types(type1, type2, 1))
          ret = 0;
        type = *type1;
    } else if (is_float(bt1) || is_float(bt2)) {
        if (bt1 == VT_LDOUBLE || bt2 == VT_LDOUBLE) {
            type.t = VT_LDOUBLE;
        } else if (bt1 == VT_DOUBLE || bt2 == VT_DOUBLE) {
            type.t = VT_DOUBLE;
        } else {
            type.t = VT_FLOAT;
        }
    } else if (bt1 == VT_LLONG || bt2 == VT_LLONG) {
        /* cast to biggest op */
        type.t = VT_LLONG | VT_LONG;
        if (bt1 == VT_LLONG)
          type.t &= t1;
        if (bt2 == VT_LLONG)
          type.t &= t2;
        /* convert to unsigned if it does not fit in a long long */
        if ((t1 & (VT_BTYPE | VT_UNSIGNED)) == (VT_LLONG | VT_UNSIGNED) ||
            (t2 & (VT_BTYPE | VT_UNSIGNED)) == (VT_LLONG | VT_UNSIGNED))
          type.t |= VT_UNSIGNED;
    } else {
        /* integer operations */
        type.t = VT_INT | (VT_LONG & (t1 | t2));
        /* convert to unsigned if it does not fit in an integer */
        if (((t1 & (VT_BTYPE | VT_UNSIGNED)) == (VT_INT | VT_UNSIGNED)
                && (!(t1 & VT_BITFIELD) || BIT_SIZE(t1) == 32))
         || ((t2 & (VT_BTYPE | VT_UNSIGNED)) == (VT_INT | VT_UNSIGNED)
                && (!(t2 & VT_BITFIELD) || BIT_SIZE(t2) == 32)))
          type.t |= VT_UNSIGNED;
    }
    if (dest)
      *dest = type;
    return ret;
}

static void gen_complex_part(int is_im)
{
    CType ct = vtop->type;
    int t = ct.t;
    int base_t = t & ~(VT_COMPLEX | VT_IMAGINARY);
    CType base_type = ct;
    base_type.t = base_t;
    int base_align, base_size = type_size(&base_type, &base_align);

    if (t & VT_COMPLEX) {
        if (!(vtop->r & VT_LVAL)) {
            int align, size = type_size(&ct, &align);
            int r2, loc = get_temp_local_var(size, align, &r2);
            vset(&ct, VT_LOCAL | VT_LVAL, loc);
            vtop->r2 = r2;
            vswap();
            vstore();
        }
        if (is_im) {
            if ((vtop->r & VT_VALMASK) == VT_LOCAL) {
                vtop->c.i += base_size;
            } else {
                gaddrof();
                vtop->type = char_pointer_type;
                vpushi(base_size);
                gen_op('+');
                indir();
            }
        }
        vtop->type = base_type;
        vtop->r |= VT_LVAL;
        vtop->r2 = VT_CONST;
    } else if (t & VT_IMAGINARY) {
        if (is_im) {
            vtop->type = base_type;
        } else {
            vpop();
            {
                CValue cval = {0};
                vsetc(&base_type, VT_CONST, &cval);
            }
        }
    } else {
        if (is_im) {
            vpop();
            {
                CValue cval = {0};
                vsetc(&base_type, VT_CONST, &cval);
            }
        } else {
            vtop->type = base_type;
        }
    }
}

static void gen_op_complex(int op)
{
    CType combtype;
    if (!combine_types(&combtype, vtop - 1, vtop, op))
        tcc_error("invalid operand types for complex binary operation");

    if (op == TOK_EQ || op == TOK_NE) {
        CType t1 = vtop[-1].type, t2 = vtop[0].type;
        int align1, size1 = type_size(&t1, &align1);
        int r2_1, loc1 = get_temp_local_var(size1, align1, &r2_1);
        int align2, size2 = type_size(&t2, &align2);
        int r2_2, loc2 = get_temp_local_var(size2, align2, &r2_2);

        vset(&t2, VT_LOCAL | VT_LVAL, loc2); vtop->r2 = r2_2;
        vswap(); vstore(); vpop();
        vset(&t1, VT_LOCAL | VT_LVAL, loc1); vtop->r2 = r2_1;
        vswap(); vstore(); vpop();

        vset(&t1, VT_LOCAL | VT_LVAL, loc1); vtop->r2 = r2_1; gen_complex_part(0);
        vset(&t2, VT_LOCAL | VT_LVAL, loc2); vtop->r2 = r2_2; gen_complex_part(0);
        gen_op(TOK_EQ);

        vset(&t1, VT_LOCAL | VT_LVAL, loc1); vtop->r2 = r2_1; gen_complex_part(1);
        vset(&t2, VT_LOCAL | VT_LVAL, loc2); vtop->r2 = r2_2; gen_complex_part(1);
        gen_op(TOK_EQ);

        gen_op(TOK_LAND);
        if (op == TOK_NE) {
            vpushi(0);
            gen_op(TOK_EQ);
        }
        return;
    }

    CType t1 = vtop[-1].type, t2 = vtop[0].type;
    int align1, size1 = type_size(&t1, &align1);
    int r2_1, loc1 = get_temp_local_var(size1, align1, &r2_1);
    int align2, size2 = type_size(&t2, &align2);
    int r2_2, loc2 = get_temp_local_var(size2, align2, &r2_2);

    vset(&t2, VT_LOCAL | VT_LVAL, loc2); vtop->r2 = r2_2;
    vswap(); vstore(); vpop();
    vset(&t1, VT_LOCAL | VT_LVAL, loc1); vtop->r2 = r2_1;
    vswap(); vstore(); vpop();

    int r_align, r_size = type_size(&combtype, &r_align);
    int r_r2, r_loc = get_temp_local_var(r_size, r_align, &r_r2);

    int base_t = combtype.t & ~(VT_COMPLEX | VT_IMAGINARY);
    CType base_type; base_type.t = base_t; base_type.ref = NULL;

#define PUSH_PART(loc_id, r2_id, type_id, is_im_val) \
    vset(&type_id, VT_LOCAL | VT_LVAL, loc_id); vtop->r2 = r2_id; gen_complex_part(is_im_val); if (vtop->r & VT_LVAL) gv(RC_TYPE(vtop->type.t))

    if (op == '+' || op == '-') {
        PUSH_PART(loc1, r2_1, t1, 0);
        PUSH_PART(loc2, r2_2, t2, 0);
        gen_op(op);
        vset(&base_type, VT_LOCAL | VT_LVAL, r_loc);
        vswap(); vstore(); vpop();

        if (combtype.t & (VT_COMPLEX | VT_IMAGINARY)) {
            int base_align, base_sz = type_size(&base_type, &base_align);
            PUSH_PART(loc1, r2_1, t1, 1);
            PUSH_PART(loc2, r2_2, t2, 1);
            gen_op(op);
            vset(&base_type, VT_LOCAL | VT_LVAL, r_loc + base_sz);
            vswap(); vstore(); vpop();
        }
    } else if (op == '*') {
        PUSH_PART(loc1, r2_1, t1, 0);
        PUSH_PART(loc2, r2_2, t2, 0);
        gen_op('*');
        PUSH_PART(loc1, r2_1, t1, 1);
        PUSH_PART(loc2, r2_2, t2, 1);
        gen_op('*');
        gen_op('-');
        vset(&base_type, VT_LOCAL | VT_LVAL, r_loc);
        vswap(); vstore(); vpop();

        if (combtype.t & (VT_COMPLEX | VT_IMAGINARY)) {
            int base_align, base_sz = type_size(&base_type, &base_align);
            PUSH_PART(loc1, r2_1, t1, 0);
            PUSH_PART(loc2, r2_2, t2, 1);
            gen_op('*');
            PUSH_PART(loc1, r2_1, t1, 1);
            PUSH_PART(loc2, r2_2, t2, 0);
            gen_op('*');
            gen_op('+');
            vset(&base_type, VT_LOCAL | VT_LVAL, r_loc + base_sz);
            vswap(); vstore(); vpop();
        }
    } else if (op == '/') {
        PUSH_PART(loc2, r2_2, t2, 0); vdup(); gen_op('*');
        PUSH_PART(loc2, r2_2, t2, 1); vdup(); gen_op('*');
        gen_op('+');
        int den_align, den_sz = type_size(&base_type, &den_align);
        int den_r2, den_loc = get_temp_local_var(den_sz, den_align, &den_r2);
        vset(&base_type, VT_LOCAL | VT_LVAL, den_loc); vtop->r2 = den_r2;
        vswap(); vstore(); vpop();

        PUSH_PART(loc1, r2_1, t1, 0); PUSH_PART(loc2, r2_2, t2, 0); gen_op('*');
        PUSH_PART(loc1, r2_1, t1, 1); PUSH_PART(loc2, r2_2, t2, 1); gen_op('*');
        gen_op('+');
        vset(&base_type, VT_LOCAL | VT_LVAL, den_loc); vtop->r2 = den_r2; indir();
        gen_op('/');
        vset(&base_type, VT_LOCAL | VT_LVAL, r_loc);
        vswap(); vstore(); vpop();

        if (combtype.t & (VT_COMPLEX | VT_IMAGINARY)) {
            int base_align, base_sz = type_size(&base_type, &base_align);
            PUSH_PART(loc1, r2_1, t1, 1); PUSH_PART(loc2, r2_2, t2, 0); gen_op('*');
            PUSH_PART(loc1, r2_1, t1, 0); PUSH_PART(loc2, r2_2, t2, 1); gen_op('*');
            gen_op('-');
            vset(&base_type, VT_LOCAL | VT_LVAL, den_loc); vtop->r2 = den_r2; indir();
            gen_op('/');
        vset(&base_type, VT_LOCAL | VT_LVAL, r_loc + base_sz);
        vswap(); vstore(); vpop();
        }
    }

#undef PUSH_PART

    vset(&combtype, VT_LOCAL | VT_LVAL, r_loc);
    vtop->r2 = r_r2;
}

/* generic `gen_op`: handles types problems */
ST_FUNC void gen_op(int op)
{
    int t1, t2, bt1, bt2, t;
    CType type1, combtype;
    int op_class = op;

    if ((vtop[-1].type.t & (VT_COMPLEX | VT_IMAGINARY)) || (vtop[0].type.t & (VT_COMPLEX | VT_IMAGINARY))) {
        gen_op_complex(op);
        return;
    }

    if (op == TOK_SHR || op == TOK_SAR || op == TOK_SHL)
        op_class = SHIFT_OP;
    else if (TOK_ISCOND(op)) /* == != > ... */
        op_class = CMP_OP;

redo:
    t1 = vtop[-1].type.t;
    t2 = vtop[0].type.t;
    bt1 = t1 & VT_BTYPE;
    bt2 = t2 & VT_BTYPE;

    if (bt1 == VT_FUNC || bt2 == VT_FUNC) {
	if (bt2 == VT_FUNC) {
	    mk_pointer(&vtop->type);
	    gaddrof();
	}
	if (bt1 == VT_FUNC) {
	    vswap();
	    mk_pointer(&vtop->type);
	    gaddrof();
	    vswap();
	}
	goto redo;
     } else if (!combine_types(&combtype, vtop - 1, vtop, op_class)) {
 op_err:
         tcc_error("invalid operand types for binary operation");
     } else if ((op == TOK_EQ || op == TOK_NE) &&
              (is_slice_type(&vtop[-1].type) || is_slice_type(&vtop[0].type)) &&
              can_compare_array_or_slice_content(&vtop[-1].type) &&
              can_compare_array_or_slice_content(&vtop[0].type) &&
              !is_null_pointer(vtop) && !is_null_pointer(vtop - 1)) {
         CType elem, slice_type;
         int esize, ealign, la, lb;

         get_array_or_slice_elem(&vtop[-1].type, &elem);
         slice_type.t = VT_STRUCT;
         slice_type.ref = make_slice_type(&elem);
         esize = type_size(&elem, &ealign);

         /* `Right` operand is at vtop */
         if (is_slice_type(&vtop->type)) {
             CType re;
             get_array_or_slice_elem(&vtop->type, &re);
             if (!is_compatible_unqualified_types(&re, &elem))
                 cast_error(&vtop->type, &slice_type);
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
             CType le;
             get_array_or_slice_elem(&vtop->type, &le);
             if (!is_compatible_unqualified_types(&le, &elem))
                 cast_error(&vtop->type, &slice_type);
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
     } else if (op_class == CMP_OP &&
              is_slice_type(&vtop[-1].type) && is_slice_type(&vtop[0].type)) {
         tcc_error("relational operators not supported for arrays/slices");
     } else if (bt1 == VT_PTR || bt2 == VT_PTR) {
        /* relational op: must be both pointers */
        int align;
        if (op_class == CMP_OP)
            goto std_op;
        if (cur_scope->fn.safety == FUNC_SAFETY_SAFE)
            tcc_error("pointer arithmetic not allowed in @safe scope");
        else {
            if (cur_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                cur_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
            if (root_scope->fn.safety == FUNC_SAFETY_UNKNOWN)
                root_scope->fn.safety = FUNC_SAFETY_INFERRED_SYSTEM;
        }
        /* if both pointers, then it must be the '-' op */
        if (bt1 == VT_PTR && bt2 == VT_PTR) {
            if (op != '-')
                goto op_err;
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            vrott(3);
            gen_opic(op);
            vtop->type.t = VT_PTRDIFF_T;
            vswap();
            gen_op(TOK_PDIV);
        } else {
            /* exactly one pointer : must be '+' or '-'. */
            if (op != '-' && op != '+')
                goto op_err;
            /* `Put` pointer as first operand */
            if (bt2 == VT_PTR) {
                vswap();
                t = t1, t1 = t2, t2 = t;
                bt2 = bt1;
            }
#if PTR_SIZE == 4
            if (bt2 == VT_LLONG)
                /* `XXX`: truncate here because `gen_opl` can't handle ptr + long long */
                gen_cast_s(VT_INT);
#endif
            type1 = vtop[-1].type;
            vpush_type_size(pointed_type(&vtop[-1].type), &align);
            vtop->type.t &= ~VT_UNSIGNED;
            gen_op('*');
#ifdef CONFIG_TCC_BCHECK
            if (tcc_state->do_bounds_check && !CONST_WANTED
                && !no_ptr_bounds_check) {
                /* if bounded pointers, we generate a special code to
                   test bounds */
                if (op == '-') {
                    vpushi(0);
                    vswap();
                    gen_op('-');
                }
                gen_bounded_ptr_add();
            } else
#endif
            {
                gen_opic(op);
            }
            type1.t &= ~(VT_ARRAY|VT_VLA);
            /* put again type if `gen_opic`() swaped operands */
            vtop->type = type1;
        }
    } else {
        /* floats can only be used for a few operations */
        if (is_float(combtype.t)
            && op != '+' && op != '-' && op != '*' && op != '/'
            && op_class != CMP_OP) {
            goto op_err;
        }
    std_op:
        t = t2 = combtype.t;
        /* special case for shifts and long long: we keep the shift as
           an integer */
        if (op_class == SHIFT_OP)
            t2 = VT_INT;
        /* `XXX`: currently, some unsigned operations are explicit, so
           we modify them here */
        if (t & VT_UNSIGNED) {
            if (op == TOK_SAR)
                op = TOK_SHR;
            else if (op == '/')
                op = TOK_UDIV;
            else if (op == '%')
                op = TOK_UMOD;
            else if (op == TOK_LT)
                op = TOK_ULT;
            else if (op == TOK_GT)
                op = TOK_UGT;
            else if (op == TOK_LE)
                op = TOK_ULE;
            else if (op == TOK_GE)
                op = TOK_UGE;
        }
        vswap();
        gen_cast_s(t);
        vswap();
        gen_cast_s(t2);
        if (is_float(t))
            gen_opif(op);
        else
            gen_opic(op);
        if (op_class == CMP_OP) {
            /* relational op: the result is an int */
            vtop->type.t = VT_INT;
        } else {
            vtop->type.t = t;
        }
    }
    // Make sure that we have converted to an rvalue:
    if (vtop->r & VT_LVAL)
        gv(RC_TYPE(vtop->type.t));
}

#if defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64 || defined TCC_TARGET_ARM
#define gen_cvt_itof1 gen_cvt_itof
#else
/* generic itof for unsigned long long case */
static void gen_cvt_itof1(int t)
{
    if ((vtop->type.t & (VT_BTYPE | VT_UNSIGNED)) ==
        (VT_LLONG | VT_UNSIGNED)) {

        if (t == VT_FLOAT)
            vpush_helper_func(TOK___floatundisf);
#if LDOUBLE_SIZE != 8
        else if (t == VT_LDOUBLE)
            vpush_helper_func(TOK___floatundixf);
#endif
        else
            vpush_helper_func(TOK___floatundidf);
        vrott(2);
        gfunc_call(1);
        vpushi(0);
        PUT_R_RET(vtop, t);
    } else {
        gen_cvt_itof(t);
    }
}
#endif

#if defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
#define gen_cvt_ftoi1 gen_cvt_ftoi
#else
/* generic ftoi for unsigned long long case */
static void gen_cvt_ftoi1(int t)
{
    int st;
    if (t == (VT_LLONG | VT_UNSIGNED)) {
        /* not handled natively */
        st = vtop->type.t & VT_BTYPE;
        if (st == VT_FLOAT)
            vpush_helper_func(TOK___fixunssfdi);
#if LDOUBLE_SIZE != 8
        else if (st == VT_LDOUBLE)
            vpush_helper_func(TOK___fixunsxfdi);
#endif
        else
            vpush_helper_func(TOK___fixunsdfdi);
        vrott(2);
        gfunc_call(1);
        vpushi(0);
        PUT_R_RET(vtop, t);
    } else {
        gen_cvt_ftoi(t);
    }
}
#endif

/* special delayed cast for char/short */
static void force_charshort_cast(void)
{
    int sbt = BFGET(vtop->r, VT_MUSTCAST) == 2 ? VT_LLONG : VT_INT;
    int dbt = vtop->type.t;
    vtop->r &= ~VT_MUSTCAST;
    vtop->type.t = sbt;
    gen_cast_s(dbt == VT_BOOL ? VT_BYTE|VT_UNSIGNED : dbt);
    vtop->type.t = dbt;
}

static void gen_cast_s(int t)
{
    CType type;
    type.t = t;
    type.ref = NULL;
    gen_cast(&type);
}

/* cast 'vtop' to 'type'. `Casting` to bitfields is forbidden. */
static void gen_cast(CType *type)
{
    int sbt, dbt, sf, df, c;
    int dbt_bt, sbt_bt, ds, ss, bits, trunc;
    static TCC_TLS int casting_complex = 0;
    if (!casting_complex && ((vtop->type.t & (VT_COMPLEX | VT_IMAGINARY)) || (type->t & (VT_COMPLEX | VT_IMAGINARY)))) {
        casting_complex = 1;
        int st_t = vtop->type.t;
        int dt_t = type->t;
        int base_dt = dt_t & ~(VT_COMPLEX | VT_IMAGINARY);
        CType base_type; base_type.t = base_dt; base_type.ref = NULL;

        if (!(st_t & VT_COMPLEX) && (st_t & VT_IMAGINARY) && !(dt_t & (VT_COMPLEX | VT_IMAGINARY))) {
            vpop();
            {
                CValue cval = {0};
                vsetc(&base_type, VT_CONST, &cval);
            }
        } else if (!(st_t & (VT_COMPLEX | VT_IMAGINARY)) && !(dt_t & VT_COMPLEX) && (dt_t & VT_IMAGINARY)) {
            gen_cast(&base_type);
            vtop->type.t |= VT_IMAGINARY;
        } else {
            gen_cast(&base_type);
            vtop->type.t = dt_t;
        }
        casting_complex = 0;
        return;
    }
    /* special delayed cast for char/short */
    if (vtop->r & VT_MUSTCAST)
        force_charshort_cast();

    /* bitfields first get cast to ints */
    if (vtop->type.t & VT_BITFIELD)
        gv(RC_INT);

    if (IS_ENUM(type->t) && type->ref->c < 0)
        tcc_error("cast to incomplete type");

    dbt = type->t & (VT_BTYPE | VT_UNSIGNED);
    sbt = vtop->type.t & (VT_BTYPE | VT_UNSIGNED);
    if (sbt == VT_FUNC)
        sbt = VT_PTR;

again:
    if (sbt != dbt) {
        sf = is_float(sbt);
        df = is_float(dbt);
        dbt_bt = dbt & VT_BTYPE;
        sbt_bt = sbt & VT_BTYPE;
        if (dbt_bt == VT_VOID) {
            /* do not confuse backends with `VT_VOID` in registers */
            vpop(), vpushi(0);
            goto done;
        }
        if (sbt_bt == VT_VOID) {
error:
            cast_error(&vtop->type, type);
        }

        c = (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM)) == VT_CONST;
        if (c) {
            /* constant case: we can do it now */
            /* `XXX`: in `ISOC`, cannot do it if error in convert */
            if (sbt == VT_FLOAT)
                vtop->c.ld = vtop->c.f;
            else if (sbt == VT_DOUBLE)
                vtop->c.ld = vtop->c.d;

            if (df) {
                if (sbt_bt == VT_LLONG) {
                    if ((sbt & VT_UNSIGNED) || !(vtop->c.i >> 63))
                        vtop->c.ld = vtop->c.i;
                    else
                        vtop->c.ld = -(long double)-vtop->c.i;
                } else if(!sf) {
                    if ((sbt & VT_UNSIGNED) || !(vtop->c.i >> 31))
                        vtop->c.ld = (uint32_t)vtop->c.i;
                    else
                        vtop->c.ld = -(long double)-(uint32_t)vtop->c.i;
                }

                if (dbt == VT_FLOAT)
                    vtop->c.f = (float)vtop->c.ld;
                else if (dbt == VT_DOUBLE)
                    vtop->c.d = (double)vtop->c.ld;
            } else if (sf && dbt == VT_BOOL) {
                vtop->c.i = (vtop->c.ld != 0);
            } else {
                if(sf) {
                    if (dbt & VT_UNSIGNED)
                        vtop->c.i = (uint64_t)vtop->c.ld;
                    else
                        vtop->c.i = (int64_t)vtop->c.ld;
                }
                else if (sbt_bt == VT_LLONG || (PTR_SIZE == 8 && sbt == VT_PTR))
                    ;
                else if (sbt & VT_UNSIGNED)
                    vtop->c.i = (uint32_t)vtop->c.i;
                else
                    vtop->c.i = ((uint32_t)vtop->c.i | -(vtop->c.i & 0x80000000));

                if (dbt_bt == VT_LLONG || (PTR_SIZE == 8 && dbt == VT_PTR))
                    ;
                else if (dbt == VT_BOOL)
                    vtop->c.i = (vtop->c.i != 0);
                else {
                    uint32_t m = dbt_bt == VT_BYTE ? 0xff :
                                 dbt_bt == VT_SHORT ? 0xffff :
                                  0xffffffff;
                    vtop->c.i &= m;
                    if (!(dbt & VT_UNSIGNED))
                        vtop->c.i |= -(vtop->c.i & ((m >> 1) + 1));
                }
            }
            goto done;

        } else if (dbt == VT_BOOL
            && (vtop->r & (VT_VALMASK | VT_LVAL | VT_SYM))
                == (VT_CONST | VT_SYM)) {
            /* addresses are considered non-zero (see tcctest.c:sinit23) */
            vtop->r = VT_CONST;
            vtop->c.i = 1;
            goto done;
        }

        /* cannot generate code for global or static initializers */
        if (nocode_wanted & DATA_ONLY_WANTED) {
            if (df)
                vtop->r = get_reg(RC_FLOAT); /* don't confuse backends */
            goto done;
        }

        /* non constant case: generate code */
        if (dbt == VT_BOOL) {
            gen_test_zero(TOK_NE);
            goto done;
        }

        if (sf || df) {
            if (sf && df) {
                /* convert from fp to fp */
                gen_cvt_ftof(dbt);
            } else if (df) {
                /* convert int to fp */
                gen_cvt_itof1(dbt);
            } else {
                /* convert fp to int */
                sbt = dbt;
                if (dbt_bt != VT_LLONG && dbt_bt != VT_INT)
                    sbt = VT_INT;
                gen_cvt_ftoi1(sbt);
                goto again; /* may need char/short cast */
            }
            goto done;
        }

        ds = btype_size(dbt_bt);
        ss = btype_size(sbt_bt);
        if (ds == 0 || ss == 0)
            goto error;

        /* same size and no sign conversion needed */
        if (ds == ss && ds >= 4)
            goto done;
        if (dbt_bt == VT_PTR || sbt_bt == VT_PTR) {
            tcc_warning("cast between pointer and integer of different size");
            if (sbt_bt == VT_PTR) {
                /* put integer type to allow logical operations below */
                vtop->type.t = (PTR_SIZE == 8 ? VT_LLONG : VT_INT);
            }
        }

        /* processor allows { int a = 0, b = *(char*)&a; }
           `That` means that if we cast to less width, we can just
           change the type and read it still later. */
        #define ALLOW_SUBTYPE_ACCESS 1

        if (ALLOW_SUBTYPE_ACCESS && (vtop->r & VT_LVAL)) {
            /* value still in memory */
            if (ds <= ss)
                goto done;
            /* ss <= 4 here */
            if (ds <= 4 && !(dbt == (VT_SHORT | VT_UNSIGNED) && sbt == VT_BYTE)) {
                gv(RC_INT);
                goto done; /* no 64bit envolved */
            }
        }
        gv(RC_INT);

        trunc = 0;
#if PTR_SIZE == 4
        if (ds == 8) {
            /* generate high word */
            if (sbt & VT_UNSIGNED) {
                vpushi(0);
                gv(RC_INT);
            } else {
                gv_dup();
                vpushi(31);
                gen_op(TOK_SAR);
            }
            lbuild(dbt);
        } else if (ss == 8) {
            /* from long long: just take low order word */
            lexpand();
            vpop();
        }
        ss = 4;

#elif PTR_SIZE == 8
        if (ds == 8) {
            /* need to convert from 32bit to 64bit */
            if (sbt & VT_UNSIGNED) {
#if defined(TCC_TARGET_RISCV64)
                /* `RISC`-V keeps 32bit vals in registers sign-extended.
                   `So` here we need a zero-extension.  */
                trunc = 32;
#else
                goto done;
#endif
            } else {
                gen_cvt_sxtw();
                goto done;
            }
            ss = ds, ds = 4, dbt = sbt;
        } else if (ss == 8) {
            /* `RISC`-V keeps 32bit vals in registers sign-extended.
               `So` here we need a sign-extension for signed types and
               zero-extension. for unsigned types. */
#if !defined(TCC_TARGET_RISCV64)
            trunc = 32; /* zero upper 32 bits for non `RISC`-V targets */
#endif
        } else {
            ss = 4;
        }
#endif

        if (ds >= ss)
            goto done;
#if defined TCC_TARGET_I386 || defined TCC_TARGET_X86_64 || defined TCC_TARGET_ARM64 || defined TCC_TARGET_RISCV64
    if (ss == 4) {
        gen_cvt_csti(dbt);
        goto done;
    }
#endif
        bits = (ss - ds) * 8;
        /* for unsigned, `gen_op` will convert `SAR` to `SHR` */
        vtop->type.t = (ss == 8 ? VT_LLONG : VT_INT) | (dbt & VT_UNSIGNED);
        vpushi(bits);
        gen_op(TOK_SHL);
        vpushi(bits - trunc);
        gen_op(TOK_SAR);
        vpushi(trunc);
        gen_op(TOK_SHR);
    }
done:
    vtop->type = *type;
    vtop->type.t &= ~ ( VT_QUALIFIER | VT_ARRAY | VT_STORAGE_TLS );
}

#include "tccgen-types.c"
