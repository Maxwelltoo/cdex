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

static cdex_data_type_t str_to_type(const char* str, size_t* size) {
    if (strcmp(str, "u8") == 0) { *size = 1; return CDEX_TYPE_U8; }
    if (strcmp(str, "i8") == 0) { *size = 1; return CDEX_TYPE_I8; }
    if (strcmp(str, "u16") == 0) { *size = 2; return CDEX_TYPE_U16; }
    if (strcmp(str, "i16") == 0) { *size = 2; return CDEX_TYPE_I16; }
    if (strcmp(str, "u32") == 0) { *size = 4; return CDEX_TYPE_U32; }
    if (strcmp(str, "i32") == 0) { *size = 4; return CDEX_TYPE_I32; }
    if (strcmp(str, "u64") == 0) { *size = 8; return CDEX_TYPE_U64; }
    if (strcmp(str, "i64") == 0) { *size = 8; return CDEX_TYPE_I64; }
    if (strcmp(str, "f32") == 0) { *size = 4; return CDEX_TYPE_F32; }
    if (strcmp(str, "d64") == 0) { *size = 8; return CDEX_TYPE_D64; }
    // variable-sized types
    *size = 0;
    if (strcmp(str, "num") == 0) { return CDEX_TYPE_NUM; }
    if (strcmp(str, "bin") == 0) { return CDEX_TYPE_BIN; }
    if (strcmp(str, "str") == 0) { return CDEX_TYPE_STR; }
    return CDEX_TYPE_UNKNOWN;
}

static char *type_to_str(cdex_data_type_t type) {
    switch (type) {
        case CDEX_TYPE_U8: return "u8";
        case CDEX_TYPE_I8: return "i8";
        case CDEX_TYPE_U16: return "u16";
        case CDEX_TYPE_I16: return "i16";
        case CDEX_TYPE_U32: return "u32";
        case CDEX_TYPE_I32: return "i32";
        case CDEX_TYPE_U64: return "u64";
        case CDEX_TYPE_I64: return "i64";
        case CDEX_TYPE_F32: return "f32";
        case CDEX_TYPE_D64: return "d64";
        case CDEX_TYPE_NUM: return "num";
        case CDEX_TYPE_BIN: return "bin";
        case CDEX_TYPE_STR: return "str";
        default: return "unknown";
    }
}

static uint64_t zigzag_encode_64(int64_t n) { return (n << 1) ^ (n >> 63); }

static int64_t zigzag_decode_64(uint64_t n) { return (n >> 1) ^ (-(int64_t)(n & 1)); }

/**
 * @brief 将 uint64_t 编码为 Varint 格式写入缓冲区
 * @return 写入的字节数
 */
static int encode_varint(uint8_t* buffer, uint64_t value) {
    int count = 0;
    while (value >= 0x80) {
        buffer[count++] = (uint8_t)(value | 0x80);
        value >>= 7;
    }
    buffer[count++] = (uint8_t)value;
    return count;
}

/**
 * @brief 从缓冲区解码 Varint 为 uint64_t
 * @param decoded_len [out] 解码所用的字节数
 * @return 解码后的值
 */
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

// --- 描述符管理 ---
static cdex_descriptor_node_t* g_descriptor_list_head = NULL;

void cdex_manager_init(void) {
    cdex_manager_cleanup();
}

void cdex_manager_cleanup(void) {
    cdex_descriptor_node_t* current = g_descriptor_list_head;
    while (current != NULL) {
        cdex_descriptor_node_t* next = current->next;
        // 释放动态分配的描述符字符串
        if (current->descriptor.raw_string) {
            free(current->descriptor.raw_string);
        }
        // 释放节点本身
        free(current);
        current = next;
    }
    g_descriptor_list_head = NULL;
}

const cdex_descriptor_t* cdex_get_descriptor_by_id(uint16_t id) {
    if (!g_descriptor_list_head) return NULL;
    cdex_descriptor_node_t* current = g_descriptor_list_head;
    while (current != NULL) {
        if (current->descriptor.id == id) {
            return &current->descriptor;
        }
        current = current->next;
    }
    return NULL;
}

