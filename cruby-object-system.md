# ***Inspiration***

# CRuby Object System Internals

## The Foundation: `VALUE`

Everything in Ruby is a `VALUE` — an unsigned integer the width of a pointer:

```c
// include/ruby/internal/value.h
typedef unsigned long VALUE;    // 8 bytes on 64-bit
typedef unsigned long ID;
```

It's NOT a pointer — it's a tagged integer that encodes either an **immediate value** (small int, symbol, true/false/nil, small float) or a **pointer to a heap-allocated struct**.

---

## Immediate (Tagged) Values

On 64-bit systems, the low bits of `VALUE` determine what it is without dereferencing anything:

```c
// include/ruby/internal/special_consts.h
enum ruby_special_consts {
    RUBY_Qfalse         = 0x00,   // 0000 0000
    RUBY_Qnil           = 0x04,   // 0000 0100
    RUBY_Qtrue          = 0x14,   // 0001 0100
    RUBY_Qundef         = 0x24,   // 0010 0100  (internal sentinel)
    RUBY_IMMEDIATE_MASK = 0x07,
    RUBY_FIXNUM_FLAG    = 0x01,   // bit 0 set → Fixnum
    RUBY_FLONUM_MASK    = 0x03,
    RUBY_FLONUM_FLAG    = 0x02,   // bits 1:0 = 10 → Flonum
    RUBY_SYMBOL_FLAG    = 0x0c,   // bits 7:0 = xxxx1100 → Symbol
    RUBY_SPECIAL_SHIFT  = 8
};
```

| Low-bit pattern | Type | Encoding |
|---|---|---|
| `xxxxxxx1` | **Fixnum** | `(long << 1) \| 1` — arithmetic shift right by 1 to decode |
| `xxxxxx10` | **Flonum** | IEEE 754 bits rotated left by 3, low 2 bits set to `10` |
| `xxxx1100` | **Static Symbol** | `(ID << 8) \| 0x0c` |
| `0x00` | **false** | Singleton |
| `0x04` | **nil** | Singleton |
| `0x14` | **true** | Singleton |
| All other aligned values | **Heap object pointer** | Dereference to get `RBasic*` |

**Fixnum encoding:**
```c
// Encode: VALUE = (i << 1) | 1
static inline VALUE RB_INT2FIX(long i) {
    return (VALUE)((unsigned long)i << 1) + RUBY_FIXNUM_FLAG;
}
// Decode: long = (SIGNED_VALUE)v >> 1   (arithmetic shift)
```

Fixnums cover `LONG_MIN/2` to `LONG_MAX/2` — on 64-bit that's ±4.6 × 10^18.

**Flonum encoding** (64-bit only): Doubles whose exponent bits 62–60 are `011` or `100` (covers roughly ±1.72×10⁻⁷⁷ to ±1.84×10⁷⁷) are bit-rotated and tagged inline. Others fall through to a heap-allocated `RFloat`.

---

## The Type Enum

```c
// include/ruby/internal/value_type.h
enum ruby_value_type {
    RUBY_T_NONE     = 0x00,    // non-object (swept slot)
    RUBY_T_OBJECT   = 0x01,    // struct RObject
    RUBY_T_CLASS    = 0x02,    // struct RClass
    RUBY_T_MODULE   = 0x03,    // struct RClass
    RUBY_T_FLOAT    = 0x04,    // struct RFloat
    RUBY_T_STRING   = 0x05,    // struct RString
    RUBY_T_REGEXP   = 0x06,    // struct RRegexp
    RUBY_T_ARRAY    = 0x07,    // struct RArray
    RUBY_T_HASH     = 0x08,    // struct RHash (opaque)
    RUBY_T_STRUCT   = 0x09,    // struct RStruct
    RUBY_T_BIGNUM   = 0x0a,    // struct RBignum
    RUBY_T_FILE     = 0x0b,    // struct RFile
    RUBY_T_DATA     = 0x0c,    // struct RTypedData
    RUBY_T_MATCH    = 0x0d,    // struct RMatch
    RUBY_T_COMPLEX  = 0x0e,    // struct RComplex
    RUBY_T_RATIONAL = 0x0f,    // struct RRational

    RUBY_T_NIL      = 0x11,    // Qnil
    RUBY_T_TRUE     = 0x12,    // Qtrue
    RUBY_T_FALSE    = 0x13,    // Qfalse
    RUBY_T_SYMBOL   = 0x14,    // Symbol
    RUBY_T_FIXNUM   = 0x15,    // Fixnum
    RUBY_T_UNDEF    = 0x16,    // Qundef

    RUBY_T_IMEMO    = 0x1a,    // internal memo objects
    RUBY_T_NODE     = 0x1b,    // AST node
    RUBY_T_ICLASS   = 0x1c,    // include class (hidden)
    RUBY_T_ZOMBIE   = 0x1d,    // swept but not yet finalized
    RUBY_T_MOVED    = 0x1e,    // forwarding ref (compaction)

    RUBY_T_MASK     = 0x1f     // 5-bit bitmask
};
```

