#include "bytecode.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"
#include "value.h"
#include "vm.h"

// ---------------------------------------------------------------------------
// The on-disk format is a JSON mirror of the in-memory representation:
//
//   document = prototype (the root, what compile() yields)
//   prototype = { "name": string|null,
//                 "upvalueCount": int,
//                 "parameters": [type...],
//                 "returnType": type|null,
//                 "code": [int...],            raw bytecode bytes
//                 "lines": [[line,count]...],  RLE, mirrors LineInfoArray
//                 "constants": [value...] }
//   type = { "name": string|null, "next": type|null,
//            "generics": type|null }      (mirrors Type)
//   value = { "type": <ValueType>, ... }  (mirrors Value)
//   obj = { "type": <ObjType>, ... }    (mirrors Obj)
//
// The "code" list is byte-for-byte the Chunk's code array, so the on-disk form
// and the VM's internal form are the same thing. ValueType (VAL_BOOL=0,
// VAL_NIL=1, VAL_NUMBER=2, VAL_OBJ=3) and ObjType (OBJ_STRING=3,
// OBJ_PROTOTYPE=7) are the enum values from value.h. Non-finite numbers are
// written as the strings "NaN", "Infinity", "-Infinity" because JSON has no
// token for them; the loader maps them back.
// ---------------------------------------------------------------------------

// Writing

typedef struct {
    FILE *out;
    bool ok;      // whether it has failed yet
} JsonWriter;

static void writePrototypeBody(JsonWriter *w, ObjPrototype *prototype, int depth);

static void writeString(FILE *out, const char *chars, int length) {
    fputc('"', out);
    for (int i = 0; i < length; i++) {
        unsigned char c = (unsigned char)chars[i];
        switch (c) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            case '\b': fputs("\\b", out); break;
            case '\f': fputs("\\f", out); break;
            default:
                // bytes below 0x20 have no escape, use unicode escape.
                if (c < 0x20) fprintf(out, "\\u%04x", c);
                else fputc(c, out);
        }
    }
    fputc('"', out);
}

static void writeNumber(FILE *out, double value) {
    // %.17g round-trips any finite double exactly. JSON can't carry the
    // non-finite values, so they become strings (see header comment).
    if (isnan(value)) {
        fputs("\"NaN\"", out);
    } else if (value == INFINITY) {
        fputs("\"Infinity\"", out);
    } else if (value == -INFINITY) {
        fputs("\"-Infinity\"", out);
    } else {
        fprintf(out, "%.17g", value);
    }
}

static void writeIndent(FILE *out, int depth) {
    for (int i = 0; i < depth; i++) fputs("  ", out);
}

// Recursive: `next` is the union-variant chain, `generics` the slot chain of
// function types. Each field is itself a Type exactly as in struct Type.
static void writeType(JsonWriter *w, Type *type, int depth) {
    FILE *out = w->out;
    if (type == NULL) {
        fputs("null", out);
        return;
    }
    fputs("{\n", out);
    writeIndent(out, depth + 1);
    fputs("\"name\": ", out);
    if (type->name != NULL) writeString(out, type->name->chars, type->name->length);
    else fputs("null", out);
    fputs(",\n", out);
    writeIndent(out, depth + 1);
    fputs("\"next\": ", out);
    writeType(w, type->next, depth + 1);
    fputs(",\n", out);
    writeIndent(out, depth + 1);
    fputs("\"generics\": ", out);
    writeType(w, type->generics, depth + 1);
    fputs("\n", out);
    writeIndent(out, depth);
    fputs("}", out);
}

static void writeValue(JsonWriter *w, Value value, int depth);

