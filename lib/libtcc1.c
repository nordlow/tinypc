/* TPC runtime library.
   Parts of this code are (c) 2002 Fabrice Bellard 

   Copyright (C) 1987, 1988, 1992, 1994, 1995 Free Software Foundation, Inc.

This file is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

In addition to the permissions in the GNU General Public License, the
Free Software Foundation gives you unlimited permission to link the
compiled version of this file into combinations with other programs,
and to distribute those combinations without any restriction coming
from the use of this file.  (The General Public License restrictions
do apply in other respects; for example, they cover modification of
the file, and distribution when not linked into a combine
executable.)

This file is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING.  If not, write to
the Free Software Foundation, 59 Temple Place - Suite 330,
Boston, MA 02111-1307, USA.  
*/

#define W_TYPE_SIZE   32
#define BITS_PER_UNIT 8

typedef int Wtype;
typedef unsigned int UWtype;
typedef unsigned int USItype;
typedef long long DWtype;
typedef unsigned long long UDWtype;

struct DWstruct {
    Wtype low, high;
};

typedef union
{
  struct DWstruct s;
  DWtype ll;
} DWunion;

typedef long double XFtype;
#define WORD_SIZE (sizeof (Wtype) * BITS_PER_UNIT)
#define HIGH_WORD_COEFF (((UDWtype) 1) << WORD_SIZE)

/* the following deal with IEEE single-precision numbers */
#define EXCESS		126
#define SIGNBIT		0x80000000
#define HIDDEN		(1 << 23)
#define SIGN(fp)	((fp) & SIGNBIT)
#define EXP(fp)		(((fp) >> 23) & 0xFF)
#define MANT(fp)	(((fp) & 0x7FFFFF) | HIDDEN)
#define PACK(s,e,m)	((s) | ((e) << 23) | (m))

/* the following deal with IEEE double-precision numbers */
#define EXCESSD		1022
#define HIDDEND		(1 << 20)
#define EXPD(fp)	(((fp.l.upper) >> 20) & 0x7FF)
#define SIGND(fp)	((fp.l.upper) & SIGNBIT)
#define MANTD(fp)	(((((fp.l.upper) & 0xFFFFF) | HIDDEND) << 10) | \
				(fp.l.lower >> 22))
#define HIDDEND_LL	((long long)1 << 52)
#define MANTD_LL(fp)	((fp.ll & (HIDDEND_LL-1)) | HIDDEND_LL)
#define PACKD_LL(s,e,m)	(((long long)((s)+((e)<<20))<<32)|(m))

/* the following deal with x86 long double-precision numbers */
#define EXCESSLD	16382
#define EXPLD(fp)	(fp.l.upper & 0x7fff)
#define SIGNLD(fp)	((fp.l.upper) & 0x8000)

/* only for x86 */
union ldouble_long {
    long double ld;
    struct {
        unsigned long long lower;
        unsigned short upper;
    } l;
};

union double_long {
    double d;
#if 1
    struct {
        unsigned int lower;
        int upper;
    } l;
#else
    struct {
        int upper;
        unsigned int lower;
    } l;
#endif
    long long ll;
};

union float_long {
    float f;
    unsigned int l;
};

/* XXX: we don't support several builtin supports for now */
#if defined __i386__

/* XXX: use gcc/tcc intrinsic ? */
#define sub_ddmmss(sh, sl, ah, al, bh, bl) \
  __asm__ ("subl %5,%1\n\tsbbl %3,%0"					\
	   : "=r" ((USItype) (sh)),					\
	     "=&r" ((USItype) (sl))					\
	   : "0" ((USItype) (ah)),					\
	     "g" ((USItype) (bh)),					\
	     "1" ((USItype) (al)),					\
	     "g" ((USItype) (bl)))
#define umul_ppmm(w1, w0, u, v) \
  __asm__ ("mull %3"							\
	   : "=a" ((USItype) (w0)),					\
	     "=d" ((USItype) (w1))					\
	   : "%0" ((USItype) (u)),					\
	     "rm" ((USItype) (v)))
