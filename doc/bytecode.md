# Solace Bytecode (`.slb`)

Solace can save a compiled program to a JSON bytecode file and run it back
without re-parsing or type-checking the source. Dump a file with:

```
solace --dump hello.slc      # writes hello.slc.slb, then runs hello.slc
solace hello.slc.slb        # run the dumped bytecode directly
```

The format is a JSON mirror of the interpreter's own in-memory structures. The
bytecode itself is just a list of integers - byte-for-byte the same bytes the
VM executes. Because it is plain JSON, any language with a JSON library (Python,
Ruby, and eventually Solace itself) can produce or consume `.slb` files.

## Layout

The root of a `.slb` file is a **prototype** (what `compile()` produces, effectively an ObjFunction). A
nested function is just a prototype referenced from another prototype's
`constants`, so the file is one recursively nested object:

```
document = prototype (the root, what compile() yields)

prototype = { "name": string|null,
              "upvalueCount": int,
              "parameters": [type...],
              "returnType": type|null,
              "code": [int...],            raw bytecode bytes
              "lines": [[line,count]...],  RLE, mirrors LineInfoArray
              "constants": [value...] }

type = { "name": string|null, "next": type|null,
         "generics": type|null }      (mirrors Type)

value = { "type": <ValueType>, ... }  (mirrors Value)

obj = { "type": <ObjType>, ... }    (mirrors Obj)
```

A `.slb` file also carries `"version": 1` at the root; the loader refuses to
run any other version. This ensures forward-compatibility.

## `code` - the bytecode list

Each entry is one instruction byte (0–255) of the VM's opcode set, in order.
Operands are inline, exactly as the interpreter stores them.

Jump offsets are relative to the instruction that follows the offset, matching
the interpreter (`frame->ip += offset` after reading it).

## `value` constants

The constant pool holds literals and, for functions and classes, the objects
they need:

```
bool  value     = {"type": 0, "boolean": true}         VAL_BOOL
nil   value     = {"type": 1}                          VAL_NIL
num   value     = {"type": 2, "number": 1.5}           VAL_NUMBER
string value    = {"type": 3, "obj": {"type": 3, "length": 5, "chars": "hello"}}
function value  = {"type": 3, "obj": <prototype as nested object>}
```

ValueType is 0 VAL_BOOL, 1 VAL_NIL, 2 VAL_NUMBER, 3 VAL_OBJ. ObjType is 3
OBJ_STRING and 7 OBJ_PROTOTYPE. Classes come from `OP_CLASS` (name constant)
plus `OP_METHOD` calls, so they need no representation of their own - only the
name string and the method prototypes appear.

Non-finite `double`s have no JSON token, so the writer emits them as strings:
`"NaN"`, `"Infinity"`, `"-Infinity"`. The loader maps them back. Compact
decimal output (Python/Ruby shortest-repr, C `%.17g`) round-trips every double
exactly.

## Types

Parameters and return type use the same encoding as `struct Type`: a name
string (or `null`), a `next` chain for union variants, and a `generics` slot
chain for `Function` types (each slot has a `null` name and holds one type).
The loader rebuilds the exact chains; if a `.slb` file doesn't need type
checking at load time it still carries them, mirroring the compiled object.

## Writing from another language

Build the nested objects above with your language's JSON writer and dump to a
file:

```python
import json

prototype = {
    "name": "<script>",
    "upvalueCount": 0,
    "parameters": [],
    "returnType": None,
    "code": [0, 0, 30],                      # OP_CONSTANT 0 ; OP_RETURN
    "lines": [[1, 3]],
    "constants": [{"type": 2, "number": 42}],
}
with open("answer.slb", "w") as f:
    json.dump({"version": 1, **prototype}, f)
```

Then `solace answer.slb` prints `42`.

## Notes

- `.slb` files are **trusted input**, like Python's `.pyc`. The loader does a
  light structural validation (operand bounds, constant indices, jump targets,
  closure upvalue lists) so a truncated file (hopefully) can't crash the VM, but it does
  not prove the program is well-typed. Don't run bytecode from an untrusted
  source.
- Loading skips the lexer, parser, and type checker entirely; the prototype
  tree is executed as-is. Native functions are still resolved by index at
  runtime (`OP_GET_NATIVE`), so the built-in native set is stable for a given
  `"version"`.