cdex_status_t cdex_descriptor_register(uint16_t id, const char* descriptor_string) {
    if (cdex_get_descriptor_by_id(id) != NULL) {
        return CDEX_ERROR_ID_EXISTS;
    }
    cdex_descriptor_node_t* new_node = (cdex_descriptor_node_t*)malloc(sizeof(cdex_descriptor_node_t));
    if (!new_node) return CDEX_ERROR_MEMORY_ALLOCATION;
    memset(new_node, 0, sizeof(cdex_descriptor_node_t));
    new_node->descriptor.id = id;
    new_node->descriptor.raw_string = strdup(descriptor_string);
    if (!new_node->descriptor.raw_string) {
        free(new_node);
        return CDEX_ERROR_MEMORY_ALLOCATION;
    }
    // 解析字符串以填充 fields 数组
    char* str_copy = strdup(descriptor_string);
    if (!str_copy) {
        free(new_node->descriptor.raw_string);
        free(new_node);
        return CDEX_ERROR_MEMORY_ALLOCATION;
    }
    char* segment = strtok(str_copy, ",");
    int field_idx = 0;
    while (segment != NULL && field_idx < CDEX_MAX_FIELDS) {
        char* hyphen = strrchr(segment, ':');
        if (hyphen) {
            *hyphen = '\0'; // 分割名称和类型
            strncpy(new_node->descriptor.fields[field_idx].name, segment, sizeof(new_node->descriptor.fields[field_idx].name) - 1);
            new_node->descriptor.fields[field_idx].type = str_to_type(hyphen + 1, &new_node->descriptor.fields[field_idx].size);
        }
        field_idx++;
        segment = strtok(NULL, ",");
    }
    new_node->descriptor.field_count = field_idx;
    free(str_copy);
    // 将新节点添加到链表头部
    new_node->next = g_descriptor_list_head;
    g_descriptor_list_head = new_node;
    return CDEX_SUCCESS;
}

cdex_status_t cdex_descriptor_load(uint16_t id, const cdex_field_t* fields, int field_count) {
    if (cdex_get_descriptor_by_id(id) != NULL) {
        return CDEX_ERROR_ID_EXISTS;
    }
    if (field_count > CDEX_MAX_FIELDS) {
        return CDEX_ERROR_INDEX_OUT_OF_BOUNDS;
    }
    cdex_descriptor_node_t* new_node = (cdex_descriptor_node_t*)malloc(sizeof(cdex_descriptor_node_t));
    if (!new_node) return CDEX_ERROR_MEMORY_ALLOCATION;
    memset(new_node, 0, sizeof(cdex_descriptor_node_t));
    new_node->descriptor.id = id;
    new_node->descriptor.field_count = field_count;
    new_node->descriptor.raw_string = NULL; // 没有原始字符串
    memcpy(new_node->descriptor.fields, fields, field_count * sizeof(cdex_field_t));

    // 将新节点添加到链表头部
    new_node->next = g_descriptor_list_head;
    g_descriptor_list_head = new_node;
    return CDEX_SUCCESS;
}

code_status_t cdex_fields_to_string(char *buf, size_t buf_size, const cdex_field_t *fields, int field_count) {
    if (!buf || buf_size == 0 || !fields || field_count <= 0 || field_count > CDEX_MAX_FIELDS) {
        return CDEX_ERROR_INVALID_DATA;
    }

    size_t offset = 0;
    for (int i = 0; i < field_count; ++i) {
        char *type_str = type_to_str(fields[i].type);
        if (strcmp(type_str, "unknown") == 0) {
            return CDEX_ERROR_INVALID_DATA;
        }

        int written = snprintf(buf + offset, buf_size - offset, "%s:%s", fields[i].name, type_str);
        if (written < 0 || (size_t)written >= buf_size - offset) {
            return CDEX_ERROR_BUFFER_TOO_SMALL;
        }
        offset += written;
        if (i < field_count - 1) {
            if (offset < buf_size - 1) {
                buf[offset++] = ',';
                buf[offset] = '\0';
            } else {
                return CDEX_ERROR_BUFFER_TOO_SMALL;
            }
        }
    }
    return CDEX_SUCCESS;
}

