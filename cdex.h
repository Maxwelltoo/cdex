#ifndef CDEX_PROTOCOL_H
#define CDEX_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "cjson/cJSON.h"

#define CDEX_MAX_FIELDS 64
#define CDEX_FIELD_NAME_LEN 32

// 数据类型枚举
typedef enum {
    CDEX_TYPE_U8, CDEX_TYPE_I8,
    CDEX_TYPE_U16, CDEX_TYPE_I16,
    CDEX_TYPE_U32, CDEX_TYPE_I32,
    CDEX_TYPE_U64, CDEX_TYPE_I64,
    CDEX_TYPE_F32, CDEX_TYPE_D64,
    CDEX_TYPE_STR,
    CDEX_TYPE_UNKNOWN
} cdex_data_type_t;

// 用于存储任意类型值的联合体
typedef union {
    uint8_t u8;
    int8_t i8;
    uint16_t u16;
    int16_t i16;
    uint32_t u32;
    int32_t i32;
    uint64_t u64;
    int64_t i64;
    float f32;
    double d64;
    char* str; // 注意：解析时会动态分配内存，需手动释放
} cdex_value_t;

/**
 * @brief 单个字段的描述信息
 */
typedef struct {
    char name[CDEX_FIELD_NAME_LEN];
    cdex_data_type_t type;
    size_t size;
} cdex_field_t;

/**
 * @brief 完整的 CDEX 描述符信息
 */
typedef struct {
    uint16_t id;
    char* raw_string;
    int field_count;
    cdex_field_t fields[CDEX_MAX_FIELDS];
} cdex_descriptor_t;

/**
 * @brief CDEX 描述符链表的节点结构体。
 */
typedef struct cdex_descriptor_node {
    cdex_descriptor_t descriptor;
    struct cdex_descriptor_node* next;
} cdex_descriptor_node_t;

// CDEX 数据包的内存表示
typedef struct {
    uint16_t descriptor_id;
    uint64_t bitmap;
    int data_count;
    cdex_value_t values[CDEX_MAX_FIELDS]; // 按bitmap顺序存放数据
} cdex_packet_t;

// 错误码
typedef enum {
    CDEX_SUCCESS = 0,
    CDEX_ERROR_BUFFER_TOO_SMALL,
    CDEX_ERROR_INVALID_PACKET,
    CDEX_ERROR_BAD_CHECKSUM,
    CDEX_ERROR_DESCRIPTOR_NOT_FOUND,
    CDEX_ERROR_INVALID_DATA,
    CDEX_ERROR_MEMORY_ALLOCATION,
    CDEX_ERROR_INDEX_OUT_OF_BOUNDS,
    CDEX_ERROR_PACKET_FULL,
    CDEX_ERROR_ID_EXISTS
} cdex_status_t;


/**
 * @brief 初始化描述符管理器
 */
void cdex_manager_init(void);

/**
 * @brief 清理并释放所有已注册的描述符，防止内存泄漏
 */
void cdex_manager_cleanup(void);

/**
 * @brief 通过描述符字符串动态注册一个新的描述符
 * @param id 要注册的描述符ID
 * @param descriptor_string 描述符字符串，例如 "temp-f32,hum-u16"
 * @return 状态码 (CDEX_SUCCESS 表示成功)
 */
cdex_status_t cdex_descriptor_register(uint16_t id, const char* descriptor_string);

/**
 * @brief 通过预定义的字段数组加载一个新的描述符
 * @param id 要加载的描述符ID
 * @param fields 指向 cdex_field_t 数组的指针
 * @param field_count 数组中的字段数量
 * @return 状态码 (CDEX_SUCCESS 表示成功)
 */
cdex_status_t cdex_descriptor_load(uint16_t id, const cdex_field_t* fields, int field_count);

/**
 * @brief 根据ID查找一个已初始化的描述符
 * @param id 描述符ID
 * @return 成功则返回描述符指针，失败返回NULL
 */
const cdex_descriptor_t* cdex_get_descriptor_by_id(uint16_t id);

/**
 * @brief 将 cdex_packet_t 数据打包成 CDEX 字节流
 * @param packet 指向待打包的数据包结构体
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 成功则返回打包后的字节数，失败返回-1
 */
int cdex_pack(const cdex_packet_t* packet, uint8_t* buffer, size_t buffer_size);

/**
 * @brief 将 CDEX 字节流解析到 cdex_packet_t 结构体中
 * @param buffer 包含CDEX字节流的缓冲区
 * @param buffer_len 缓冲区中的数据长度
 * @param packet_out 指向用于存储解析结果的结构体指针
 * @return 状态码 (CDEX_SUCCESS 表示成功)
 */
cdex_status_t cdex_parse(const uint8_t* buffer, size_t buffer_len, cdex_packet_t* packet_out);

#ifdef CDEX_PARSE_TO_JSON
/**
 * @brief 将解析后的 cdex_packet_t 转换为 cJSON 对象
 * @param packet 指向已解析的数据包
 * @return 成功则返回 cJSON 对象根节点，失败返回 NULL。调用者需负责释放返回的cJSON对象。
 */
cJSON* cdex_packet_to_json(const cdex_packet_t* packet);
#endif

/**
 * @brief 初始化一个 CDEX 数据包结构体
 * @param packet 指向要初始化的数据包
 * @param descriptor_id 该数据包将使用的描述符ID
 */
void cdex_packet_init(cdex_packet_t* packet, uint16_t descriptor_id);

/**
 * @brief 向数据包中添加或更新一个字段
 * @param packet 指向目标数据包
 * @param field_index 要添加的字段在其描述符中的索引 (0-63)
 * @param value 要添加的数据值
 * @return 状态码 (CDEX_SUCCESS 表示成功)
 */
cdex_status_t cdex_packet_push(cdex_packet_t* packet, int field_index, cdex_value_t value);

/**
 * @brief 从数据包中移除一个字段
 * @param packet 指向目标数据包
 * @param field_index 要移除的字段在其描述符中的索引 (0-63)
 * @return 状态码 (CDEX_SUCCESS 表示成功)
 */
cdex_status_t cdex_packet_pop(cdex_packet_t* packet, int field_index);

/**
 * @brief 计算当前数据包打包后所需的字节数
 * @param packet 指向要计算的数据包
 * @return 成功则返回预估的字节数，失败返回-1
 */
int cdex_packet_calculate_packed_size(const cdex_packet_t* packet);

/**
 * @brief 释放由 cdex_parse 动态分配的字符串内存
 * @param packet 指向已解析的数据包
 */
void cdex_free_packet_strings(cdex_packet_t* packet);

#endif // CDEX_PROTOCOL_H
