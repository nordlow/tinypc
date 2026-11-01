# Self-Descriptive C Enum Member and Type Attribute Defines

This task outlines the curation and extension of naming conventions for
C enum types, type descriptors, bitfields, storage qualifiers, value
locations, and masks in the TCC compiler (`tcc.h`), categorizing them
clearly into **plain values**, **bitfield-values**, and **masks**.

## Motivation

TCC uses a packed integer bitmask (`CType.t` and value flags) to encode
types, modifiers, storage classes, and structural qualifiers (such as
enumerations, unions, bitfields, assembler symbols, and basic types).
Currently, many of these `#define`s lack explicit prefixes or categorical
clarity (e.g., mixing plain values, bitfields, and masks under flat `VT_`
prefixes).

To improve maintainability and self-descriptiveness, all relevant
`#define`s are classified into distinct naming categories:
1. **Plain Values**: Concrete type tags, enum indicators, constant
   values, and value locations.
2. **Bitfield-Values**: Shift amounts, bitfield dimensions, and
   bitfield representation encodings.
3. **Masks**: Bitmasks used for extracting or testing components of the
   type word or value flags.

---

## 1. Plain Values and Value Locations

Value location states (how a value is stored or represented at runtime):

```c
#define VT_CONST     0x0030  /**< Constant in vc (plain value) */
#define VT_LLOCAL    0x0031  /**< Lvalue, offset on stack (plain value) */
#define VT_LOCAL     0x0032  /**< Offset on stack (plain value) */
#define VT_CMP       0x0033  /**< Stored in processor flags (plain value) */
#define VT_JMP       0x0034  /**< Consequence of jmp true (plain value) */
#define VT_JMPI      0x0035  /**< Consequence of jmp false (plain value) */
```

Proposed renamings: `VT_VAL_CONST`, `VT_VAL_LLOCAL`, `VT_VAL_LOCAL`,
`VT_VAL_CMP`, `VT_VAL_JMP`, `VT_VAL_JMPI`.

Basic type tags:

```c
#define VT_VOID             0  /**< Void type (plain value) */
#define VT_BYTE             1  /**< Signed byte type (plain value) */
#define VT_SHORT            2  /**< Short type (plain value) */
#define VT_INT              3  /**< Integer type (plain value) */
#define VT_LLONG            4  /**< 64-bit integer (plain value) */
#define VT_PTR              5  /**< Pointer (plain value) */
#define VT_FUNC             6  /**< Function type (plain value) */
#define VT_STRUCT           7  /**< Struct/union definition (plain value) */
#define VT_FLOAT            8  /**< IEEE float (plain value) */
#define VT_DOUBLE           9  /**< IEEE double (plain value) */
#define VT_LDOUBLE         10  /**< IEEE long double (plain value) */
#define VT_BOOL            11  /**< ISOC99 boolean type (plain value) */
#define VT_SLICE           12  /**< D-style slice type (plain value) */
#define VT_QLONG           13  /**< 128-bit integer ABI (plain value) */
#define VT_QFLOAT          14  /**< 128-bit float ABI (plain value) */
#define VT_AUTO            15  /**< Auto type inference (plain value) */
```

Proposed renamings: `VT_BT_VOID`, `VT_BT_BYTE`, `VT_BT_INT`, etc.

Structural, Enum, and Assembler Type Tags:

```c
#define VT_UNION           (1 << VT_STRUCT_SHIFT | VT_STRUCT) /**< Union type indicator */
#define VT_ENUM            (2 << VT_STRUCT_SHIFT) /**< Integral type is an enum really */
#define VT_ENUM_VAL        (3 << VT_STRUCT_SHIFT) /**< Integral type is an enum constant really */
#define VT_ASM             (VT_VOID | 4 << VT_STRUCT_SHIFT) /**< Assembler symbol type marker */
#define VT_ASM_FUNC        (VT_VOID | 5 << VT_STRUCT_SHIFT) /**< Assembler function symbol marker */
#define VT_BT_ARRAY        (6 << VT_STRUCT_SHIFT) /**< Array base type indicator from typedef/typeof */
```

---

## 2. Bitfield-Values, Modifiers, and Qualifiers

Type modifiers, storage qualifiers, and bitfield attributes:

```c
#define VT_UNSIGNED        0x0010  /**< Unsigned type modifier */
#define VT_DEFSIGN         0x0020  /**< Explicitly signed or unsigned */
#define VT_ARRAY           0x0040  /**< Array type modifier */
#define VT_BITFIELD        0x0080  /**< Bitfield modifier */
#define VT_QUALIFIER_CONSTANT        0x0100  /**< Const modifier */
#define VT_QUALIFIER_VOLATILE        0x0200  /**< Volatile modifier */
#define VT_QUALIFIER_RESTRICT        0x0400  /**< Restrict modifier */
#define VT_QUALIFIER_IMMUTABLE       0x00040000 /**< D-style immutable modifier */
#define VT_VLA             0x0800  /**< VLA type modifier */
#define VT_LONG            0x1000  /**< Long type modifier */

#define VT_STORAGE_EXTERN          0x00002000  /**< Extern storage specifier */
#define VT_STORAGE_STATIC          0x00004000  /**< Static storage specifier */
#define VT_STORAGE_TYPEDEF         0x00008000  /**< Typedef storage specifier */
#define VT_STORAGE_INLINE          0x00010000  /**< Inline storage specifier */
#define VT_STORAGE_TLS             0x00020000  /**< Thread-local storage specifier */
```

Proposed renamings:
- Modifiers: `VT_MOD_UNSIGNED`, `VT_MOD_BITFIELD`, `VT_MOD_CONSTANT`,
  `VT_MOD_VOLATILE`, `VT_MOD_RESTRICT`, `VT_MOD_IMMUTABLE`, `VT_MOD_LONG`,
  `VT_MOD_VLA`, `VT_MOD_ARRAY`.
- Storage: `VT_STO_EXTERN`, `VT_STO_STATIC`, `VT_STO_TYPEDEF`,
  `VT_STO_INLINE`, `VT_STO_TLS`.

Bitfield Shift and Dimension Helpers:

```c
#define VT_STRUCT_SHIFT    20     /**< Shift for bitfield / structure metadata */
#define BIT_POS(t)         (((t) >> VT_STRUCT_SHIFT) & 0x3f) /**< Extract bit position */
#define BIT_SIZE(t)        (((t) >> (VT_STRUCT_SHIFT + 6)) & 0x3f) /**< Extract bit size */
#define BFVAL(M,N)         ((unsigned)((M) & ~((M) << 1)) * (N)) /**< Pseudo-bitfield value helper */
```

---

## 3. Masks and Extraction Predicates

Masks used for bitwise isolation of subfields:

```c
#define VT_VALMASK         0x003f  /**< Mask for value location */
#define VT_BTYPE           0x000f  /**< Mask for basic type */
#define VT_STRUCT_MASK     (((1U << (6+6)) - 1) << VT_STRUCT_SHIFT | VT_BITFIELD) /**< Mask for structure/union/enum representation */
#define VT_QUALIFIER            (VT_QUALIFIER_CONSTANT | VT_QUALIFIER_VOLATILE | VT_QUALIFIER_RESTRICT | VT_QUALIFIER_IMMUTABLE) /**< Mask for all qualifiers */
#define VT_STORAGE         (VT_STORAGE_EXTERN | VT_STORAGE_STATIC | VT_STORAGE_TYPEDEF | VT_STORAGE_INLINE | VT_STORAGE_TLS) /**< Mask for storage specifiers */
#define VT_TYPE            (~(VT_STORAGE|VT_STRUCT_MASK)) /**< Mask covering base types excluding storage and bitfield */
```

Proposed renamings: `VT_MASK_VAL`, `VT_MASK_BTYPE`, `VT_MASK_STRUCT`,
`VT_MASK_QUAL`, `VT_MASK_STORAGE`, `VT_MASK_TYPE`.

Type Check Predicates:

```c
#define IS_ENUM(t)         ((t & VT_STRUCT_MASK) == VT_ENUM)
#define IS_ENUM_VAL(t)     ((t & VT_STRUCT_MASK) == VT_ENUM_VAL)
#define IS_UNION(t)        ((t & (VT_STRUCT_MASK|VT_BTYPE)) == VT_UNION)
#define IS_ASM_SYM(sym)    (((sym)->type.t & ((VT_BTYPE|VT_STRUCT_MASK) & ~(1<<VT_STRUCT_SHIFT))) == VT_ASM)
#define IS_ASM_FUNC(t)     ((t & (VT_BTYPE|VT_STRUCT_MASK)) == VT_ASM_FUNC)
#define IS_BT_ARRAY(t)     ((t & VT_STRUCT_MASK) == VT_BT_ARRAY)
```
