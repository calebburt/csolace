# Plan: `import` in Solace (static compilation, dot-namespaced)

## Semantics it will have

```solace
import shapes          # shapes.slc, relative to the *importing* file's dir
c = shapes.Circle(2)   # dot access into the module
c.area()               # method on an imported class
import lib.util        # -> lib/util.slc

def helper() > Number  # imports are allowed anywhere
  import shapes
  shapes.Circle(1).area()
end
```

- A module is a file, compiled **once per program** (canonical-path cache) into its **own `ObjPrototype`**, and its code runs **once** (singleton per file).
- The module value is a namespace object; its exports are its top-level `def`s, `class`es and variables, reachable only as `name.export`.
- Names are **not** merged into the importer's scope. Two modules may both declare `class Cell`.
- `import` inside a function body is allowed; it re-emits nothing per call (a 2-byte `OP_IMPORT`), so the module still initialises once.

## Compile side (compiler.c)

1. **`importExpr` parse fn** (new rule `[TOKEN_IMPORT] = {importExpr, NULL, PREC_NONE}` near `rules[]`, compiler.c:1446).
   - Parse the dotted path (`identifier ('.' identifier)*`).
   - Resolve + load via the new loader; on a cache hit emit nothing but the value push; on a cycle, error `"Circular import of 'a.b'."`
   - Compile the file (once) into a module compiler; register it in `vm->imports`; emit `OP_IMPORT <idx>`; return the module's static type.