#define udiv_qrnnd(q, r, n1, n0, dv) \
  __asm__ ("divl %4"							\
	   : "=a" ((USItype) (q)),					\
	     "=d" ((USItype) (r))					\
	   : "0" ((USItype) (n0)),					\
	     "1" ((USItype) (n1)),					\
	     "rm" ((USItype) (dv)))
#define count_leading_zeros(count, x) \
  do {									\
    USItype __cbtmp;							\
    __asm__ ("bsrl %1,%0"						\
	     : "=r" (__cbtmp) : "rm" ((USItype) (x)));			\
    (count) = __cbtmp ^ 31;						\
  } while (0)

/* most of this code is taken from libgcc2.c from gcc */

static UDWtype __udivmoddi4 (UDWtype n, UDWtype d, UDWtype *rp)
{
  DWunion ww;
  DWunion nn, dd;
  DWunion rr;
  UWtype d0, d1, n0, n1, n2;
  UWtype q0, q1;
  UWtype b, bm;

  nn.ll = n;
  dd.ll = d;

  d0 = dd.s.low;
  d1 = dd.s.high;
  n0 = nn.s.low;
  n1 = nn.s.high;

#if !defined(UDIV_NEEDS_NORMALIZATION)
  if (d1 == 0)
    {
      if (d0 > n1)
	{
	  /* 0q = nn / 0D */

	  udiv_qrnnd (q0, n0, n1, n0, d0);
	  q1 = 0;

	  /* Remainder in n0.  */
	}
      else
	{
	  /* qq = NN / 0d */

	  if (d0 == 0)
	    d0 = 1 / d0;	/* Divide intentionally by zero.  */

	  udiv_qrnnd (q1, n1, 0, n1, d0);
	  udiv_qrnnd (q0, n0, n1, n0, d0);

	  /* Remainder in n0.  */
	}

      if (rp != 0)
	{
	  rr.s.low = n0;
	  rr.s.high = 0;
	  *rp = rr.ll;
	}
    }

#else /* UDIV_NEEDS_NORMALIZATION */

  if (d1 == 0)
    {
      if (d0 > n1)
	{
	  /* 0q = nn / 0D */

	  count_leading_zeros (bm, d0);

	  if (bm != 0)
	    {
	      /* Normalize, i.e. make the most significant bit of the
		 denominator set.  */

	      d0 = d0 << bm;
	      n1 = (n1 << bm) | (n0 >> (W_TYPE_SIZE - bm));
	      n0 = n0 << bm;
	    }

	  udiv_qrnnd (q0, n0, n1, n0, d0);
	  q1 = 0;

	  /* Remainder in n0 >> bm.  */
	}
      else
	{
	  /* qq = NN / 0d */

	  if (d0 == 0)
	    d0 = 1 / d0;	/* Divide intentionally by zero.  */

	  count_leading_zeros (bm, d0);

	  if (bm == 0)
	    {
	      /* From (n1 >= d0) /\ (the most significant bit of d0 is set),
		 conclude (the most significant bit of n1 is set) /\ (the
		 leading quotient digit q1 = 1).

		 This special case is necessary, not an optimization.
		 (Shifts counts of W_TYPE_SIZE are undefined.)  */

	      n1 -= d0;
	      q1 = 1;
	    }
	  else
	    {
	      /* Normalize.  */

	      b = W_TYPE_SIZE - bm;

	      d0 = d0 << bm;
	      n2 = n1 >> b;
	      n1 = (n1 << bm) | (n0 >> b);
	      n0 = n0 << bm;

	      udiv_qrnnd (q1, n1, n2, n1, d0);
	    }

	  /* n1 != d0...  */

	  udiv_qrnnd (q0, n0, n1, n0, d0);

	  /* Remainder in n0 >> bm.  */
	}

      if (rp != 0)
	{
	  rr.s.low = n0 >> bm;
	  rr.s.high = 0;
	  *rp = rr.ll;
	}
    }
#endif /* UDIV_NEEDS_NORMALIZATION */

  else
    {
      if (d1 > n1)
	{
	  /* 00 = nn / DD */

	  q0 = 0;
	  q1 = 0;

	  /* Remainder in n1n0.  */
	  if (rp != 0)
	    {
	      rr.s.low = n0;
	      rr.s.high = n1;
	      *rp = rr.ll;
	    }
	}
      else
	{
	  /* 0q = NN / dd */

	  count_leading_zeros (bm, d1);
	  if (bm == 0)
	    {
	      /* From (n1 >= d1) /\ (the most significant bit of d1 is set),
		 conclude (the most significant bit of n1 is set) /\ (the
		 quotient digit q0 = 0 or 1).

		 This special case is necessary, not an optimization.  */

	      /* The condition on the next line takes advantage of that
		 n1 >= d1 (true due to program flow).  */
	      if (n1 > d1 || n0 >= d0)
		{
		  q0 = 1;
		  sub_ddmmss (n1, n0, n1, n0, d1, d0);
		}
	      else
		q0 = 0;

	      q1 = 0;

	      if (rp != 0)
		{
		  rr.s.low = n0;
		  rr.s.high = n1;
		  *rp = rr.ll;
		}
	    }
	  else
	    {
	      UWtype m1, m0;
	      /* Normalize.  */

	      b = W_TYPE_SIZE - bm;

	      d1 = (d1 << bm) | (d0 >> b);
	      d0 = d0 << bm;
	      n2 = n1 >> b;
	      n1 = (n1 << bm) | (n0 >> b);
	      n0 = n0 << bm;

	      udiv_qrnnd (q0, n1, n2, n1, d1);
	      umul_ppmm (m1, m0, q0, d0);

	      if (m1 > n1 || (m1 == n1 && m0 > n0))
		{
		  q0--;
		  sub_ddmmss (m1, m0, m1, m0, d1, d0);
		}

	      q1 = 0;

	      /* Remainder in (n1n0 - m1m0) >> bm.  */
	      if (rp != 0)
		{
		  sub_ddmmss (n1, n0, n1, n0, m1, m0);
		  rr.s.low = (n1 << b) | (n0 >> bm);
		  rr.s.high = n1 >> bm;
		  *rp = rr.ll;
		}
	    }
	}
    }

  ww.s.low = q0;
  ww.s.high = q1;
  return ww.ll;
}

