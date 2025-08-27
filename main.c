#include <stdio.h>
#include <stdlib.h>
#include "cdex.h"

void print_hex(const char* title, const uint8_t* data, int len) {
    printf("%s (%d bytes): ", title, len);
    for (int i = 0; i < len; ++i) {
        printf("%02X ", data[i]);
    }
    printf("\n");
}

void print_packet_details(const cdex_packet_t* packet) {
    printf("Packet Details:\n");
    printf("  Descriptor ID: %u\n", packet->descriptor_id);
    printf("  Bitmap: 0x%llx\n", (unsigned long long)packet->bitmap);
    printf("  Data Count: %d\n", packet->data_count);
}

int main() {
    printf("CDEX Protocol Dynamic Descriptor Demo\n======================================\n\n");

    // --- 1. 动态注册描述符 ---
    printf("--- 1. Registering Descriptors ---\n");

    // 方法 A: 通过字符串注册
    const char* sensor_desc_str = "temp:f32,humidity:u16,pressure:u32,status:u8,device_name:str";
    cdex_status_t reg_status = cdex_descriptor_register(1001, sensor_desc_str);
    if (reg_status == CDEX_SUCCESS) {
        printf("Successfully registered descriptor ID 1001 from string.\n");
    } else {
        printf("Failed to register descriptor 1001. Error: %d\n", reg_status);
        return -1;
    }

    // 方法 B: 通过预定义结构体数组加载
    cdex_field_t power_fields[] = {
        {"voltage", CDEX_TYPE_I16, 2},
        {"current", CDEX_TYPE_I16, 2},
        {"power", CDEX_TYPE_F32, 4},
        {"error_code", CDEX_TYPE_U32, 4},
        {"uptime", CDEX_TYPE_U64, 8}
    };
    int power_field_count = sizeof(power_fields) / sizeof(power_fields[0]);
    cdex_status_t load_status = cdex_descriptor_load(2005, power_fields, power_field_count);
    if (load_status == CDEX_SUCCESS) {
        printf("Successfully loaded descriptor ID 2005 from struct array.\n");
    } else {
        printf("Failed to load descriptor 2005. Error: %d\n", load_status);
        cdex_manager_cleanup(); // 清理已注册的
        return -1;
    }

    // --- 2. 使用新注册的描述符进行打包 ---
    printf("\n--- 2. Packing with Registered Descriptor ---\n");
    cdex_packet_t packet_to_pack;
    cdex_value_t val;

    cdex_packet_init(&packet_to_pack, 1001); // 使用我们刚注册的ID

    // Push 字段 #0 (temp) 和 #4 (device_name)
    val.f32 = 16.125f;
    cdex_packet_push(&packet_to_pack, 0, val);
    val.str = "Sensor_A";
    cdex_packet_push(&packet_to_pack, 4, val);
    
    print_packet_details(&packet_to_pack);

    // 打包
    uint8_t buffer[128];
    int packed_len = cdex_pack(&packet_to_pack, buffer, sizeof(buffer));
    if (packed_len > 0) {
        print_hex("Packed CDE Data", buffer, packed_len);
    } else {
        printf("Packing failed!\n");
        cdex_manager_cleanup();
        return -1;
    }

    // --- 3. 解析和验证 ---
    printf("\n--- 3. Parsing and Verification ---\n");
    cdex_packet_t parsed_packet;
    cdex_status_t parse_status = cdex_parse(buffer, packed_len, &parsed_packet);
    if (parse_status == CDEX_SUCCESS) {
        cJSON* json_obj = cdex_packet_to_json(&parsed_packet);
        char* json_string = cJSON_Print(json_obj);
        printf("Parsed and converted to JSON:\n%s\n", json_string);
        
        free(json_string);
        cJSON_Delete(json_obj);
        cdex_free_packet_memory(&parsed_packet);
    } else {
        printf("Parsing failed with code %d\n", parse_status);
    }

    // --- 4. 清理 ---
    printf("\n--- 4. Cleaning Up ---\n");
    cdex_manager_cleanup();
    printf("Descriptor manager cleaned up.\n");
    
    printf("\nDemo finished successfully.\n");
    return 0;
}
