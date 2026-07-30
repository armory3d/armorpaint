
// .arm file format parser
// msgpack with typed arrays

#pragma once

#include "iron_array.h"
#include "iron_map.h"
#include <stdbool.h>
#include <stdint.h>

void *armpack_decode(buffer_t *b);

void armpack_encode_start(void *encoded);
int  armpack_encode_end();
void armpack_encode_map(uint32_t count);
void armpack_encode_array(uint32_t count);
void armpack_encode_array_f32(f32_array_t *f32a);
void armpack_encode_array_i32(i32_array_t *i32a);
void armpack_encode_array_i16(i16_array_t *i16a);
void armpack_encode_array_u8(u8_array_t *u8a);
void armpack_encode_array_string(string_array_t *strings);
void armpack_encode_string(char *str);
void armpack_encode_i32(int32_t i);
void armpack_encode_f32(float f);
void armpack_encode_bool(bool b);
void armpack_encode_null();

uint32_t armpack_size_map();
uint32_t armpack_size_array();
uint32_t armpack_size_array_f32(f32_array_t *f32a);
uint32_t armpack_size_array_u8(u8_array_t *u8a);
uint32_t armpack_size_string(char *str);
uint32_t armpack_size_i32();
uint32_t armpack_size_f32();
uint32_t armpack_size_bool();

any_map_t *armpack_decode_to_map(buffer_t *b);
char      *armpack_decode_to_json(buffer_t *b);
char      *armpack_decode_to_json_omit_buffers(buffer_t *b);
float      armpack_map_get_f32(any_map_t *map, char *key);
int        armpack_map_get_i32(any_map_t *map, char *key);
void       armpack_map_set_i32(any_map_t *map, char *key, int value);

void armpack_write_u8(uint8_t i);
void armpack_write_i16(int16_t i);
void armpack_write_u32(uint32_t i);
void armpack_write_i32(int32_t i);
void armpack_write_f32(float i);

/* JSON object:

    {
        name: "test",
        point: { x: 2, y: 4 },
        array: i32_array_create([1, 2, 3])
    }
*/

/* C struct:

    typedef struct point {
        int x;
        int y;
    } point_t;

    typedef struct test {
        char *name;
        point_t point;
        int32_array_t *array;
        // Optional pointer for storing runtime data
        void *_;
    } test_t;
*/

/*
    void encode_decode_test() {
        point_t a;
        a.x = 3;
        a.y = 9;

        uint32_t size = 0;
        size += armpack_size_map();
        size += armpack_size_string("x");
        size += armpack_size_i32();
        size += armpack_size_string("y");
        size += armpack_size_i32();

        void *encoded = malloc(size);
        armpack_encode_start(encoded);
        armpack_encode_map(2);
        armpack_encode_string("x");
        armpack_encode_i32(a.x);
        armpack_encode_string("y");
        armpack_encode_i32(a.y);

        buffer_t b = { .buffer = encoded, .length = size };
        point_t *decoded = armpack_decode(b);
    }
*/