For heap objects, the type is extracted from the flags: `RBASIC(obj)->flags & 0x1f`.

---

## `RBasic` — The Header Every Heap Object Shares

```c
// include/ruby/internal/core/rbasic.h
struct RBasic {
    VALUE flags;
    const VALUE klass;
};
```

16 bytes on 64-bit. Every heap-allocated Ruby object starts with this.

### `flags` bit layout (64-bit)

```
 63        32 31                                  12 11       5 4    0
┌────────────┬──────────────────────────────────────┬──────────┬──────┐
│  shape_id  │  FL_USER0 ... FL_USER19              │ GC/misc  │ type │
│  (32 bits) │  (20 type-specific flag bits)        │  flags   │(5bit)│
└────────────┴──────────────────────────────────────┴──────────┴──────┘
```

```c
// include/ruby/internal/fl_type.h
enum ruby_fl_type {
    // bits 0-4:  type (RUBY_T_MASK)
    RUBY_FL_WB_PROTECTED   = (1<<5),   // write-barrier protected
    RUBY_FL_PROMOTED       = (1<<5),   // promoted to old generation (same bit)
    RUBY_FL_USERPRIV0      = (1<<6),   // type-dependent
    RUBY_FL_FINALIZE       = (1<<7),   // has finalizer
    RUBY_FL_SHAREABLE      = (1<<8),   // ractor-shareable
    RUBY_FL_WEAK_REFERENCE = (1<<9),   // weak references
    RUBY_FL_UNUSED10       = (1<<10),
    RUBY_FL_FREEZE         = (1<<11),  // object is frozen

    RUBY_FL_USHIFT         = 12,       // user flags start here
    RUBY_FL_USER0          = (1<<12),
    RUBY_FL_USER1          = (1<<13),
    // ... through ...
    RUBY_FL_USER19         = (1<<31),
};
```

Bits 32–63 hold the **shape ID** (see below).

---

## `RObject` — Generic Ruby Objects

```c
// include/ruby/internal/core/robject.h
enum ruby_robject_flags {
    ROBJECT_HEAP = RUBY_FL_USER4   // ivars in external buffer
};

struct RObject {
    struct RBasic basic;
    union {
        struct {
            VALUE *fields;         // heap-allocated ivar array
        } heap;
        VALUE ary[1];              // embedded ivars (flexible array)
    } as;
};
```

If `ROBJECT_HEAP` is clear, instance variables are stored **directly inside the object slot** (`as.ary`). A minimum-size slot (40 bytes) fits 3 VALUEs after the RBasic header. With Variable Width Allocation, larger slots (64, 80, 128... bytes) can embed more.

If the object outgrows its slot, `ROBJECT_HEAP` is set and `as.heap.fields` points to a `malloc`'d array.

### The Shape System

Shapes replaced the old per-class `iv_index_tbl` hash. Each shape represents a unique sequence of ivar additions:

```c
// shape.h
typedef uint32_t shape_id_t;

// shape_id_t bit layout:
//   bits 0-18:  shape index in global shape list
//   bits 19-23: heap index (capacity class, T_OBJECT only)
//   bit 24:     frozen flag
//   bit 25:     has object_id
//   bit 26:     too_complex (fallback to st_table)

struct rb_shape {
    VALUE edges;                    // ID table: ivar name → child shape
    ID edge_name;                   // ivar name for this transition
    redblack_node_t *ancestor_index;
    shape_id_t parent_id;
    attr_index_t next_field_index;  // number of ivars at this shape
    attr_index_t capacity;
    uint8_t type;                   // SHAPE_ROOT, SHAPE_IVAR, or SHAPE_OBJ_ID
};

enum shape_type {
    SHAPE_ROOT,
    SHAPE_IVAR,
    SHAPE_OBJ_ID,
};
```

When you do `obj.@x = 1; obj.@y = 2`, the object transitions through shapes: root → shape(@x, index=0) → shape(@y, index=1). The `fields` array stores values at the index dictated by the shape tree. No hash table lookup needed for ivar access — it's an array index.

---

## `RClass` — Classes and Modules

The public header is opaque. The real definition is internal:

```c
// internal/class.h
struct RClass {
    struct RBasic basic;
    VALUE object_id;
};

struct rb_classext_struct {
    const rb_box_t *box;
    VALUE super;                              // superclass
    VALUE fields_obj;
    struct rb_id_table *m_tbl;                // method table
    struct rb_id_table *const_tbl;            // constant table
    struct rb_id_table *callable_m_tbl;       // callable method cache
    VALUE cc_tbl;                             // call cache table
    struct rb_id_table *cvc_tbl;              // class variable cache
    VALUE *superclasses;                      // linearized superclass array
    struct rb_subclass_entry *subclasses;
    struct rb_subclass_entry *subclass_entry;
    struct rb_subclass_entry *module_subclass_entry;
    const VALUE origin_;                      // for prepend
    const VALUE refined_class;
    union {
        struct { rb_alloc_func_t allocator; } class;
        struct { VALUE attached_object; } singleton_class;
        struct { const VALUE includer; } iclass;
    } as;
    attr_index_t max_iv_count;
    uint16_t superclass_depth;
    unsigned char variation_count;
    bool permanent_classpath : 1;
    bool cloned : 1;
    bool shared_const_tbl : 1;
    bool iclass_is_origin : 1;
    bool iclass_origin_shared_mtbl : 1;
    bool superclasses_with_self : 1;
    VALUE classpath;
};
```

The `rb_classext_struct` is allocated **contiguously** right after `RClass`:

```c
struct RClass_and_rb_classext_t {
    struct RClass rclass;
    rb_classext_t classext;
};

#define RCLASS_EXT_PRIME(c) (&((struct RClass_and_rb_classext_t*)(c))->classext)
```

---

## `RString`

```c
// include/ruby/internal/core/rstring.h
enum ruby_rstring_flags {
    RSTRING_NOEMBED = RUBY_FL_USER1,   // NOT embedded — data on heap
    RSTRING_FSTR    = RUBY_FL_USER17   // frozen interned string
};

struct RString {
    struct RBasic basic;
    long len;
    union {
        struct {
            char *ptr;
            union {
                long capa;
                VALUE shared;          // shared string parent
            } aux;
        } heap;
        struct {
            char ary[1];               // embedded string bytes
        } embed;
    } as;
};
```

If `RSTRING_NOEMBED` is **clear**, the string bytes live inside `as.embed.ary`. With a 40-byte slot: 16 (RBasic) + 8 (len) = 24 bytes of header, leaving ~15 usable bytes for embedded chars. Larger VWA slots give more room.

---

## `RArray`

```c
// include/ruby/internal/core/rarray.h
enum ruby_rarray_flags {
    RARRAY_EMBED_FLAG     = RUBY_FL_USER1,
    RARRAY_EMBED_LEN_MASK = RUBY_FL_USER9 | ... | RUBY_FL_USER3  // 7 bits
};
enum ruby_rarray_consts {
    RARRAY_EMBED_LEN_SHIFT = 15   // FL_USHIFT + 3
};

struct RArray {
    struct RBasic basic;
    union {
        struct {
            long len;
            union {
                long capa;
                const VALUE shared_root;
            } aux;
            const VALUE *ptr;
        } heap;
        const VALUE ary[1];           // embedded elements
    } as;
};
```

When `RARRAY_EMBED_FLAG` is set, length is packed into flags bits 15–21 and elements are stored inline in `as.ary`. A 40-byte slot fits 3 embedded VALUEs.