// Mirrors Obj/Value: a tag plus the type's own fields. Only strings and nested 
// prototypes go in the constant pool.
static void writeObj(JsonWriter *w, Obj *object, int depth) {
    FILE *out = w->out;
    switch (object->type) {
        case OBJ_STRING: {
            ObjString *string = (ObjString*)object;
            fprintf(out, "{\"type\": %d, \"length\": %d, \"chars\": ",
                    OBJ_STRING, string->length);
            writeString(out, string->chars, string->length);
            fputs("}", out);
            break;
        }
        case OBJ_PROTOTYPE:
            fprintf(out, "{\"type\": %d,\n", OBJ_PROTOTYPE);
            writePrototypeBody(w, (ObjPrototype*)object, depth);
            fputs("}", out);
            break;
        default:
            fprintf(stderr, "Bytecode save: cannot serialize object type %d\n",
                    object->type);
            w->ok = false;
            fputs("null", out);
            break;
    }
}

static void writeValue(JsonWriter *w, Value value, int depth) {
    FILE *out = w->out;
    switch (value.type) {
        case VAL_BOOL:
            fprintf(out, "{\"type\": %d, \"boolean\": %s}", VAL_BOOL,
                    AS_BOOL(value) ? "true" : "false");
            break;
        case VAL_NIL:
            fprintf(out, "{\"type\": %d}", VAL_NIL);
            break;
        case VAL_NUMBER:
            fprintf(out, "{\"type\": %d, \"number\": ", VAL_NUMBER);
            writeNumber(out, AS_NUMBER(value));
            fputs("}", out);
            break;
        case VAL_OBJ:
            fprintf(out, "{\"type\": %d, \"obj\": ", VAL_OBJ);
            writeObj(w, AS_OBJ(value), depth + 1);
            fputs("}", out);
            break;
    }
}

// The fields of a prototype object (assumes the caller already opened '{')
void writePrototypeBody(JsonWriter *w, ObjPrototype *prototype, int depth) {
    FILE *out = w->out;
    Chunk *chunk = &prototype->chunk;

    writeIndent(out, depth + 1);
    fputs("\"name\": ", out);
    if (prototype->name != NULL) writeString(out, prototype->name->chars, prototype->name->length);
    else fputs("null", out);
    fputs(",\n", out);

    writeIndent(out, depth + 1);
    fputs("\"upvalueCount\": ", out);
    fprintf(out, "%d", prototype->upvalueCount);
    fputs(",\n", out);

    writeIndent(out, depth + 1);
    fputs("\"parameters\": [", out);
    for (int i = 0; i < prototype->parameters.count; i++) {
        if (i > 0) fputs(", ", out);
        writeType(w, prototype->parameters.data[i], depth + 1);
    }
    fputs("],\n", out);

    writeIndent(out, depth + 1);
    fputs("\"returnType\": ", out);
    writeType(w, prototype->returnType, depth + 1);
    fputs(",\n", out);

    // The bytecode itself: a flat list of integers, 16 per line for legibility.
    writeIndent(out, depth + 1);
    fputs("\"code\": [", out);
    for (int i = 0; i < chunk->code.count; i++) {
        if (i % 16 == 0) {
            fputs("\n", out);
            writeIndent(out, depth + 2);
        }
        fprintf(out, "%d", chunk->code.data[i]);
        if (i + 1 < chunk->code.count) fputs(", ", out);
    }
    fputs("\n", out);
    writeIndent(out, depth + 1);
    fputs("],\n", out);

    writeIndent(out, depth + 1);
    fputs("\"lines\": [", out);
    for (int i = 0; i < chunk->lines.count; i++) {
        if (i > 0) fputs(", ", out);
        fprintf(out, "[%d, %d]", chunk->lines.data[i].line, chunk->lines.data[i].num);
    }
    fputs("],\n", out);

    writeIndent(out, depth + 1);
    fputs("\"constants\": [", out);
    for (int i = 0; i < chunk->constants.count; i++) {
        fputs("\n", out);
        writeIndent(out, depth + 2);
        writeValue(w, chunk->constants.data[i], depth + 2);
        if (i + 1 < chunk->constants.count) fputs(",", out);
    }
    fputs("\n", out);
    writeIndent(out, depth + 1);
    fputs("]", out);
    fputs("\n", out);
    writeIndent(out, depth);
}

static void writeDocument(JsonWriter *w, ObjPrototype *prototype) {
    FILE *out = w->out;
    fputs("{\n", out);
    writeIndent(out, 1);
    fputs("\"version\": 1,\n", out);
    writePrototypeBody(w, prototype, 1);
    fputs("}\n", out);
}

