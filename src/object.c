#include "object.h"

MAKE_DYNAMIC_ARRAY(Object, ObjectArray)

void printObject(Object object) {
    printf("%g", object);
}