---

## `RFloat`

```c
// internal/numeric.h
struct RFloat {
    struct RBasic basic;
    double float_value;    // on 64-bit, rb_float_value_type = double
};
```

Most floats never reach `RFloat` — they're encoded as flonums. Only out-of-range doubles (very large/small exponents, NaN, -0.0) get heap-allocated.

---

## `RBignum`

```c
// internal/bignum.h
struct RBignum {
    struct RBasic basic;
    union {
        struct {
            size_t len;
            BDIGIT *digits;
        } heap;
        BDIGIT ary[1];        // embedded digits
    } as;
};
// BIGNUM_EMBED_FLAG = FL_USER2
// BIGNUM_SIGN_BIT   = FL_USER1 (1 = positive)
```

---

## `RRegexp`

```c
// include/ruby/internal/core/rregexp.h
struct RRegexp {
    struct RBasic basic;
    struct re_pattern_buffer *ptr;   // compiled Onigmo pattern
    const VALUE src;                 // source string
    unsigned long usecnt;
};
```

---

## `RData` (deprecated) / `RTypedData`

```c
// include/ruby/internal/core/rdata.h
struct RData {
    struct RBasic basic;
    RUBY_DATA_FUNC dmark;
    RUBY_DATA_FUNC dfree;
    void *data;
};

// include/ruby/internal/core/rtypeddata.h
struct RTypedData {
    struct RBasic basic;
    VALUE fields_obj;
    const VALUE type;       // rb_data_type_t* with low bit = embedded flag
    void *data;
};

struct rb_data_type_struct {
    const char *wrap_struct_name;
    struct {
        RUBY_DATA_FUNC dmark;
        RUBY_DATA_FUNC dfree;
        size_t (*dsize)(const void *);
        RUBY_DATA_FUNC dcompact;
        void (*handle_weak_references)(void *);
        void *reserved[7];
    } function;
    const rb_data_type_t *parent;
    void *data;
    VALUE flags;
};
```

`RTypedData` is the modern replacement. The `data` field is at the same offset in both structs (static assertion enforced). When `type & 1` is set, the wrapped C data is embedded inline starting at `&data` instead of being a separate allocation.

---

## `RStruct`, `RComplex`, `RRational`

```c
// internal/struct.h
struct RStruct {
    struct RBasic basic;
    union {
        struct {
            long len;
            const VALUE *ptr;
            VALUE fields_obj;
        } heap;
        const VALUE ary[1];   // embedded members
    } as;
};

// internal/complex.h
struct RComplex {
    struct RBasic basic;
    VALUE real;
    VALUE imag;
};

// internal/rational.h
struct RRational {
    struct RBasic basic;
    VALUE num;
    VALUE den;
};
```

---

## The RVALUE Union (Historical)

Pre-Ruby 3.2 had a union in `gc.c` overlaying all the R* structs at the same address:

```c
typedef union RVALUE {
    struct RBasic  basic;
    struct RObject object;
    struct RClass  klass;
    struct RFloat  flonum;
    struct RString string;
    struct RArray  array;
    struct RRegexp regexp;
    struct RHash   hash;
    struct RData   data;
    struct RTypedData typeddata;
    struct RStruct rstruct;
    struct RBignum bignum;
    struct RFile   file;
    struct RMatch  match;
    struct RRational rational;
    struct RComplex complex;
} RVALUE;
```

This is **gone** in modern Ruby. Variable Width Allocation replaced it with multiple heap pools of different slot sizes (40, 64, 80, 128, 160, 256... bytes). Objects are placed in the smallest slot that fits.

---

## The Big Picture

```
VALUE (64 bits)
├─ bit 0 set?          → Fixnum (value >> 1)
├─ bits 1:0 == 10?     → Flonum (rotated double)
├─ bits 7:0 == xxxx1100? → Symbol (ID << 8 | 0x0c)
├─ == 0x00?            → false
├─ == 0x04?            → nil
├─ == 0x14?            → true
└─ otherwise           → heap pointer to:
     ┌──────────────────┐
     │ RBasic           │  ← every heap object
     │   flags (64-bit) │  ← [type:5][gc/misc:7][user:20][shape_id:32]
     │   klass          │  ← VALUE pointing to the object's class
     ├──────────────────┤
     │ type-specific    │  ← RObject, RString, RArray, RClass, etc.
     │ payload          │     (may be embedded in-slot or heap-allocated)
     └──────────────────┘
```