bool saveBytecode(VM *vm, ObjPrototype *prototype, FILE *out) {
    JsonWriter w = { out, true };
    writeDocument(&w, prototype);
    return w.ok;
}

// Reading

typedef struct {
    const char *p;      // cursor
    bool ok;
    const char *error;
    int depth;          // recursion guard for hostile nesting
} JsonReader;

static bool parseRawString(JsonReader *r, char **outChars, int *outLength);

static void fail(JsonReader *r, const char *message) {
    if (r->ok) {
        r->ok = false;
        r->error = message;
    }
}

static void skipWs(JsonReader *r) {
    while (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r') r->p++;
}

static bool parseBool(JsonReader *r, bool *out) {
    skipWs(r);
    if (strncmp(r->p, "true", 4) == 0) {
        *out = true;
        r->p += 4;
        return true;
    }
    if (strncmp(r->p, "false", 5) == 0) {
        *out = false;
        r->p += 5;
        return true;
    }
    fail(r, "expected boolean");
    return false;
}

// Consume an integer token (no fraction/exponent) into *out.
static bool parseInt(JsonReader *r, int *out) {
    skipWs(r);
    const char *start = r->p;
    if (*r->p == '-') r->p++;
    if (*r->p < '0' || *r->p > '9') {
        fail(r, "expected number");
        return false;
    }
    while (*r->p >= '0' && *r->p <= '9') r->p++;
    if (*r->p == '.' || *r->p == 'e' || *r->p == 'E') {
        fail(r, "expected integer");
        return false;
    }
    int len = (int)(r->p - start);
    char buf[32];
    if (len >= (int)sizeof(buf)) {
        fail(r, "integer too large");
        return false;
    }
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
    *out = atoi(buf);
    return true;
}

static bool parseNumber(JsonReader *r, double *out) {
    skipWs(r);
    const char *start = r->p;
    if (*r->p == '-') r->p++;
    if (*r->p == '0') {
        r->p++;
    } else if (*r->p >= '1' && *r->p <= '9') {
        while (*r->p >= '0' && *r->p <= '9') r->p++;
    } else {
        fail(r, "expected number");
        return false;
    }
    if (*r->p == '.') {
        r->p++;
        if (*r->p < '0' || *r->p > '9') {
            fail(r, "expected digit after '.'");
            return false;
        }
        while (*r->p >= '0' && *r->p <= '9') r->p++;
    }
    if (*r->p == 'e' || *r->p == 'E') {
        r->p++;
        if (*r->p == '+' || *r->p == '-') r->p++;
        if (*r->p < '0' || *r->p > '9') {
            fail(r, "expected exponent digit");
            return false;
        }
        while (*r->p >= '0' && *r->p <= '9') r->p++;
    }
    int len = (int)(r->p - start);
    char buf[64];
    if (len >= (int)sizeof(buf)) {
        fail(r, "number too long");
        return false;
    }
    memcpy(buf, start, (size_t)len);
    buf[len] = '\0';
    *out = strtod(buf, NULL);
    return true;
}

// The number payload of a VAL_NUMBER value: a JSON number, or one of the
// string escapes the writer emits for non-finite doubles.
static bool parseNumberValue(JsonReader *r, double *out) {
    skipWs(r);
    if (*r->p == '"') {
        char *string;
        int length;
        if (!parseRawString(r, &string, &length)) return false;
        bool matched = false;
        if (length == 3 && memcmp(string, "NaN", 3) == 0) {
            *out = NAN;
            matched = true;
        } else if (length == 8 && memcmp(string, "Infinity", 8) == 0) {
            *out = INFINITY;
            matched = true;
        } else if (length == 9 && memcmp(string, "-Infinity", 9) == 0) {
            *out = -INFINITY;
            matched = true;
        }
        free(string);
        if (!matched) fail(r, "bad number token");
        return matched;
    }
    return parseNumber(r, out);
}

static void bufAppend(char **buffer, int *length, int *capacity, char c) {
    if (*length + 1 >= *capacity) {
        *capacity *= 2;
        *buffer = (char*)realloc(*buffer, (size_t)*capacity);
    }
    (*buffer)[(*length)++] = c;
}

// Parse a JSON string into a malloc'd buffer with escapes decoded.
static bool parseRawString(JsonReader *r, char **outChars, int *outLength) {
    skipWs(r);
    if (!*r->p || *r->p != '"') {
        fail(r, "expected string");
        return false;
    }
    r->p++;
    int capacity = 32, length = 0;
    char *buffer = (char*)malloc((size_t)capacity);
    while (true) {
        char c = *r->p;
        if (c == '\0') {
            free(buffer);
            fail(r, "unterminated string");
            return false;
        }
        if (c == '"') {
            r->p++;
            break;
        }
        if (c == '\\') {
            r->p++;
            char escape = *r->p++;
            switch (escape) {
                case '"':  bufAppend(&buffer, &length, &capacity, '"'); break;
                case '\\': bufAppend(&buffer, &length, &capacity, '\\'); break;
                case '/':  bufAppend(&buffer, &length, &capacity, '/'); break;
                case 'b':  bufAppend(&buffer, &length, &capacity, '\b'); break;
                case 'f':  bufAppend(&buffer, &length, &capacity, '\f'); break;
                case 'n':  bufAppend(&buffer, &length, &capacity, '\n'); break;
                case 'r':  bufAppend(&buffer, &length, &capacity, '\r'); break;
                case 't':  bufAppend(&buffer, &length, &capacity, '\t'); break;
                case 'u': {
                    uint32_t codePoint = 0;
                    for (int i = 0; i < 4; i++) {
                        char hex = *r->p++;
                        codePoint <<= 4;
                        if (hex >= '0' && hex <= '9') codePoint |= (uint32_t)(hex - '0');
                        else if (hex >= 'a' && hex <= 'f') codePoint |= (uint32_t)(hex - 'a' + 10);
                        else if (hex >= 'A' && hex <= 'F') codePoint |= (uint32_t)(hex - 'A' + 10);
                        else {
                            free(buffer);
                            fail(r, "bad \\u escape");
                            return false;
                        }
                    }
                    if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
                        free(buffer);
                        fail(r, "surrogate \\u escape not supported");
                        return false;
                    }
                    // Encode as UTF-8; the writer only produces \u00XX, which
                    // decodes back to the same single byte.
                    if (codePoint < 0x80) {
                        bufAppend(&buffer, &length, &capacity, (char)codePoint);
                    } else if (codePoint < 0x800) {
                        bufAppend(&buffer, &length, &capacity, (char)(0xC0 | (codePoint >> 6)));
                        bufAppend(&buffer, &length, &capacity, (char)(0x80 | (codePoint & 0x3F)));
                    } else if (codePoint < 0x10000) {
                        bufAppend(&buffer, &length, &capacity, (char)(0xE0 | (codePoint >> 12)));
                        bufAppend(&buffer, &length, &capacity, (char)(0x80 | ((codePoint >> 6) & 0x3F)));
                        bufAppend(&buffer, &length, &capacity, (char)(0x80 | (codePoint & 0x3F)));
                    } else {
                        bufAppend(&buffer, &length, &capacity, (char)(0xF0 | (codePoint >> 18)));
                        bufAppend(&buffer, &length, &capacity, (char)(0x80 | ((codePoint >> 12) & 0x3F)));
                        bufAppend(&buffer, &length, &capacity, (char)(0x80 | ((codePoint >> 6) & 0x3F)));
                        bufAppend(&buffer, &length, &capacity, (char)(0x80 | (codePoint & 0x3F)));
                    }
                    break;
                }
                default:
                    free(buffer);
                    fail(r, "bad string escape");
                    return false;
            }
            continue;
        }
        bufAppend(&buffer, &length, &capacity, c);
        r->p++;
    }
    // Terminate without counting the NUL in *outLength.
    buffer[length] = '\0';
    *outChars = buffer;
    *outLength = length;
    return true;
}

