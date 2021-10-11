// Author: strawberryhacker (all credit goes to Alex Taradov)

#ifndef ARRAY_H
#define ARRAY_H

#include "utilities.h"
#include "stdlib.h"

//--------------------------------------------------------------------------------------------------

#define define_array(name, array_type, item_type)                                                   \
                                                                                                    \
typedef struct array_type {                                                                         \
    item_type* items;                                                                               \
    int count;                                                                                      \
    int capacity;                                                                                   \
} array_type;                                                                                       \
                                                                                                    \
static inline void name##_resize(array_type* array, int capacity) {                                 \
    u8* source = (u8 *)array->items;                                                                \
    u8* destination = (u8 *)malloc(capacity * sizeof(item_type));                                   \
                                                                                                    \
    for (int i = 0; i < (array->count * sizeof(item_type)); i++) {                                  \
        destination[i] = source[i];                                                                 \
    }                                                                                               \
                                                                                                    \
    if (array->capacity) {                                                                          \
        free(source);                                                                               \
    }                                                                                               \
                                                                                                    \
    array->items = (item_type *)destination;                                                        \
    array->capacity = capacity;                                                                     \
}                                                                                                   \
                                                                                                    \
static inline void name##_extend(array_type* array, int size) {                                     \
    if (array->capacity < size) {                                                                   \
        name##_resize(array, size * 2);                                                             \
    }                                                                                               \
}                                                                                                   \
                                                                                                    \
static inline array_type* name##_new(int capacity) {                                                \
    array_type* array = malloc(sizeof(array_type));                                                 \
    array->count = 0;                                                                               \
    array->capacity = 0;                                                                            \
    name##_resize(array, capacity);                                                                 \
    return array;                                                                                   \
}                                                                                                   \
                                                                                                    \
static inline void name##_init(array_type* array, int capacity) {                                   \
    array->count = 0;                                                                               \
    array->capacity = 0;                                                                            \
    name##_resize(array, capacity);                                                                 \
}                                                                                                   \
                                                                                                    \
static inline void name##_delete(array_type* array) {                                               \
    free(array->items);                                                                             \
    free(array);                                                                                    \
}                                                                                                   \
                                                                                                    \
static inline void name##_clear(array_type* array) {                                                \
    array->count = 0;                                                                               \
}                                                                                                   \
                                                                                                    \
static inline int name##_append(array_type* array, item_type item) {                                \
    name##_extend(array, array->count + 1);                                                         \
    array->items[array->count++] = item;                                                            \
    return array->count - 1;                                                                        \
}                                                                                                   \
                                                                                                    \
static inline void name##_insert(array_type* array, item_type item, int index) {                    \
    name##_extend(array, array->count + 1);                                                         \
                                                                                                    \
    if (index < array->count) {                                                                     \
        u8* source = (u8 *)(array->items + array->count - 1);                                       \
        u8* destination = (u8 *)(array->items + array->count);                                      \
                                                                                                    \
        for (int i = 0; i < (array->count - index) * sizeof(item_type) - 1; i++) {                  \
            *destination-- = *source--;                                                             \
        }                                                                                           \
    }                                                                                               \
                                                                                                    \
    array->items[index] = item;                                                                     \
    array->count++;                                                                                 \
}                                                                                                   \
                                                                                                    \
static inline void name##_remove(array_type* array, int index) {                                    \
    if (index < array->count) {                                                                     \
        u8* source = (u8* )(array->items + index + 1);                                              \
        u8* destination = (u8 *)(array->items + index);                                             \
                                                                                                    \
        for (int i = 0; i < (array->count - index) * sizeof(item_type) - 1; i++) {                  \
            *destination++ = *source++;                                                             \
        }                                                                                           \
    }                                                                                               \
    array->count--;                                                                                 \
}                                                                                                   \
                                                                                                    \
static inline item_type* name##_allocate_last(array_type* array) {                                  \
    name##_extend(array, array->count + 1);                                                         \
    return &array->items[array->count++];                                                           \
}                                                                                                   \

#endif