The key insight: Ruby's object model is a **tagged pointer** scheme where common small values avoid heap allocation entirely, and heap objects all share a uniform 16-byte header (`RBasic`) whose flag bits encode the type, GC state, frozen status, and (on 64-bit) the shape ID — enabling O(1) instance variable access through the shape tree instead of hash table lookups.

---

# CRuby Class Hierarchy Implementation

## The Core Idea: A Linked List of Method Tables

The entire class hierarchy is a **singly-linked list** connected by `super` pointers. Method lookup is a linear walk of this list. Everything else — `include`, `prepend`, singleton classes — is implemented by **inserting proxy nodes** (IClasses) into this list.

---

## 1. The `klass` Pointer: Object → Class

Every heap object's `RBasic.klass` points to the **first entry in its method lookup chain**:

```c
struct RBasic {
    VALUE flags;
    const VALUE klass;   // points to class (or singleton class, or iclass)
};
```

When you call `obj.class` in Ruby, CRuby calls `rb_obj_class()`, which skips past "fake" classes (singleton classes and IClasses) to find the real one:

```c
static inline VALUE
class_real(VALUE cl)
{
    while (RB_UNLIKELY(fake_class_p(cl))) {
        cl = RCLASS_SUPER(cl);
    }
    return cl;
}
```

---

## 2. The `super` Chain

`rb_classext_struct.super` links classes together. For a simple hierarchy:

```ruby
class Animal; end
class Dog < Animal; end
```

The internal chain is:

```
Dog --super--> Animal --super--> Object --super--> BasicObject --super--> nil
```

Method lookup (`search_method0`) just walks this chain:

```c
static inline rb_method_entry_t*
search_method0(VALUE klass, ID id, VALUE *defined_class_ptr, bool skip_refined)
{
    rb_method_entry_t *me = NULL;
    for (; klass; klass = RCLASS_SUPER(klass)) {
        if ((me = lookup_method_table(klass, id)) != 0) {
            break;
        }
    }
    if (defined_class_ptr) *defined_class_ptr = klass;
    return me;
}
```

Each node has an `m_tbl` (an `rb_id_table` mapping method name IDs to `rb_method_entry_t*`). Walk until you find a hit or reach nil.

---

## 3. `include` — Inserting IClasses

When you `include` a module, CRuby creates a **T_ICLASS** (inclusion class) — a transparent proxy that **shares the module's method table pointer**:

```c
VALUE
rb_include_class_new(VALUE module, VALUE super)
{
    VALUE klass = class_alloc(T_ICLASS, rb_cClass);
    RCLASS_SET_M_TBL(klass, RCLASS_M_TBL(module));   // SHARED pointer, not a copy
    RCLASS_SET_CONST_TBL(klass, RCLASS_CONST_TBL(module), true);
    class_associate_super(klass, super, true);
    RBASIC_SET_CLASS(klass, module);  // iclass.klass -> original module
    return klass;
}
```

The ICLASS is spliced into the super chain. Given:

```ruby
class Dog < Animal
  include Walkable
  include Swimmable
end
```

The chain becomes:

```
Dog --super--> ICLASS(Swimmable) --super--> ICLASS(Walkable) --super--> Animal --super--> Object --super--> ...
```

Because the ICLASS **shares** the module's `m_tbl` pointer (not a copy), adding a method to `Walkable` later is immediately visible through the ICLASS.

The insertion logic (`do_include_modules_at`) checks for duplicates by comparing `m_tbl` pointers — if an ICLASS for that module already exists in the chain, it skips it.

---

## 4. `prepend` — The Origin Trick

`prepend` must insert methods **before** the class's own methods. This requires a clever restructuring:

```c
static bool
ensure_origin(VALUE klass)
{
    VALUE origin = RCLASS_ORIGIN(klass);
    if (origin == klass) {
        origin = class_alloc(T_ICLASS, klass);
        RCLASS_SET_M_TBL(origin, RCLASS_M_TBL(klass));   // move m_tbl to origin
        rb_class_set_super(origin, RCLASS_SUPER(klass));   // origin.super = old super
        rb_class_set_super(klass, origin);                 // klass.super = origin
        RCLASS_WRITE_ORIGIN(klass, origin);
        class_clear_method_table(klass);                   // klass gets empty m_tbl
        return true;
    }
    return false;
}
```

Before prepend:
```
Foo (m_tbl has #bar, #baz) --super--> Object
```

After `prepend M`:
```
Foo (empty m_tbl) --super--> ICLASS(M) --super--> ICLASS(origin, has Foo's original m_tbl) --super--> Object
```

The class itself becomes a hollow shell. Its original methods move into the "origin" ICLASS. The prepended module's ICLASS is inserted **before** the origin, so prepended methods win.

---

## 5. Singleton Classes (Metaclasses)

A singleton class is a `T_CLASS` with `FL_SINGLETON` set. Created lazily:

```c
static inline VALUE
make_singleton_class(VALUE obj)
{
    VALUE orig_class = METACLASS_OF(obj);
    VALUE klass = class_alloc0(T_CLASS, rb_cClass, ...);
    FL_SET(klass, FL_SINGLETON);
    class_associate_super(klass, orig_class, true);  // super = original class
    RBASIC_SET_CLASS(obj, klass);                     // obj.klass = singleton
    rb_singleton_class_attached(klass, obj);          // attached_object = obj
    return klass;
}
```

Before `def obj.foo`:
```
obj.klass --> Dog --> Animal --> Object
```

After:
```
obj.klass --> #<Class:obj> --super--> Dog --> Animal --> Object
```

**For Class objects**, metaclasses chain parallelly to the class hierarchy:

```c
static inline VALUE
make_metaclass(VALUE klass)
{
    // metaclass.super = metaclass of klass's superclass
    super = RCLASS_SUPER(klass);
    while (RB_TYPE_P(super, T_ICLASS)) super = RCLASS_SUPER(super);
    class_associate_super(metaclass, ENSURE_EIGENCLASS(super), true);
    // ...
}
```

This gives you:

```
Dog  ──klass──>  #<Class:Dog>  ──super──>  #<Class:Animal>  ──super──>  #<Class:Object>  ──super──>  Class
 │                                            │                            │
 └──super──>  Animal  ─────super────>  Object  ─────super────>  BasicObject
```

This is why class methods are inherited: `Dog.some_class_method` walks Dog's metaclass chain, which parallels the class chain.

---

## 6. The `superclasses` Array — O(1) `is_a?`

Walking the super chain for `is_a?` would be O(n). CRuby caches a **linearized array** of `T_CLASS` ancestors (skipping IClasses):

```c
// In rb_classext_struct:
VALUE *superclasses;       // [BasicObject, Object, Animal, Dog]
uint16_t superclass_depth; // index of this class in the array
```

This enables O(1) ancestry checks between classes:

```c
static VALUE
class_search_class_ancestor(VALUE cl, VALUE c)
{
    size_t c_depth = RCLASS_SUPERCLASS_DEPTH(c);
    size_t cl_depth = RCLASS_SUPERCLASS_DEPTH(cl);
    VALUE *classes = RCLASS_SUPERCLASSES(cl);

    if (cl_depth <= c_depth) return Qfalse;
    return RBOOL(classes[c_depth] == c);   // single array index!
}
```

"Is `Dog` a subclass of `Animal`?" → check if `Dog.superclasses[Animal.depth] == Animal`. One comparison, no loop.

For module checks (`is_a? SomeModule`), it falls back to the linear walk comparing `m_tbl` pointers.

---

## 7. The Subclass Linked List

Each class maintains a doubly-linked list of direct subclasses:

```c
struct rb_subclass_entry {
    VALUE klass;
    struct rb_subclass_entry *next;
    struct rb_subclass_entry *prev;
};
```

Used for:
- `Class#subclasses` (Ruby-level)
- Propagating method cache invalidations down the tree
- Propagating `include`/`prepend` to existing includers of a module

---

## 8. `Module#ancestors` — Reading the Chain

