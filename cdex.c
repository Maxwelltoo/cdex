#include "cdex.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static uint16_t crc16_update(uint16_t crc, uint8_t data) {
    crc ^= data;
    for (int i = 0; i < 8; ++i) {
        if (crc & 1) {
            crc = (crc >> 1) ^ 0xA001;
        } else {
            crc = (crc >> 1);
        }
    }
    return crc;
}

static uint16_t calculate_crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc = crc16_update(crc, data[i]);
    }
    return crc;
}

// --- 描述符管理 ---
// 在实际应用中，这部分可以从文件或配置中加载
#define DESCRIPTOR_COUNT 2
static cdex_descriptor_t g_descriptors[DESCRIPTOR_COUNT];
static bool g_manager_initialized = false;

// 定义您的描述符字符串
static const cdex_descriptor_t g_descriptor_templates[DESCRIPTOR_COUNT] = {
    {
        .id = 1001,
        .raw_string = "temp-f32,humidity-u16,pressure-u32,status-u8,device_name-str"
    },
    {
        .id = 2005,
        .raw_string = "voltage-i16,current-i16,power-f32,error_code-u32,uptime-u64"
    }
};

static cdex_data_type_t str_to_type(const char* str, size_t* size) {
    if (strcmp(str, "u8") == 0) { *size = 1; return CDE_TYPE_U8; }
    if (strcmp(str, "i8") == 0) { *size = 1; return CDE_TYPE_I8; }
    if (strcmp(str, "u16") == 0) { *size = 2; return CDE_TYPE_U16; }
    if (strcmp(str, "i16") == 0) { *size = 2; return CDE_TYPE_I16; }
    if (strcmp(str, "u32") == 0) { *size = 4; return CDE_TYPE_U32; }
    if (strcmp(str, "i32") == 0) { *size = 4; return CDE_TYPE_I32; }
    if (strcmp(str, "u64") == 0) { *size = 8; return CDE_TYPE_U64; }
    if (strcmp(str, "i64") == 0) { *size = 8; return CDE_TYPE_I64; }
    if (strcmp(str, "f32") == 0) { *size = 4; return CDE_TYPE_F32; }
    if (strcmp(str, "d64") == 0) { *size = 8; return CDE_TYPE_D64; }
    if (strcmp(str, "str") == 0) { *size = 0; return CDE_TYPE_STR; } // Size is variable
    *size = 0;
    return CDE_TYPE_UNKNOWN;
}

void cdex_manager_init(void) {
    if (g_manager_initialized) return;

    for (int i = 0; i < DESCRIPTOR_COUNT; ++i) {
        g_descriptors[i] = g_descriptor_templates[i];
        
        char* str_copy = strdup(g_descriptors[i].raw_string);
        if (!str_copy) {
            // Handle memory allocation error
            return;
        }

        char* segment = strtok(str_copy, ",");
        int field_idx = 0;
        while (segment != NULL && field_idx < CDE_MAX_FIELDS) {
            char* hyphen = strrchr(segment, '-');
            if (hyphen) {
                *hyphen = '\0'; // Split name and type
                strncpy(g_descriptors[i].fields[field_idx].name, segment, sizeof(g_descriptors[i].fields[field_idx].name) - 1);
                g_descriptors[i].fields[field_idx].type = str_to_type(hyphen + 1, &g_descriptors[i].fields[field_idx].size);
            }
            field_idx++;
            segment = strtok(NULL, ",");
        }
        g_descriptors[i].field_count = field_idx;
        free(str_copy);
    }
    g_manager_initialized = true;
}

const cdex_descriptor_t* cdex_get_descriptor_by_id(uint16_t id) {
    if (!g_manager_initialized) {
        cdex_manager_init();
    }
    for (int i = 0; i < DESCRIPTOR_COUNT; ++i) {
        if (g_descriptors[i].id == id) {
            return &g_descriptors[i];
        }
    }
    return NULL;
}

// --- 核心功能实现 ---

int cdex_pack(const cdex_packet_t* packet, uint8_t* buffer, size_t buffer_size) {
    const cdex_descriptor_t* desc = cdex_get_descriptor_by_id(packet->descriptor_id);
    if (!desc) return -1;

    // 计算Bitmap字节长度
    size_t bitmap_bytes = (desc->field_count + 7) / 8;

    uint8_t* ptr = buffer;
    
    // 1. 写入Descriptor ID (小端)
    if (ptr + 2 > buffer + buffer_size) return -1;
    *(uint16_t*)ptr = packet->descriptor_id;
    ptr += 2;

    // 2. 写入Bitmap
    if (ptr + bitmap_bytes > buffer + buffer_size) return -1;
    memcpy(ptr, &packet->bitmap, bitmap_bytes);
    ptr += bitmap_bytes;

    // 3. 写入Data List
    int data_idx = 0;
    for (int i = 0; i < desc->field_count; ++i) {
        if ((packet->bitmap >> i) & 1) {
            const cdex_field_descriptor_t* field_desc = &desc->fields[i];
            const cdex_value_t* value = &packet->values[data_idx];
            
            if (field_desc->type == CDE_TYPE_STR) {
                size_t str_len = strlen(value->str) + 1; // +1 for null terminator
                if (ptr + str_len > buffer + buffer_size) return -1;
                memcpy(ptr, value->str, str_len);
                ptr += str_len;
            } else {
                if (ptr + field_desc->size > buffer + buffer_size) return -1;
                memcpy(ptr, value, field_desc->size);
                ptr += field_desc->size;
            }
            data_idx++;
        }
    }

    // 4. 计算并写入Checksum
    size_t data_len = ptr - buffer;
    uint16_t crc = calculate_crc16(buffer, data_len);
    if (ptr + 2 > buffer + buffer_size) return -1;
    *(uint16_t*)ptr = crc;
    ptr += 2;

    return ptr - buffer;
}


