/* Type tuples (P1): declarations, typedefs, layout, access, identity.  */

#include <stdio.h>

#define PAIR (int, char)
#define ELEMS int, char
#define ID(x) x

typedef (int, char) Pair;
typedef ((int, char), double) Nested;
typedef (int,) One;

typedef PAIR MacroPair;
typedef (ELEMS) ElemPair;
typedef ID((int, char)) IdPair;

static Pair g_pair = {1, 'a'};
Nested g_nested = {{2, 'b'}, 3.5};
One g_one = {7};

struct Box { (int, char) v; int tag; };
struct Box g_box = {{4, 'd'}, 5};

struct Eq { int a; char b; };

Pair make_pair(int i, char c)
{
    Pair r = {i, c};
    return r;
}

int sum_pair(Pair p)
{
    return p[0] + p[1];
}

int main(void)
{
    Pair p = {1, 'a'};
    Nested n = {{2, 'b'}, 3.5};
    One o = {7};
    Pair arr[2] = {{8, 'h'}, {9, 'i'}};
    Pair *pp = &p;
    const (int, char) cp = {10, 'j'};
    struct Box box = {{11, 'k'}, 12};
    Pair literal = (Pair){13, 'l'};
    MacroPair m = {14, 'm'};
    ElemPair e = {15, 'n'};
    IdPair i = {16, 'o'};
    Pair q = {17, 'p'};
    Pair copy;

    printf("p %d %c\n", p[0], p[1]);
    printf("n %d %c %g\n", n[0][0], n[0][1], n[1]);
    printf("o %d\n", o[0]);
    printf("g %d %c %d %c %g %d\n", g_pair[0], g_pair[1],
           g_nested[0][0], g_nested[0][1], g_nested[1], g_one[0]);
    printf("box %d %c %d\n", g_box.v[0], g_box.v[1], g_box.tag);
    printf("local box %d %c %d\n", box.v[0], box.v[1], box.tag);
    printf("arr %d %c %d %c\n", arr[0][0], arr[0][1], arr[1][0], arr[1][1]);
    printf("ptr %d %c\n", (*pp)[0], (*pp)[1]);
    printf("const %d %c\n", cp[0], cp[1]);
    printf("literal %d %c\n", literal[0], literal[1]);
    printf("macro %d %c %d %c %d %c\n", m[0], m[1], e[0], e[1], i[0], i[1]);

    printf("size %d %d %d %d %d\n",
           (int)sizeof(Pair), (int)_Alignof(Pair),
           (int)sizeof(Nested), (int)sizeof(One), (int)sizeof(struct Box));
    printf("size expr %d %d\n", (int)sizeof((int, char)),
           (int)_Alignof((int, char)));
    printf("size typeof %d\n", (int)sizeof(typeof(p)));

    p[0] = 21;
    printf("assign %d %c\n", p[0], p[1]);
    printf("call %d %d %c\n", sum_pair(p), make_pair(22, 'm')[0],
           make_pair(22, 'm')[1]);
    copy = make_pair(23, 'n');
    printf("return %d %c\n", copy[0], copy[1]);
    q = p;
    printf("copy %d %c\n", q[0], q[1]);

    printf("generic %d %d\n", _Generic(p, Pair: 1, default: 0),
           _Generic(p, (int, char): 2, default: 0));
    printf("compat %d %d %d %d\n",
           __builtin_types_compatible_p(Pair, (int, char)),
           __builtin_types_compatible_p(Pair, (int, int)),
           __builtin_types_compatible_p(Pair, struct Eq),
           __builtin_types_compatible_p(((int, char), double), Nested));
    return 0;
}