// Consume "," or "}" after an object/array element; sets *done when the object
// is finished. Tolerates a trailing comma even though the writer doesn't emit
// one.
static bool memberBoundary(JsonReader *r, bool *done) {
    skipWs(r);
    if (*r->p == ',') {
        r->p++;
        *done = false;
        return true;
    }
    if (*r->p == '}') {
        r->p++;
        *done = true;
        return true;
    }
    fail(r, "expected ',' or '}'");
    return false;
}

static Type *parseType(JsonReader *r, VM *vm);
static Value parseValue(JsonReader *r, VM *vm);
static bool expectSpecific(JsonReader *r, char c);
static bool isPrototypeKey(const char *key);

// Dispatch one prototype field. The cursor sits right after "key":.
static void parsePrototypeField(JsonReader *r, VM *vm,
                                ObjPrototype *proto, const char *key) {
    if (strcmp(key, "name") == 0) {
        skipWs(r);
        if (r->p[0] == 'n' && strncmp(r->p, "null", 4) == 0) {
            r->p += 4;
            proto->name = NULL;
        } else {
            char *string;
            int length;
            if (parseRawString(r, &string, &length)) {
                proto->name = copyString(vm, string, length);
                free(string);
            }
        }
    } else if (strcmp(key, "upvalueCount") == 0) {
        parseInt(r, &proto->upvalueCount);
    } else if (strcmp(key, "parameters") == 0) {
        if (!expectSpecific(r, '[')) return;
        skipWs(r);
        if (*r->p == ']') {
            r->p++;
            return;
        }
        while (true) {
            Type *param = parseType(r, vm);
            if (param == NULL || !r->ok) break;
            appendTypeArray(vm, &proto->parameters, param);
            skipWs(r);
            if (*r->p == ',') r->p++;
            else if (*r->p == ']') {
                r->p++;
                break;
            } else {
                fail(r, "expected ',' or ']' in parameters");
                break;
            }
        }
    } else if (strcmp(key, "returnType") == 0) {
        proto->returnType = parseType(r, vm);
    } else if (strcmp(key, "code") == 0) {
        skipWs(r);
        if (*r->p != '[') {
            fail(r, "expected code array");
            return;
        }
        r->p++;
        skipWs(r);
        int byte = 0;
        while (r->ok && *r->p != ']') {
            if (!parseInt(r, &byte)) break;
            if (byte < 0 || byte > 255) {
                fail(r, "code byte out of range");
                break;
            }
            appendCode(vm, &proto->chunk.code, (uint8_t)byte);
            skipWs(r);
            if (*r->p == ',') r->p++;
            else if (*r->p != ']') fail(r, "expected ',' or ']' in code");
        }
        if (r->ok && *r->p == ']') r->p++;
    } else if (strcmp(key, "lines") == 0) {
        skipWs(r);
        if (*r->p != '[') {
            fail(r, "expected lines array");
            return;
        }
        r->p++;
        skipWs(r);
        while (r->ok && *r->p != ']') {
            skipWs(r);
            if (*r->p != '[') {
                fail(r, "expected line pair");
                break;
            }
            r->p++;
            int line = 0, count = 0;
            if (!parseInt(r, &line)) break;
            if (!expectSpecific(r, ',')) break;
            if (!parseInt(r, &count)) break;
            if (!expectSpecific(r, ']')) break;
            appendLineInfoArray(vm, &proto->chunk.lines, (LineInfo){line, count});
            skipWs(r);
            if (*r->p == ',') r->p++;
        }
        if (r->ok && *r->p == ']') r->p++;
    } else if (strcmp(key, "constants") == 0) {
        skipWs(r);
        if (*r->p != '[') {
            fail(r, "expected constants array");
            return;
        }
        r->p++;
        skipWs(r);
        while (r->ok && *r->p != ']') {
            Value value = parseValue(r, vm);
            if (!r->ok) break;
            appendValueArray(vm, &proto->chunk.constants, value);
            skipWs(r);
            if (*r->p == ',') r->p++;
        }
        if (r->ok && *r->p == ']') r->p++;
    } else {
        fail(r, "unknown prototype field");
    }
}