#define __negdi2(a) (-(a))

long long __divdi3(long long u, long long v)
{
    int c = 0;
    DWunion uu, vv;
    DWtype w;
    
    uu.ll = u;
    vv.ll = v;
    
    if (uu.s.high < 0) {
        c = ~c;
        uu.ll = __negdi2 (uu.ll);
    }
    if (vv.s.high < 0) {
        c = ~c;
        vv.ll = __negdi2 (vv.ll);
    }
    w = __udivmoddi4 (uu.ll, vv.ll, (UDWtype *) 0);
    if (c)
        w = __negdi2 (w);
    return w;
}

long long __moddi3(long long u, long long v)
{
    int c = 0;
    DWunion uu, vv;
    DWtype w;
    
    uu.ll = u;
    vv.ll = v;
    
    if (uu.s.high < 0) {
        c = ~c;
        uu.ll = __negdi2 (uu.ll);
    }
    if (vv.s.high < 0)
        vv.ll = __negdi2 (vv.ll);
    
    __udivmoddi4 (uu.ll, vv.ll, (UDWtype *) &w);
    if (c)
        w = __negdi2 (w);
    return w;
}

unsigned long long __udivdi3(unsigned long long u, unsigned long long v)
{
    return __udivmoddi4 (u, v, (UDWtype *) 0);
}

unsigned long long __umoddi3(unsigned long long u, unsigned long long v)
{
    UDWtype w;
    
    __udivmoddi4 (u, v, &w);
    return w;
}

/* XXX: fix tcc's code generator to do this instead */
long long __ashrdi3(long long a, int b)
{
#ifdef __TINYC__
    DWunion u;
    u.ll = a;
    if (b >= 32) {
        u.s.low = u.s.high >> (b - 32);
        u.s.high = u.s.high >> 31;
    } else if (b != 0) {
        u.s.low = ((unsigned)u.s.low >> b) | (u.s.high << (32 - b));
        u.s.high = u.s.high >> b;
    }
    return u.ll;
#else
    return a >> b;
#endif
}

