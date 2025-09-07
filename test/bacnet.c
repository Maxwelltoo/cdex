#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cdex.h"

static uint64_t zigzag_encode_64(int64_t n) { return (n << 1) ^ (n >> 63); }

static int64_t zigzag_decode_64(uint64_t n) { return (n >> 1) ^ (-(int64_t)(n & 1)); }

static int encode_varint(uint8_t* buffer, uint64_t value) {
    int count = 0;
    while (value >= 0x80) {
        buffer[count++] = (uint8_t)(value | 0x80);
        value >>= 7;
    }
    buffer[count++] = (uint8_t)value;
    return count;
}

static uint64_t decode_varint(const uint8_t* buffer, int* decoded_len) {
    uint64_t value = 0;
    int shift = 0;
    int i = 0;
    while (1) {
        uint8_t byte = buffer[i];
        value |= (uint64_t)(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
        shift += 7;
        i++;
    }
    *decoded_len = i + 1;
    return value;
}

// "4.1.85" -> |4(varint)|1(varint)|85(varint)|
static size_t pack_bacnet_field_name(const char* name, uint8_t* buffer, size_t buffer_size) {
    size_t total_bytes = 0;
    char* name_copy = strdup(name);
    if (!name_copy) return 0;

    char* token = strtok(name_copy, ".");
    while (token != NULL) {
        int part = atoi(token);
        uint8_t varint_buf[10];
        int varint_size = encode_varint(varint_buf, (uint64_t)part);
        if (total_bytes + varint_size > buffer_size) {
            free(name_copy);
            return 0; // Buffer overflow
        }
        memcpy(buffer + total_bytes, varint_buf, varint_size);
        total_bytes += varint_size;
        token = strtok(NULL, ".");
    }

    free(name_copy);
    return total_bytes;
}

static size_t pack_bacnet_descriptor(const cdex_descriptor_t* desc, uint8_t* buffer, size_t buffer_size) {
    if (!desc || !buffer || buffer_size < 2) return 0;

    uint8_t* ptr = buffer;

    *ptr++ = (uint8_t)(desc->field_count);

    for (int i = 0; i < desc->field_count; ++i) {
        if (ptr + 1 > buffer + buffer_size) return 0; // Buffer overflow
        *ptr++ = (uint8_t)field->type;
    }

    for (int i = 0; i < desc->field_count; ++i) {
        const cdex_field_t* field = &desc->fields[i];
        size_t name_bytes = pack_bacnet_field_name(field->name, ptr, buffer + buffer_size - ptr);
        if (name_bytes == 0) return 0; // Error in packing field name
        ptr += name_bytes;
    }

    return ptr - buffer;
}