// Non-owned cursor helpers for the array dispatch above.
static bool isPrototypeKey(const char *key) {
    return strcmp(key, "name") == 0 ||
           strcmp(key, "upvalueCount") == 0 ||
           strcmp(key, "parameters") == 0 ||
           strcmp(key, "returnType") == 0 ||
           strcmp(key, "code") == 0 ||
           strcmp(key, "lines") == 0 ||
           strcmp(key, "constants") == 0;
}

static bool expectSpecific(JsonReader *r, char c) {
    skipWs(r);
    if (*r->p != c) {
        fail(r, "expected character");
        return false;
    }
    r->p++;
    return true;
}

static Type *parseType(JsonReader *r, VM *vm) {
    skipWs(r);
    if (strncmp(r->p, "null", 4) == 0) {
        r->p += 4;
        return NULL;
    }
    if (r->depth++ > 500) {
        fail(r, "type nesting too deep");
        return NULL;
    }
    if (!expectSpecific(r, '{')) return NULL;
    Type *node = ALLOCATE(vm, Type, 1);
    node->name = NULL;
    node->next = NULL;
    node->generics = NULL;
    while (true) {
        skipWs(r);
        if (*r->p == '}') {
            r->p++;
            break;
        }
        char *key;
        int keyLen;
        if (!parseRawString(r, &key, &keyLen)) break;
        if (!expectSpecific(r, ':')) {
            free(key);
            break;
        }
        bool done = false;
        if (strcmp(key, "name") == 0) {
            skipWs(r);
            if (strncmp(r->p, "null", 4) == 0) {
                r->p += 4;
                node->name = NULL;
            } else {
                char *string;
                int length;
                if (parseRawString(r, &string, &length)) {
                    node->name = copyString(vm, string, length);
                    free(string);
                }
            }
        } else if (strcmp(key, "next") == 0) {
            node->next = parseType(r, vm);
        } else if (strcmp(key, "generics") == 0) {
            node->generics = parseType(r, vm);
        } else {
            fail(r, "unknown type field");
        }
        free(key);
        if (!r->ok) break;
        if (!memberBoundary(r, &done)) break;
        if (done) break;
    }
    r->depth--;
    return node;
}