/* XXX: fix tcc's code generator to do this instead */
unsigned long long __lshrdi3(unsigned long long a, int b)
{
#ifdef __TINYC__
    DWunion u;
    u.ll = a;
    if (b >= 32) {
        u.s.low = (unsigned)u.s.high >> (b - 32);
        u.s.high = 0;
    } else if (b != 0) {
        u.s.low = ((unsigned)u.s.low >> b) | (u.s.high << (32 - b));
        u.s.high = (unsigned)u.s.high >> b;
    }
    return u.ll;
#else
    return a >> b;
#endif
}

/* XXX: fix tcc's code generator to do this instead */
long long __ashldi3(long long a, int b)
{
#ifdef __TINYC__
    DWunion u;
    u.ll = a;
    if (b >= 32) {
        u.s.high = (unsigned)u.s.low << (b - 32);
        u.s.low = 0;
    } else if (b != 0) {
        u.s.high = ((unsigned)u.s.high << b) | ((unsigned)u.s.low >> (32 - b));
        u.s.low = (unsigned)u.s.low << b;
    }
    return u.ll;
#else
    return a << b;
#endif
}

#endif /* __i386__ */

/* XXX: fix tcc's code generator to do this instead */
float __floatundisf(unsigned long long a)
{
    DWunion uu; 
    XFtype r;

    uu.ll = a;
    if (uu.s.high >= 0) {
        return (float)uu.ll;
    } else {
        r = (XFtype)uu.ll;
        r += 18446744073709551616.0;
        return (float)r;
    }
}

double __floatundidf(unsigned long long a)
{
    DWunion uu; 
    XFtype r;

    uu.ll = a;
    if (uu.s.high >= 0) {
        return (double)uu.ll;
    } else {
        r = (XFtype)uu.ll;
        r += 18446744073709551616.0;
        return (double)r;
    }
}

long double __floatundixf(unsigned long long a)
{
    DWunion uu; 
    XFtype r;

    uu.ll = a;
    if (uu.s.high >= 0) {
        return (long double)uu.ll;
    } else {
        r = (XFtype)uu.ll;
        r += 18446744073709551616.0;
        return (long double)r;
    }
}

unsigned long long __fixunssfdi (float a1)
{
    register union float_long fl1;
    register int exp;
    register unsigned long long l;

    fl1.f = a1;

    if (fl1.l == 0)
	return (0);

    exp = EXP (fl1.l) - EXCESS - 24;
    l = MANT(fl1.l);

    if (exp >= 41)
        return 1ULL << 63;
    else if (exp >= 0)
        l <<= exp;
    else if (exp >= -23)
        l >>= -exp;
    else
	return 0;
    if (SIGN(fl1.l))
        l = (unsigned long long)-l;
    return l;
}

long long __fixsfdi (float a1)
{
    long long ret; int s;
    ret = __fixunssfdi((s = a1 >= 0) ? a1 : -a1);
    return s ? ret : -ret;
}

unsigned long long __fixunsdfdi (double a1)
{
    register union double_long dl1;
    register int exp;
    register unsigned long long l;

    dl1.d = a1;

    if (dl1.ll == 0)
	return (0);

    exp = EXPD (dl1) - EXCESSD - 53;
    l = MANTD_LL(dl1);

    if (exp >= 12)
        return 1ULL << 63; /* overflow result (like gcc, somewhat) */
    else if (exp >= 0)
        l <<= exp;
    else if (exp >= -52)
        l >>= -exp;
    else
        return 0;
    if (SIGND(dl1))
        l = (unsigned long long)-l;
    return l;
}

long long __fixdfdi (double a1)
{
    long long ret; int s;
    ret = __fixunsdfdi((s = a1 >= 0) ? a1 : -a1);
    return s ? ret : -ret;
}

