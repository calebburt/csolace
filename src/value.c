#include "common.h"
#include "value.h"

MAKE_DYNAMIC_ARRAY(Value, ValueArray)

void printValue(Value value) {
    printf("%g", AS_NUMBER(value));
}