code_status_t cdex_string_to_fields(const char *str, cdex_field_t *fields, int *field_count) {
    if (!str || !fields || !field_count || *field_count <= 0) {
        return CDEX_ERROR_INVALID_DATA;
    }

    char *str_copy = strdup(str);
    if (!str_copy) {
        return CDEX_ERROR_MEMORY_ALLOCATION;
    }

    char *segment = strtok(str_copy, ",");
    int count = 0;

    while (segment != NULL && count < *field_count) {
        char *hyphen = strrchr(segment, ':');
        if (hyphen) {
            *hyphen = '\0'; // 分割名称和类型
            strncpy(fields[count].name, segment, sizeof(fields[count].name) - 1);
            fields[count].type = str_to_type(hyphen + 1, &fields[count].size);
            count++;
        }
        segment = strtok(NULL, ",");
    }

    free(str_copy);
    *field_count = count;
    return CDEX_SUCCESS;
}

static int count_set_bits_before(uint64_t n, int index) {
    int count = 0;
    for (int i = 0; i < index; i++) {
        if ((n >> i) & 1) {
            count++;
        }
    }
    return count;
}

void cdex_packet_init(cdex_packet_t* packet, uint16_t descriptor_id) {
    if (!packet) return;
    memset(packet, 0, sizeof(cdex_packet_t));
    packet->descriptor_id = descriptor_id;
}

cdex_status_t cdex_packet_push(cdex_packet_t* packet, int field_index, cdex_value_t value) {
    if (!packet) return CDEX_ERROR_INVALID_DATA;
    if (field_index < 0 || field_index >= CDEX_MAX_FIELDS) return CDEX_ERROR_INDEX_OUT_OF_BOUNDS;

    const cdex_descriptor_t* desc = cdex_get_descriptor_by_id(packet->descriptor_id);
    if (!desc) return CDEX_ERROR_DESCRIPTOR_NOT_FOUND;
    if (field_index >= desc->field_count) return CDEX_ERROR_INDEX_OUT_OF_BOUNDS;

    int insertion_index = count_set_bits_before(packet->bitmap, field_index);
    bool already_exists = (packet->bitmap >> field_index) & 1;

    if (already_exists) {
        // 字段已存在，直接覆盖值
        packet->values[insertion_index] = value;
    } else {
        // 字段不存在，需要插入
        if (packet->data_count >= CDEX_MAX_FIELDS) return CDEX_ERROR_PACKET_FULL;

        // 为新元素腾出空间，将插入点之后的所有元素向后移动一位
        memmove(&packet->values[insertion_index + 1],
                &packet->values[insertion_index],
                (packet->data_count - insertion_index) * sizeof(cdex_value_t));

        // 插入新值
        packet->values[insertion_index] = value;

        // 更新 bitmap 和计数
        packet->bitmap |= (1ULL << field_index);
        packet->data_count++;
    }

    return CDEX_SUCCESS;
}

cdex_status_t cdex_packet_pop(cdex_packet_t* packet, int field_index) {
    if (!packet) return CDEX_ERROR_INVALID_DATA;
    if (field_index < 0 || field_index >= CDEX_MAX_FIELDS) return CDEX_ERROR_INDEX_OUT_OF_BOUNDS;

    bool exists = (packet->bitmap >> field_index) & 1;
    if (!exists) {
        // 字段不存在，无需操作
        return CDEX_SUCCESS;
    }

    int removal_index = count_set_bits_before(packet->bitmap, field_index);

    // 将移除点之后的所有元素向前移动一位，覆盖被删除的元素
    memmove(&packet->values[removal_index],
            &packet->values[removal_index + 1],
            (packet->data_count - removal_index - 1) * sizeof(cdex_value_t));

    // 更新 bitmap 和计数
    packet->bitmap &= ~(1ULL << field_index);
    packet->data_count--;

    return CDEX_SUCCESS;
}