#ifndef __arm__
unsigned long long __fixunsxfdi (long double a1)
{
    register union ldouble_long dl1;
    register int exp;
    register unsigned long long l;

    dl1.ld = a1;

    if (dl1.l.lower == 0 && dl1.l.upper == 0)
	return (0);

    exp = EXPLD (dl1) - EXCESSLD - 64;
    l = dl1.l.lower;
    if (exp > 0)
	return 1ULL << 63;
    if (exp < -63)
        return 0;
    l >>= -exp;
    if (SIGNLD(dl1))
        l = (unsigned long long)-l;
    return l;
}

long long __fixxfdi (long double a1)
{
    long long ret; int s;
    ret = __fixunsxfdi((s = a1 >= 0) ? a1 : -a1);
    return s ? ret : -ret;
}
#endif /* !ARM */

/* D-style dynamic array (slice) support.  A slice is a header holding
   the element count followed by a pointer to the elements.  The
   runtime only knows about memory obtained through these functions;
   slices that view an array must be duplicated before they are
   modified.  */

typedef __SIZE_TYPE__ size_t;

struct darray_header {
    size_t length;
    void *ptr;
};

extern void *malloc(size_t);
extern void *realloc(void *, size_t);
extern void free(void *);
extern void *memcpy(void *, const void *, size_t);
extern void *memmove(void *, const void *, size_t);
extern int memcmp(const void *, const void *, size_t);
extern int printf(const char *, ...);
extern int snprintf(const char *, ...);
extern size_t strlen(const char *);
extern void exit(int);

static void darray_set_length(struct darray_header *a,
                              size_t elemsize,
                              size_t newlen)
{
    void *np;

    if (newlen == 0) {
        free(a->ptr);
        a->ptr = 0;
        a->length = 0;
        return;
    }
    if (a->ptr)
        np = realloc(a->ptr, newlen * elemsize);
    else
        np = malloc(newlen * elemsize);
    if (!np)
        return;
    a->ptr = np;
    a->length = newlen;
}

void __darray_resize(void *p, size_t elemsize, size_t newlen)
{
    darray_set_length(p, elemsize, newlen);
}

void __darray_append(void *p, const void *elem, size_t elemsize)
{
    struct darray_header *a = p;
    size_t n = a->length;

    darray_set_length(a, elemsize, n + 1);
    if (a->length == n + 1)
        memcpy((char *)a->ptr + n * elemsize, elem, elemsize);
}

void __darray_dup(void *dst, const void *src, size_t elemsize)
{
    const struct darray_header *s = src;
    struct darray_header *d = dst;

    d->length = 0;
    d->ptr = 0;
    darray_set_length(d, elemsize, s->length);
    if (d->length == s->length && s->length)
        memcpy(d->ptr, s->ptr, s->length * elemsize);
}

/* Bulk copy of one slice into another.  D requires the lengths to be
   equal; 'mode' selects what happens otherwise:
   0 (default) - report a range error and abort, like D's RangeError,
   1           - copy only the common prefix,
   2           - silently ignore the mismatch.  */
void __darray_copy(void *dst_slice, const void *src_slice, size_t elemsize,
                   int mode)
{
    const struct darray_header *d = dst_slice;
    const struct darray_header *s = src_slice;
    size_t n = d->length;

    if (d->length != s->length) {
        if (mode == 2)
            return;
        if (mode == 0) {
            printf("tcc: slice assignment length mismatch "
                   "(destination %lu, source %lu elements)\n",
                   (unsigned long)d->length, (unsigned long)s->length);
            exit(255);
        }
        if (s->length < n)
            n = s->length;
    }
    if (n && d->ptr && s->ptr)
        memmove(d->ptr, s->ptr, n * elemsize);
}

void __darray_copy_nonoverlapping(void *dst_slice, const void *src_slice, size_t elemsize)
{
    const struct darray_header *d = dst_slice;
    const struct darray_header *s = src_slice;
    if (d->length == s->length && d->length && d->ptr && s->ptr) {
        memcpy(d->ptr, s->ptr, d->length * elemsize);
    }
}