cdex_status_t cdex_parse(const uint8_t* buffer, size_t buffer_len, cdex_packet_t* packet_out) {
    if (buffer_len < 5) return CDE_ERROR_INVALID_PACKET; // 至少 ID(2) + Bitmap(1) + CRC(2)

    // 1. 校验Checksum
    uint16_t received_crc = *(uint16_t*)(buffer + buffer_len - 2);
    uint16_t calculated_crc = calculate_crc16(buffer, buffer_len - 2);
    if (received_crc != calculated_crc) return CDE_ERROR_BAD_CHECKSUM;

    memset(packet_out, 0, sizeof(cdex_packet_t));
    const uint8_t* ptr = buffer;

    // 2. 解析Descriptor ID
    packet_out->descriptor_id = *(uint16_t*)ptr;
    ptr += 2;

    const cdex_descriptor_t* desc = cdex_get_descriptor_by_id(packet_out->descriptor_id);
    if (!desc) return CDE_ERROR_DESCRIPTOR_NOT_FOUND;

    // 3. 解析Bitmap
    size_t bitmap_bytes = (desc->field_count + 7) / 8;
    if (ptr + bitmap_bytes > buffer + buffer_len - 2) return CDE_ERROR_INVALID_PACKET;
    memcpy(&packet_out->bitmap, ptr, bitmap_bytes);
    ptr += bitmap_bytes;

    // 4. 解析Data List
    int data_idx = 0;
    for (int i = 0; i < desc->field_count; ++i) {
        if ((packet_out->bitmap >> i) & 1) {
            const cdex_field_descriptor_t* field_desc = &desc->fields[i];
            cdex_value_t* value_out = &packet_out->values[data_idx];

            if (field_desc->type == CDE_TYPE_STR) {
                const char* str_start = (const char*)ptr;
                size_t max_len = (buffer + buffer_len - 2) - ptr;
                size_t str_len = strnlen(str_start, max_len);

                if (str_len == max_len) return CDE_ERROR_INVALID_DATA; // No null terminator found

                value_out->str = (char*)malloc(str_len + 1);
                if (!value_out->str) return CDE_ERROR_MEMORY_ALLOCATION;
                memcpy(value_out->str, str_start, str_len + 1);
                ptr += str_len + 1;
            } else {
                if (ptr + field_desc->size > buffer + buffer_len - 2) return CDE_ERROR_INVALID_PACKET;
                memcpy(value_out, ptr, field_desc->size);
                ptr += field_desc->size;
            }
            data_idx++;
        }
    }
    packet_out->data_count = data_idx;

    return CDE_SUCCESS;
}

cJSON* cdex_packet_to_json(const cdex_packet_t* packet) {
    const cdex_descriptor_t* desc = cdex_get_descriptor_by_id(packet->descriptor_id);
    if (!desc) return NULL;

    cJSON* root = cJSON_CreateObject();
    if (!root) return NULL;
    
    // 添加一个元数据字段，便于调试
    cJSON_AddNumberToObject(root, "_descriptor_id", packet->descriptor_id);

    int data_idx = 0;
    for (int i = 0; i < desc->field_count; ++i) {
        if ((packet->bitmap >> i) & 1) {
            const cdex_field_descriptor_t* field_desc = &desc->fields[i];
            const cdex_value_t* value = &packet->values[data_idx];
            
            switch (field_desc->type) {
                case CDE_TYPE_U8:  cJSON_AddNumberToObject(root, field_desc->name, value->u8); break;
                case CDE_TYPE_I8:  cJSON_AddNumberToObject(root, field_desc->name, value->i8); break;
                case CDE_TYPE_U16: cJSON_AddNumberToObject(root, field_desc->name, value->u16); break;
                case CDE_TYPE_I16: cJSON_AddNumberToObject(root, field_desc->name, value->i16); break;
                case CDE_TYPE_U32: cJSON_AddNumberToObject(root, field_desc->name, value->u32); break;
                case CDE_TYPE_I32: cJSON_AddNumberToObject(root, field_desc->name, value->i32); break;
                case CDE_TYPE_U64: cJSON_AddNumberToObject(root, field_desc->name, (double)value->u64); break;
                case CDE_TYPE_I64: cJSON_AddNumberToObject(root, field_desc->name, (double)value->i64); break;
                case CDE_TYPE_F32: cJSON_AddNumberToObject(root, field_desc->name, value->f32); break;
                case CDE_TYPE_D64: cJSON_AddNumberToObject(root, field_desc->name, value->d64); break;
                case CDE_TYPE_STR: cJSON_AddStringToObject(root, field_desc->name, value->str); break;
                default: break;
            }
            data_idx++;
        }
    }
    return root;
}

void cdex_free_packet_strings(cdex_packet_t* packet) {
    const cdex_descriptor_t* desc = cdex_get_descriptor_by_id(packet->descriptor_id);
    if (!desc) return;

    int data_idx = 0;
    for (int i = 0; i < desc->field_count; ++i) {
        if ((packet->bitmap >> i) & 1) {
            if (desc->fields[i].type == CDE_TYPE_STR) {
                if (packet->values[data_idx].str) {
                    free(packet->values[data_idx].str);
                    packet->values[data_idx].str = NULL;
                }
            }
            data_idx++;
        }
    }
}