2. **`compileModule(VM*, const char *path, const char *text)`** — mirrors `compile()` (compiler.c:1564) but:
   - fresh `Lexer`/`Parser`/`Compiler` with new `FunctionType TYPE_MODULE` (slot 0 hidden like `TYPE_FUNCTION`, compiler.c:279-285, so it isn't exported);
   - swap `vm->parser` to the module's parser (save/restore) so `markCompilerRoots` reaches the in-flight chain, and restore `parser->scanning`/`panicMode` the way `class()` does at compiler.c:1355-1365;
   - runs the statement loop, then the **export epilogue**: `OP_GET_LOCAL` for every live local with a non-empty name, in slot order, then `OP_MODULE <exportCount>`, then `OP_RETURN`;
   - builds the export table `{name, Type*, index}` and a `TypeTable` of the module's published class types.
3. **Per-file import scope.** `Parser` gains `ImportEntry *imports; int importCount; char *modulePrefix; const char *currentFile;` — visible to every `Compiler` in that file, so a module's own imports are visible in its methods but not to its importer.
4. **`namedVariable` (compiler.c:632)**: after locals and upvalues, before `resolveNative`, match a bare name against `parser->imports` → emit `OP_IMPORT <idx>`, return the module type. This is what makes `shapes` usable as a value and is the *only* new name-resolution path.
5. **`dot` (compiler.c:1102) needs no changes.** Publish the module type into the file's table with its exports as `fields` (`setTypeTable`), and `addAllTypeTable(vm, &moduleCompiler.types, &rootCompilerOf(parser->currentCompiler)->types)` so the importer and *all* of its functions/classes see the module's class types.
6. **Namespacing = qualified type names.** `Type` identity is structural by name string (`typesEqual`/`hashType`, type.c:29-49), so a class declared in `shapes.slc` gets its instance type named **`"shapes.Cell"`**, built in `class()` (compiler.c:1334) by prefixing `parser->modulePrefix`. No change to `isSubtype`/`hashType`/tables. `parseType` (compiler.c:622) accepts a dotted path: absolute-from-root first (`lib.util.Thing`), else relative to the current file (`shapes` → `shapes.Cell`); a bare name inside a module file qualifies to the file's own prefix **only if that file declares or imports it**, so `Number`/`Any` stay global. Consequence to document: `x: Cell` in the importer is a *different, inert* type — you must write `x: shapes.Cell`.
7. **Assignment into a module is rejected at compile time** (`"Cannot assign to 'shapes.Circle'."`), because `OP_SET_FIELD` would write the module's copy and the module frame is already gone.

## Runtime (vm.c, chunk.h, value.c)

- `OP_MODULE <exportCount>` (35): pop `exportCount` values into a **synthesised namespace** — an `ObjInstance` of a generated `ObjClass` named `"module shapes"` with `fieldCount = exportCount`. Reusing instances means `OP_GET_FIELD`/`OP_SET_FIELD`/`OP_GET_METHOD` and `dot` work untouched, and no new `ObjType`/GC/`printValue` case.
- `OP_IMPORT <idx>` (36): if `vm->imports[idx].instance` is nil, `newFunction(vm, vm->imports[idx].prototype)` + `call(vm, fn, 0)`, store `OBJ_VAL(fn)`, then push — mirroring the `OP_CALL` path, including reloading `frame`. All later sites get the same object.
- `VM` gains `ImportEntry *imports; int importCount;` and `SourceFile *sources; int sourceCount;` (module texts + paths). Marked in `markRoots` (memory.c:131) — **must** be, since module prototypes now hang off the VM rather than a chunk's constants.
- `runtimeError` (vm.c:90) currently prints the source line from the single `vm->source`. Make it look up the frame's prototype: set the module prototype's `name` to its path and resolve the text through `vm->sources`, falling back to `vm->source`. Per-file prototypes mean line numbers stay file-local, so no line rebasing is needed anywhere.

## Loader (new `src/module.c` + `module.h`)

`readFile` moves out of `main.c:24`. The loader owns every buffer it reads (tokens point into them; `copyString` copies, so no dangling), keyed by `realpath()` for dedupe/cycle detection; `main.c` keeps ownership of the root file's text and hands the loader a `{path, text}` pair for diagnostics. Root script dir seeds resolution; each imported file's own dir seeds *its* imports. Missing file → compile error at the `import` line, exit 65.

## Bytecode (bytecode.c, doc/bytecode.md)

- Document gains `"imports": [prototype|null, ...]`, index-aligned with the `OP_IMPORT` operand, so a `.slb` is **self-contained** — no module files needed to run it. The loader rebuilds `vm->imports` with `instance = nil`, so it instantiates lazily exactly like the compiled path.
- `"version": 1` → **2** (the loader rejects other versions).
- No change needed to the `lines`/constant/ObjType encodings. `doc/opcodes.md` + `doc/bytecode.md` updated; new `doc/imports.md` for the semantics.

## Tests

`tests/cases/` + a new `tests/cases/lib/`:
`import_names` (class/def/field/method across files), `import_nested` (dotted path + transitive sub-module), `import_once` (imported twice; a module-local counter proves one instantiation), `import_in_function`, `import_diamond` (a and b both import c).
`tests/errors/`: `import_missing`, `import_cycle`, `import_assign_to_module`, `import_unqualified_type`.
`tests/run.sh` needs **no change** and gives a free assertion: the `--dump` pass runs from `$work` where `lib/` doesn't exist, so it proves the bytecode is self-contained. Add a comment saying so.

## Ordered steps

1. `module.c/h` (read, resolve, dedupe, cycle detect) + `parser->currentFile` in diagnostics. Testable with a stub `import` that only errors.
2. `TYPE_MODULE`, `compileModule`, epilogue, `OP_MODULE`, dispatch, GC marks. Test with a hand-built namespace.
3. Qualified type names in `class()` + `parseType` dotted paths + `addAllTypeTable` merge.
4. `OP_IMPORT`, `vm->imports`, `namedVariable` module resolution, the parse rule, `import` anywhere.
5. Diagnostics pass (`runtimeError` source lookup), then `make test`, REPL spot-check (`import` in a REPL line), `make prof` sanity.
6. Docs.

## Risks worth deciding before I start

- **`import` in a `.slb` written by hand** must list `"imports"`; loader should accept a missing key as `"imports": []` (and then `OP_IMPORT` is a hard load error rather than a crash).
- **Qualified type names are user-visible** in every type error (`expected Number, got shapes.Cell`). I think that's a feature, but it changes existing error text if any test asserts on a class name.
- **255 constants / 256 locals stay per-prototype**, so modules are actually *better* isolated than a textual include, but a program with many imports in one function still shares that function's slots.