int cdex_packet_calculate_packed_size(const cdex_packet_t* packet) {
    if (!packet) return -1;
    const cdex_descriptor_t* desc = cdex_get_descriptor_by_id(packet->descriptor_id);
    if (!desc) return -1;

    // 基础开销: ID (2) + Checksum (2)
    int total_size = 4;

    // Bitmap 开销
    total_size += (desc->field_count + 7) / 8;

    // Data List 开销
    int data_idx = 0;
    for (int i = 0; i < desc->field_count; ++i) {
        if ((packet->bitmap >> i) & 1) {
            const cdex_field_t* field_desc = &desc->fields[i];
            const cdex_value_t* value = &packet->values[data_idx];
            uint8_t varint_buffer[10];

            switch (field_desc->type) {
                case CDEX_TYPE_STR:
                    total_size += strlen(value->str) + 1; // +1 for null terminator
                    break;
                case CDEX_TYPE_BIN:
                    total_size += value->bin[0] + 1; // +1 for length byte
                    break;
                case CDEX_TYPE_NUM:
                    total_size += encode_varint(varint_buffer, zigzag_encode_64(value->i64));
                    break;
                default:
                    total_size += field_desc->size;
                    break;
            }
            data_idx++;
        }
    }

    return total_size;
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
            const cdex_field_t* field_desc = &desc->fields[i];
            const cdex_value_t* value = &packet->values[data_idx];

            if (field_desc->type == CDEX_TYPE_STR) {
                size_t str_len = strlen(value->str) + 1; // +1 for null terminator
                if (ptr + str_len > buffer + buffer_size) return -1;
                memcpy(ptr, value->str, str_len);
                ptr += str_len;
            } else if (field_desc->type == CDEX_TYPE_BIN) {
                if (ptr + value->bin[0] + 1 > buffer + buffer_size) return -1;
                memcpy(ptr, value->bin, value->bin[0] + 1);
                ptr += value->bin[0] + 1;
            } else if (field_desc->type == CDEX_TYPE_NUM) {
                uint8_t varint_buffer[10];
                int varint_size = encode_varint(varint_buffer, zigzag_encode_64(value->i64));
                if (ptr + varint_size > buffer + buffer_size) return -1;
                memcpy(ptr, varint_buffer, varint_size);
                ptr += varint_size;
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
    if (buffer_len < 5) return CDEX_ERROR_INVALID_PACKET; // 至少 ID(2) + Bitmap(1) + CRC(2)

    // 1. 校验Checksum
    uint16_t received_crc = *(uint16_t*)(buffer + buffer_len - 2);
    uint16_t calculated_crc = calculate_crc16(buffer, buffer_len - 2);
    if (received_crc != calculated_crc) return CDEX_ERROR_BAD_CHECKSUM;

    memset(packet_out, 0, sizeof(cdex_packet_t));
    const uint8_t* ptr = buffer;

    // 2. 解析Descriptor ID
    packet_out->descriptor_id = *(uint16_t*)ptr;
    ptr += 2;

    const cdex_descriptor_t* desc = cdex_get_descriptor_by_id(packet_out->descriptor_id);
    if (!desc) return CDEX_ERROR_DESCRIPTOR_NOT_FOUND;

    // 3. 解析Bitmap
    size_t bitmap_bytes = (desc->field_count + 7) / 8;
    if (ptr + bitmap_bytes > buffer + buffer_len - 2) return CDEX_ERROR_INVALID_PACKET;
    memcpy(&packet_out->bitmap, ptr, bitmap_bytes);
    ptr += bitmap_bytes;

    // 4. 解析Data List
    int data_idx = 0;
    for (int i = 0; i < desc->field_count; ++i) {
        if ((packet_out->bitmap >> i) & 1) {
            const cdex_field_t* field_desc = &desc->fields[i];
            cdex_value_t* value_out = &packet_out->values[data_idx];

            if (field_desc->type == CDEX_TYPE_STR) {
                const char* str_start = (const char*)ptr;
                size_t max_len = (buffer + buffer_len - 2) - ptr;
                size_t str_len = strnlen(str_start, max_len);

                if (str_len == max_len) return CDEX_ERROR_INVALID_DATA; // No null terminator found

                value_out->str = (char*)malloc(str_len + 1);
                if (!value_out->str) return CDEX_ERROR_MEMORY_ALLOCATION;
                memcpy(value_out->str, str_start, str_len + 1);
                ptr += str_len + 1;
            } else if (field_desc->type == CDEX_TYPE_BIN) {
                if (ptr + 1 > buffer + buffer_len - 2) return CDEX_ERROR_BUFFER_TOO_SMALL;
                size_t bin_len = ptr[0];
                if (ptr + 1 + bin_len > buffer + buffer_len - 2) return CDEX_ERROR_BUFFER_TOO_SMALL;

                value_out->bin = (uint8_t*)malloc(bin_len + 1);
                if (!value_out->bin) return CDEX_ERROR_MEMORY_ALLOCATION;
                memcpy(value_out->bin, ptr, bin_len + 1);
                ptr += bin_len + 1;
            } else if (field_desc->type == CDEX_TYPE_NUM) {
                int varint_size = 0;
                uint64_t decoded = decode_varint(ptr, &varint_size);
                if (ptr + varint_size > buffer + buffer_len - 2) return CDEX_ERROR_BUFFER_TOO_SMALL;

                value_out->i64 = zigzag_decode_64(decoded);
                ptr += varint_size;
            } else {
                if (ptr + field_desc->size > buffer + buffer_len - 2) return CDEX_ERROR_BUFFER_TOO_SMALL;
                memcpy(value_out, ptr, field_desc->size);
                ptr += field_desc->size;
            }
            data_idx++;
        }
    }
    packet_out->data_count = data_idx;

    return CDEX_SUCCESS;
}

#ifdef CDEX_PARSE_TO_JSON
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
            const cdex_field_t* field_desc = &desc->fields[i];
            const cdex_value_t* value = &packet->values[data_idx];

            switch (field_desc->type) {
                case CDEX_TYPE_U8:  cJSON_AddNumberToObject(root, field_desc->name, value->u8); break;
                case CDEX_TYPE_I8:  cJSON_AddNumberToObject(root, field_desc->name, value->i8); break;
                case CDEX_TYPE_U16: cJSON_AddNumberToObject(root, field_desc->name, value->u16); break;
                case CDEX_TYPE_I16: cJSON_AddNumberToObject(root, field_desc->name, value->i16); break;
                case CDEX_TYPE_U32: cJSON_AddNumberToObject(root, field_desc->name, value->u32); break;
                case CDEX_TYPE_I32: cJSON_AddNumberToObject(root, field_desc->name, value->i32); break;
                case CDEX_TYPE_U64: cJSON_AddNumberToObject(root, field_desc->name, (double)value->u64); break;
                case CDEX_TYPE_I64: cJSON_AddNumberToObject(root, field_desc->name, (double)value->i64); break;
                case CDEX_TYPE_NUM: cJSON_AddNumberToObject(root, field_desc->name, (double)value->i64); break;
                case CDEX_TYPE_F32: cJSON_AddNumberToObject(root, field_desc->name, value->f32); break;
                case CDEX_TYPE_D64: cJSON_AddNumberToObject(root, field_desc->name, value->d64); break;
                case CDEX_TYPE_STR: cJSON_AddStringToObject(root, field_desc->name, value->str); break;
                case CDEX_TYPE_BIN: {
                    cJSON* bin_array = cJSON_CreateArray();
                    for (size_t j = 1; j <= value->bin[0]; ++j) { // value->bin[0] is length
                        cJSON_AddItemToArray(bin_array, cJSON_CreateNumber(value->bin[j]));
                    }
                    cJSON_AddItemToObject(root, field_desc->name, bin_array);
                    break;
                }
                default: break;
            }
            data_idx++;
        }
    }
    return root;
}
#endif

void cdex_free_packet_memory(cdex_packet_t* packet) {
    const cdex_descriptor_t* desc = cdex_get_descriptor_by_id(packet->descriptor_id);
    if (!desc) return;

    int data_idx = 0;
    for (int i = 0; i < desc->field_count; ++i) {
        if ((packet->bitmap >> i) & 1) {
            if (desc->fields[i].type == CDEX_TYPE_STR) {
                if (packet->values[data_idx].str) {
                    free(packet->values[data_idx].str);
                    packet->values[data_idx].str = NULL;
                }
            }
            if (desc->fields[i].type == CDEX_TYPE_BIN) {
                if (packet->values[data_idx].bin) {
                    free(packet->values[data_idx].bin);
                    packet->values[data_idx].bin = NULL;
                }
            }
            data_idx++;
        }
    }
}