void __darray_fill(void *dst_slice, const void *elem, size_t elemsize)
{
    const struct darray_header *d = dst_slice;
    size_t i;
    char *p = d->ptr;
    if (d->length && p) {
        for (i = 0; i < d->length; ++i) {
            memcpy(p + i * elemsize, elem, elemsize);
        }
    }
}

void __darray_concat(void *dst, const void *asrc, const void *bsrc,
                     size_t elemsize)
{
    const struct darray_header *a = asrc, *b = bsrc;
    struct darray_header *d = dst;
    size_t na = a->length, nb = b->length;

    d->length = 0;
    d->ptr = 0;
    darray_set_length(d, elemsize, na + nb);
    if (d->length != na + nb)
        return;
    if (na)
        memcpy(d->ptr, a->ptr, na * elemsize);
    if (nb)
        memcpy((char *)d->ptr + na * elemsize, b->ptr, nb * elemsize);
}
int __darray_eq(const void *s1, const void *s2, size_t elemsize)
{
    const struct darray_header *a = s1;
    const struct darray_header *b = s2;
    if (a->length != b->length)
        return 0;
    if (a->length == 0)
        return 1;
    if (a->ptr == b->ptr)
        return 1;
    if (!a->ptr || !b->ptr)
        return a->ptr == b->ptr;
    return memcmp(a->ptr, b->ptr, a->length * elemsize) == 0;
}
void __d_assert(const char *msg)
{
    if (msg)
        printf("Assertion failure: %s\n", msg);
    else
        printf("Assertion failure\n");
    exit(255);
}

void __slice_index_bounds_error(const char *file, int line,
                                size_t index, size_t length)
{
    printf("%s:%d: slice index %llu out of bounds (length %llu)\n",
           file, line,
           (unsigned long long)index, (unsigned long long)length);
    exit(255);
}

void __slice_range_bounds_error(const char *file, int line,
                                size_t lo, size_t hi, size_t length,
                                int inclusive)
{
    printf("%s:%d: slice range %llu..%s%llu out of bounds (length %llu)\n",
           file, line,
           (unsigned long long)lo, inclusive ? "=" : "",
           (unsigned long long)hi, (unsigned long long)length);
    exit(255);
}

extern size_t strlen(const char *);

/* basic types of the values rendered by the assertion helpers (subset of
   the compiler's VT_xxx type codes, see tcc.h) */
#define EL_BYTE     1  /* VT_BYTE    */
#define EL_SHORT    2  /* VT_SHORT   */
#define EL_INT      3  /* VT_INT     */
#define EL_LLONG    4  /* VT_LLONG   */
#define EL_PTR      5  /* VT_PTR     */
#define EL_FLOAT    8  /* VT_FLOAT   */
#define EL_DOUBLE   9  /* VT_DOUBLE  */
#define EL_LDOUBLE 10  /* VT_LDOUBLE */
#define EL_BOOL    11  /* VT_BOOL    */

#define EL_BTYPE    0x000f /* VT_BTYPE    */
#define EL_UNSIGNED 0x0010 /* VT_UNSIGNED */

static unsigned long long elem_read(const char *p, size_t elemsize)
{
    unsigned long long v = 0;
    size_t i;
    int shift = 0;
    for (i = 0; i < elemsize && i < sizeof(v); ++i) {
        v |= ((unsigned long long)((unsigned char)p[i])) << shift;
        shift += 8;
    }
    return v;
}

static double elem_read_d(const char *p)
{
    double d;
    memcpy(&d, p, sizeof(d));
    return d;
}

static int elem_is_float(int bt)
{
    return bt == EL_FLOAT || bt == EL_DOUBLE || bt == EL_LDOUBLE;
}

/* read the SIZE bytes at P as an unsigned integer */
static unsigned long long scalar_read(const char *p, int size)
{
    unsigned long long v = 0;
    int i;
    for (i = 0; i < size && i < (int)sizeof(v); ++i)
        v |= ((unsigned long long)((unsigned char)p[i])) << (i * 8);
    return v;
}

/* read the SIZE bytes at P as a signed integer, sign extending to 64 bits */
static long long scalar_read_signed(const char *p, int size)
{
    unsigned long long v = scalar_read(p, size);
    if (size > 0 && size < (int)sizeof(v) && ((v >> (size * 8 - 1)) & 1))
        v |= ~0ULL << (size * 8);
    return (long long)v;
}

