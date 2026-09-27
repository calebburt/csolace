# Opcodes in CSolace

These are the opcodes used in `csolace`. I probably will not maintain this list XD.

| opcode           | value | operand encoding                          |
|------------------|-------|-------------------------------------------|
| OP_CONSTANT      | 0     | 1 byte: constant index                    |
| OP_NIL           | 1     | -                                         |
| OP_TRUE          | 2     | -                                         |
| OP_FALSE         | 3     | -                                         |
| OP_POP           | 4     | -                                         |
| OP_GET_LOCAL     | 5     | 1 byte: local slot                        |
| OP_SET_LOCAL     | 6     | 1 byte: local slot                        |
| OP_GET_NATIVE    | 7     | 1 byte: native index                      |
| OP_GET_UPVALUE   | 8     | 1 byte: upvalue index                     |
| OP_SET_UPVALUE   | 9     | 1 byte: upvalue index                     |
| OP_CLOSE_UPVALUE | 10    | -                                         |
| OP_GET_FIELD     | 11    | 1 byte: field id (per-class)              |
| OP_GET_METHOD    | 12    | 1 byte: method id (per-class)             |
| OP_SET_FIELD     | 13    | 1 byte: field id                          |
| OP_EQUAL         | 14    | -                                         |
| OP_GREATER       | 15    | -                                         |
| OP_LESS          | 16    | -                                         |
| OP_GREATER_EQUAL | 17    | -                                         |
| OP_LESS_EQUAL    | 18    | -                                         |
| OP_ADD           | 19    | -                                         |
| OP_SUBTRACT      | 20    | -                                         |
| OP_MULTIPLY      | 21    | -                                         |
| OP_DIVIDE        | 22    | -                                         |
| OP_NOT           | 23    | -                                         |
| OP_NEGATE        | 24    | -                                         |
| OP_JUMP          | 25    | 2 bytes, relative jump-offset             |
| OP_JUMP_IF_FALSE | 26    | 2 bytes, relative offset                  |
| OP_LOOP          | 27    | 2 bytes, backward offset                  |
| OP_CALL          | 28    | 1 byte: argument count                    |
| OP_CLOSURE       | 29    | 1 byte: constant index **plus** two descriptor bytes per upvalue (`isLocal`, `index`)  |
| OP_RETURN        | 30    | -                                         |
| OP_CLASS         | 31    | 2 bytes: name constant index, field count |
| OP_HALT          | 32    | -                                         |
| OP_METHOD        | 33    | -                                         |
| OP_INITIALIZER   | 34    | 1 byte: initializer method id             |
