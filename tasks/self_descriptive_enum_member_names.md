# Adjustments to names

Make sure these comments mention that they can be treated as both
modifier and qualifiers.

```c
#define VT_BITFIELD    0x0080  /**< bitfield modifier */
#define VT_CONSTANT    0x0100  /**< const modifier */
#define VT_VOLATILE    0x0200  /**< volatile modifier */
#define VT_RESTRICT    0x0400  /**< restrict modifier */
#define VT_IMMUTABLE   0x00040000 /**< D-style immutable modifier */
```

via renamings to

```c
#define VT_MOD_BITFIELD    0x0080  /**< bitfield modifier qualifier */
#define VT_MOD_CONSTANT    0x0100  /**< const modifier qualifier */
#define VT_MOD_VOLATILE    0x0200  /**< volatile modifier qualifier */
#define VT_MOD_RESTRICT    0x0400  /**< restrict modifier qualifier */
#define VT_MOD_IMMUTABLE   0x00040000 /**< D-style immutable modifier qualifier */
```

Apply the same renamings to `VT_...` constants:

- VT_BT_VOID, ...

- VT_MOD_,

Qualifiers:
- VT_QUA_, ...

Storage qualifiers:
- VT_QUA_STO_, ...