```c
VALUE rb_mod_ancestors(VALUE mod)
{
    VALUE p, ary = rb_ary_new();
    for (p = mod; p; p = RCLASS_SUPER(p)) {
        if (p != RCLASS_ORIGIN(p)) continue;  // skip prepended class (origin represents it)
        if (BUILTIN_TYPE(p) == T_ICLASS) {
            rb_ary_push(ary, METACLASS_OF(p)); // iclass → push original module
        } else {
            rb_ary_push(ary, p);               // class → push itself
        }
    }
    return ary;
}
```

---

## 9. Bootstrap: The Chicken-and-Egg

The root of the hierarchy is created in `Init_class_hierarchy`:

```c
void Init_class_hierarchy(void)
{
    rb_cBasicObject = boot_defclass("BasicObject", 0);
    rb_cObject      = boot_defclass("Object", rb_cBasicObject);
    rb_cModule      = boot_defclass("Module", rb_cObject);
    rb_cClass       = boot_defclass("Class",  rb_cModule);

    // Fix up: Class didn't exist when these were created
    RBASIC_SET_CLASS(rb_cClass, rb_cClass);         // Class.class = Class
    RBASIC_SET_CLASS(rb_cModule, rb_cClass);        // Module.class = Class
    RBASIC_SET_CLASS(rb_cObject, rb_cClass);        // Object.class = Class
    RBASIC_SET_CLASS(rb_cBasicObject, rb_cClass);   // BasicObject.class = Class
}
```

`Class` is an instance of itself. `BasicObject` has `super = nil`. Everything else hangs off this.

---

## 10. Method Lookup with Caching

The core lookup is simple, but CRuby adds caching via `callable_method_entry_or_negative`:

```c
static const rb_callable_method_entry_t *
callable_method_entry_or_negative(VALUE klass, ID mid, VALUE *defined_class_ptr)
{
    // Fast path: lock-free read from cc_tbl cache
    VALUE cc_tbl = RUBY_ATOMIC_VALUE_LOAD(RCLASS_WRITABLE_CC_TBL(klass));
    if (cc_tbl) {
        VALUE ccs_data;
        if (rb_managed_id_table_lookup(cc_tbl, mid, &ccs_data)) {
            struct rb_class_cc_entries *ccs = (struct rb_class_cc_entries *)ccs_data;
            if (LIKELY(!METHOD_ENTRY_INVALIDATED(ccs->cme))) {
                return ccs->cme;  // cache hit!
            }
        }
    }

    // Slow path: lock and search the super chain
    RB_VM_LOCKING() {
        cme = cached_callable_method_entry(klass, mid);
        if (!cme) {
            rb_method_entry_t *me = search_method(klass, mid, &defined_class);
            if (me) cme = prepare_callable_method_entry(defined_class, mid, me, TRUE);
            else cme = negative_cme(mid);  // cache the miss too
            cache_callable_method_entry(klass, mid, cme);
        }
    }
    return cme;
}
```

---

## The Full Picture

```
                     ┌─────────────────────────────────────────────┐
                     │             METHOD LOOKUP CHAIN             │
                     │   (what RCLASS_SUPER actually traverses)    │
                     └─────────────────────────────────────────────┘

obj.klass
   │
   ▼
 #<Class:obj>          ← singleton class (if one exists)
   │ super
   ▼
 Dog                   ← real class (m_tbl has Dog's methods)
   │ super
   ▼
 ICLASS(Swimmable)     ← shares Swimmable's m_tbl pointer
   │ super
   ▼
 ICLASS(Walkable)      ← shares Walkable's m_tbl pointer
   │ super
   ▼
 Animal                ← superclass
   │ super
   ▼
 ICLASS(Comparable)    ← included in Animal
   │ super
   ▼
 Object
   │ super
   ▼
 ICLASS(Kernel)        ← Kernel is included in Object
   │ super
   ▼
 BasicObject
   │ super
   ▼
  nil
```

Every box is a `struct RClass` (or `T_ICLASS`) with an `m_tbl`. Method dispatch is just: walk down, check each `m_tbl`, return the first hit. Everything else — mixins, prepends, singleton methods — is just creative insertion of nodes into this linked list.