/* render the character C as a character literal, e.g. 'a' or '\n' */
static void render_char(char *buf, size_t buf_size, unsigned int c)
{
    const char *esc = 0;

    switch (c) {
    case 0:    esc = "\\0"; break;
    case '\a': esc = "\\a"; break;
    case '\b': esc = "\\b"; break;
    case '\t': esc = "\\t"; break;
    case '\n': esc = "\\n"; break;
    case '\v': esc = "\\v"; break;
    case '\f': esc = "\\f"; break;
    case '\r': esc = "\\r"; break;
    case '\\': esc = "\\\\"; break;
    case '\'': esc = "\\'"; break;
    }
    if (esc)
        snprintf(buf, buf_size, "'%s'", esc);
    else if (c >= 0x20 && c < 0x7f)
        snprintf(buf, buf_size, "'%c'", (int)c);
    else
        snprintf(buf, buf_size, "'\\x%02x'", c);
}

/* render the scalar value of type TYPE (SIZE bytes at P) into BUF, e.g.
   "42", "'a'", "1.5", "true" or "0x10" */
static void render_scalar(char *buf, size_t buf_size, const char *p,
                          int type, int size)
{
    switch (type & EL_BTYPE) {
    case EL_BOOL:
        snprintf(buf, buf_size, "%s", scalar_read(p, size) ? "true" : "false");
        break;
    case EL_BYTE:
        render_char(buf, buf_size, (unsigned int)scalar_read(p, size));
        break;
    case EL_FLOAT: {
        float f;
        memcpy(&f, p, sizeof(f));
        snprintf(buf, buf_size, "%g", (double)f);
        break;
    }
    case EL_DOUBLE: {
        double d;
        memcpy(&d, p, sizeof(d));
        snprintf(buf, buf_size, "%g", d);
        break;
    }
    case EL_LDOUBLE: {
        long double ld = 0;
        size_t n = size < (int)sizeof(ld) ? (size_t)size : sizeof(ld);
        memcpy(&ld, p, n);
        snprintf(buf, buf_size, "%Lg", ld);
        break;
    }
    case EL_PTR: {
        unsigned long long v = scalar_read(p, size);
        if (v)
            snprintf(buf, buf_size, "0x%llx", v);
        else
            snprintf(buf, buf_size, "null");
        break;
    }
    default:
        if (type & EL_UNSIGNED)
            snprintf(buf, buf_size, "%llu", scalar_read(p, size));
        else
            snprintf(buf, buf_size, "%lld", scalar_read_signed(p, size));
        break;
    }
}

/* called when 'assert(a op b)' fails.  A and B point at the evaluated
   operands, each described by its type code and its size in bytes.  */
void __d_assert_cmp(const char *assertion, const char *neg_op,
                    const void *a, int a_type, int a_size,
                    const void *b, int b_type, int b_size)
{
    char ab[64], bb[64];

    render_scalar(ab, sizeof(ab), a, a_type, a_size);
    render_scalar(bb, sizeof(bb), b, b_type, b_size);
    printf("assert(%s) failed: %s %s %s\n", assertion, ab, neg_op, bb);
    exit(255);
}

/* render the elements of a sequence into BUF, e.g. "[1, 2, 3]" */
static void render_array(char *buf, size_t buf_size, const struct darray_header *h,
                         size_t elemsize, int bt)
{
    size_t i;
    size_t pos = 0;
    char eb[64];
    int n;

    buf[0] = '[';
    pos = 1;
    for (i = 0; i < h->length; ++i) {
        if (i) {
            n = snprintf(buf + pos, buf_size - pos, ", ");
            if (n < 0 || (size_t)n >= buf_size - pos)
                break;
            pos += (size_t)n;
        }
        if (elem_is_float(bt)) {
            double d = elem_read_d((const char *)h->ptr + i * elemsize);
            n = snprintf(eb, sizeof(eb), "%g", d);
        } else {
            n = snprintf(eb, sizeof(eb), "%llu", elem_read((const char *)h->ptr + i * elemsize, elemsize));
        }
        if (n < 0 || (size_t)n >= sizeof(eb) || pos + (size_t)n >= buf_size)
            break;
        memcpy(buf + pos, eb, (size_t)n);
        pos += (size_t)n;
    }
    snprintf(buf + pos, buf_size - pos, "]");
}