static Obj *parseObj(JsonReader *r, VM *vm) {
    if (r->depth++ > 500) {
        fail(r, "object nesting too deep");
        return NULL;
    }
    if (!expectSpecific(r, '{')) return NULL;
    int typeTag = -1;
    ObjPrototype *proto = NULL;
    char *string = NULL;
    int stringLen = 0;
    bool sawChars = false;
    while (true) {
        skipWs(r);
        if (*r->p == '}') {
            r->p++;
            break;
        }
        char *key;
        int keyLen;
        if (!parseRawString(r, &key, &keyLen)) break;
        if (!expectSpecific(r, ':')) {
            free(key);
            break;
        }
        if (strcmp(key, "type") == 0) {
            parseInt(r, &typeTag);
        } else if (strcmp(key, "chars") == 0) {
            if (parseRawString(r, &string, &stringLen)) sawChars = true;
        } else if (strcmp(key, "length") == 0) {
            int ignored = 0;
            parseInt(r, &ignored); // length is derived from "chars" on load
        } else if (isPrototypeKey(key)) {
            if (proto == NULL) proto = newPrototype(vm);
            parsePrototypeField(r, vm, proto, key);
        } else {
            fail(r, "unknown object field");
        }
        free(key);
        if (!r->ok) break;
        bool done = false;
        if (!memberBoundary(r, &done)) break;
        if (done) break;
    }
    r->depth--;
    if (!r->ok) {
        free(string);
        return NULL;
    }
    if (typeTag == OBJ_STRING) {
        if (!sawChars) {
            fail(r, "string object missing 'chars'");
            return NULL;
        }
        Obj *object = (Obj*)copyString(vm, string, stringLen);
        free(string);
        return object;
    }
    if (typeTag == OBJ_PROTOTYPE) {
        if (proto == NULL) {
            fail(r, "prototype object has no fields");
            return NULL;
        }
        return (Obj*)proto;
    }
    free(string);
    fail(r, "unsupported object type");
    return NULL;
}

