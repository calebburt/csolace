#ifndef SLC_OBJECT_H
#define SLC_OBJECT_H

#include "dynamic_array.h"

typedef double Object;

MAKE_DYNAMIC_ARRAY_H(Object, ObjectArray)

void printObject(Object object);

#endif