/* return the rendered length of a sequence, e.g. the length of "[1, 2, 3]" */
static int render_array_len(const struct darray_header *h, size_t elemsize, int bt)
{
    size_t i;
    int len = 1; /* '[' */
    char eb[64];
    for (i = 0; i < h->length; ++i) {
        int n;
        if (i)
            len += 2; /* ", " */
        if (elem_is_float(bt)) {
            double d = elem_read_d((const char *)h->ptr + i * elemsize);
            n = snprintf(eb, sizeof(eb), "%g", d);
        } else {
            n = snprintf(eb, sizeof(eb), "%llu", elem_read((const char *)h->ptr + i * elemsize, elemsize));
        }
        if (n < 0)
            break;
        len += n;
    }
    return len + 1; /* ']' */
}

/* print a sequence, e.g. "[1, 2, 3]" */
static void print_array(const struct darray_header *h, size_t elemsize, int bt)
{
    size_t i;
    printf("[");
    for (i = 0; i < h->length; ++i) {
        char eb[64];
        if (i)
            printf(", ");
        if (elem_is_float(bt)) {
            double d = elem_read_d((const char *)h->ptr + i * elemsize);
            snprintf(eb, sizeof(eb), "%g", d);
            printf("%s", eb);
        } else {
            snprintf(eb, sizeof(eb), "%llu", elem_read((const char *)h->ptr + i * elemsize, elemsize));
            printf("%s", eb);
        }
    }
    printf("]");
}

void __d_assert_cmp_arr(const char *assertion, const char *neg_op,
                       const void *a, const void *b, size_t elemsize,
                       int elem_bt, const char *name_a, const char *name_b)
{
    const struct darray_header *ha = (const struct darray_header *)a;
    const struct darray_header *hb = (const struct darray_header *)b;
    size_t la = ha->length, lb = hb->length;
    char pre[512];
    int n, inline_len;

    n = snprintf(pre, sizeof(pre), "assert(%s) failed: ", assertion);
    inline_len = n + render_array_len(ha, elemsize, elem_bt) + 1
               + (int)strlen(neg_op) + 1
               + render_array_len(hb, elemsize, elem_bt) + 1;

    if (inline_len <= 80) {
        /* inline format: both sequences fit on one line */
        printf("assert(%s) failed: ", assertion);
        print_array(ha, elemsize, elem_bt);
        printf(" %s ", neg_op);
        print_array(hb, elemsize, elem_bt);
        printf("\n");
    } else {
        /* stacked format: values do not fit on one line */
        printf("assert(%s) failed for\n\n", assertion);
        printf("%s: ", name_a);
        print_array(ha, elemsize, elem_bt);
        printf("\n");
        printf("%s: ", name_b);
        print_array(hb, elemsize, elem_bt);
        printf("\n");
    }

    /* highlight the first differing element, if any */
    {
        size_t j = 0;

        if (la != lb) {
            /* sequences of different lengths can never be equal */
            printf("first difference at index %llu: sequence length mismatch (%llu vs %llu)\n",
                   (unsigned long long)(la < lb ? la : lb),
                   (unsigned long long)la, (unsigned long long)lb);
            exit(255);
        }
        while (j < la &&
               elem_read((const char *)ha->ptr + j * elemsize, elemsize) ==
               elem_read((const char *)hb->ptr + j * elemsize, elemsize)) {
            ++j;
        }
        if (j < la) {
            printf("first difference at index %llu: %llu %s %llu\n",
                   (unsigned long long)j,
                   elem_read((const char *)ha->ptr + j * elemsize, elemsize), neg_op,
                   elem_read((const char *)hb->ptr + j * elemsize, elemsize));
        }
    }

    exit(255);
}