static Value parseValue(JsonReader *r, VM *vm) {
    Value value = NIL_VAL;
    if (!expectSpecific(r, '{')) return value;
    int typeTag = -1;
    bool boolean = false;
    double number = 0;
    Obj *object = NULL;
    bool sawBoolean = false, sawNumber = false, sawObj = false;
    while (true) {
        skipWs(r);
        if (*r->p == '}') {
            r->p++;
            break;
        }
        char *key;
        int keyLen;
        if (!parseRawString(r, &key, &keyLen)) break;
        if (!expectSpecific(r, ':')) {
            free(key);
            break;
        }
        if (strcmp(key, "type") == 0) {
            parseInt(r, &typeTag);
        } else if (strcmp(key, "boolean") == 0) {
            sawBoolean = parseBool(r, &boolean);
        } else if (strcmp(key, "number") == 0) {
            sawNumber = parseNumberValue(r, &number);
        } else if (strcmp(key, "obj") == 0) {
            object = parseObj(r, vm);
            sawObj = r->ok;
        } else {
            fail(r, "unknown value field");
        }
        free(key);
        if (!r->ok) break;
        bool done = false;
        if (!memberBoundary(r, &done)) break;
        if (done) break;
    }
    if (!r->ok) return NIL_VAL;
    switch (typeTag) {
        case VAL_BOOL:
            if (sawBoolean) value = BOOL_VAL(boolean);
            else fail(r, "value missing 'boolean'");
            break;
        case VAL_NIL:
            break;
        case VAL_NUMBER:
            if (sawNumber) value = NUMBER_VAL(number);
            else fail(r, "value missing 'number'");
            break;
        case VAL_OBJ:
            if (sawObj) value = OBJ_VAL(object);
            else fail(r, "value missing 'obj'");
            break;
        default:
            fail(r, "bad value type");
            break;
    }
    return value;
}

// Parse the prototype object at the cursor (which must be '{'). When
// *allowVersion, a leading "version" member is consumed into *versionOut.
static ObjPrototype *parsePrototypeObject(JsonReader *r, VM *vm,
                                          bool allowVersion, int *versionOut) {
    if (r->depth++ > 500) {
        fail(r, "prototype nesting too deep");
        return NULL;
    }
    if (!expectSpecific(r, '{')) return NULL;
    ObjPrototype *proto = newPrototype(vm);
    while (true) {
        skipWs(r);
        if (*r->p == '}') {
            r->p++;
            break;
        }
        char *key;
        int keyLen;
        if (!parseRawString(r, &key, &keyLen)) break;
        if (!expectSpecific(r, ':')) {
            free(key);
            break;
        }
        bool done = false;
        if (strcmp(key, "version") == 0) {
            if (allowVersion) {
                int parsed = -1;
                parseInt(r, &parsed);
                if (versionOut != NULL) *versionOut = parsed;
            } else {
                fail(r, "unexpected 'version' field");
            }
        } else if (isPrototypeKey(key)) {
            parsePrototypeField(r, vm, proto, key);
        } else {
            fail(r, "unknown prototype field");
        }
        free(key);
        if (!r->ok) break;
        if (!memberBoundary(r, &done)) break;
        if (done) break;
    }
    r->depth--;
    return proto;
}

// ---------------------------------------------------------------------------
// Validation: a light structural pass so a truncated or hand-edited file can't
// walk the VM off the end of the code array. Files are still trusted input,
// exactly like Python .pyc files.
// ---------------------------------------------------------------------------

// Number of extra operand bytes an opcode carries; -1 means variable
// (OP_CLOSURE: 1 + 2 * upvalueCount).
static int operandBytes(uint8_t op) {
    switch ((Opcode)op) {
        case OP_CONSTANT:
        case OP_GET_LOCAL:
        case OP_SET_LOCAL:
        case OP_GET_NATIVE:
        case OP_GET_UPVALUE:
        case OP_SET_UPVALUE:
        case OP_GET_FIELD:
        case OP_GET_METHOD:
        case OP_SET_FIELD:
        case OP_CALL:
        case OP_INITIALIZER:
            return 1;
        case OP_JUMP:
        case OP_JUMP_IF_FALSE:
        case OP_LOOP:
            return 2;
        case OP_CLASS:
            return 2;
        case OP_CLOSURE:
            return -1;
        default:
            return 0;
    }
}

static bool validatePrototype(VM *vm, ObjPrototype *prototype, const char **error) {
    Chunk *chunk = &prototype->chunk;
    uint8_t *code = chunk->code.data;
    int count = chunk->code.count;
    int ip = 0;
    while (ip < count) {
        uint8_t op = code[ip];
        int extra = operandBytes(op);
        if (extra == -1) {
            if (ip + 2 > count) {
                *error = "OP_CLOSURE truncated";
                return false;
            }
            int constant = code[ip + 1];
            if (constant >= chunk->constants.count ||
                !IS_PROTOTYPE(chunk->constants.data[constant])) {
                *error = "OP_CLOSURE constant is not a function";
                return false;
            }
            int total = 2 + 2 * AS_PROTOTYPE(chunk->constants.data[constant])->upvalueCount;
            if (ip + total > count) {
                *error = "OP_CLOSURE upvalue list truncated";
                return false;
            }
            ip += total;
            continue;
        }
        if (ip + 1 + extra > count) {
            *error = "instruction operand truncated";
            return false;
        }
        if (op == OP_CONSTANT) {
            if (code[ip + 1] >= (uint8_t)chunk->constants.count) {
                *error = "OP_CONSTANT index out of range";
                return false;
            }
        } else if (op == OP_CLASS) {
            if (code[ip + 1] >= (uint8_t)chunk->constants.count ||
                !IS_STRING(chunk->constants.data[code[ip + 1]])) {
                *error = "OP_CLASS name constant invalid";
                return false;
            }
        } else if (op == OP_GET_UPVALUE || op == OP_SET_UPVALUE) {
            if (code[ip + 1] >= (uint8_t)prototype->upvalueCount) {
                *error = "upvalue index out of range";
                return false;
            }
        } else if (op == OP_JUMP || op == OP_JUMP_IF_FALSE || op == OP_LOOP) {
            int offset = (code[ip + 1] << 8) | code[ip + 2];
            if (op == OP_LOOP) {
                if (ip + 3 - offset < 0) {
                    *error = "OP_LOOP target before chunk start";
                    return false;
                }
            } else {
                if (ip + 3 + offset > count) {
                    *error = "jump target past end of chunk";
                    return false;
                }
            }
        }
        ip += 1 + extra;
    }
    // Recurse into nested functions.
    for (int i = 0; i < chunk->constants.count; i++) {
        Value value = chunk->constants.data[i];
        if (IS_PROTOTYPE(value)) {
            if (!validatePrototype(vm, AS_PROTOTYPE(value), error)) return false;
        } else if (IS_OBJ(value) && !IS_STRING(value)) {
            *error = "constant holds an unsupported object";
            return false;
        }
    }
    return true;
}

ObjPrototype *loadBytecode(VM *vm, const char *json, const char **outError) {
    JsonReader r = { json, true, NULL, 0 };
    int version = -1;

    ObjPrototype *prototype = parsePrototypeObject(&r, vm, true, &version);
    if (!r.ok) {
        if (outError != NULL) *outError = r.error;
        return NULL;
    }
    if (version != SLC_BYTECODE_VERSION) {
        if (outError != NULL) *outError = "bytecode version mismatch";
        return NULL;
    }

    const char *validationError = NULL;
    if (!validatePrototype(vm, prototype, &validationError)) {
        if (outError != NULL) *outError = validationError;
        return NULL;
    }

    // Loader depends on a blank cursor trailing input; reject trailing garbage.
    skipWs(&r);
    if (*r.p != '\0') {
        if (outError != NULL) *outError = "trailing data after bytecode";
        return NULL;
    }

    return prototype;
}