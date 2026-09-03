/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#include "gd32f4xx.h"
#include "systick.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "main.h"
#include "bsp_led.h"
#include "bsp_usart.h"
#include "exmc_sdram.h"
#include "lcd.h"
#include "lcd_ui.h"
#include "lcd_mytest.h"

#include "lv_conf.h"
#include "lvgl.h"
#include "lv_port_disp_template.h"

#include "FreeRTOS.h"
#include "task.h"



/* 全局 UI 变量，屏幕刷新强依赖这两个数组 */
volatile uint16_t g_left_adc[16] = {0}; 
volatile int16_t  g_left_imu[6]  = {0};

/* ================== 新增全局变量 ================== */
volatile uint32_t g_h7_frame_count = 0;
volatile uint8_t  g_ai_result = 0xFF; // 🚨 新增：保存 H737 传回来的 AI 分类结果

/* ================== 新增修复代码 ================== */
/* 补充保留的"右脚"哑变量，专门用来糊弄 lcd_mytest.c，防止编译报错 L6218E */
volatile uint16_t received_adc_values[16] = {0};
volatile int16_t  g_wireless_imu[6]  = {0};
volatile uint16_t g_wireless_adc[16] = {0}; // 稳妥起见，把这个也加上
extern volatile uint16_t ready_rx_len;
// ==========================================
// 🚀 新增：持续滚动式病理步态评估引擎
// ==========================================
volatile uint8_t  g_abnormal_ratio = 0xFF;      // 异常占比 (0xFF代表机器刚开机，还在等第一个1分钟)
volatile uint16_t g_1min_frame_counter = 0;     // 一分钟进度累加器 (0~1800)
volatile uint16_t g_1min_abnormal_counter = 0;  // 一分钟内的高危病态帧数a

/* ==========================================================
   📦 双路汇聚二进制协议 (与 VW553 绝对保持一致)
   ========================================================== */
#pragma pack(1)  // 强制 1 字节对齐
typedef struct {
    uint8_t  header;          // 帧头 0xAA (1 byte)
    uint32_t timestamp;       // 统一时间戳 (4 bytes)
    uint16_t left_adc[16];    // 左脚 16 路压力 (32 bytes)
    int16_t  left_imu[6];     // 左脚 6 轴 IMU (12 bytes)
    uint16_t right_adc[16];   // 右脚 16 路压力 (32 bytes)
    int16_t  right_imu[6];    // 右脚 6 轴 IMU (12 bytes)
    uint8_t  tail;            // 帧尾 0x55 (1 byte)
} CombinedDataPacket;
#pragma pack()
/* ========================================================== */

/* IPA 初始化声明 */
extern void lv_gpu_gd32_ipa_init(void);

#define TASK_UI_PRIORITY         4   
#define TASK_PARSE_PRIORITY      5

TaskHandle_t start_task_handle;
TaskHandle_t task1_handle; // UI 刷新任务
TaskHandle_t task2_handle; // 数据解析任务

void start_task(void *pvParameters);
void task1(void *pvParameters);
void task2(void *pvParameters);

int main(void)
{
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);
    systick_config();
    led_gpio_config();
    
    // 初始化接收 VW553 数据的串口
    usart_gpio_config(1500000); 
    
    // 🚨 新增：统一初始化两个 1.5Mbps 转发串口 (PC & H737)
    usart_forward_config(1500000);     
    
    printf("\r\nSystem Booting...\r\n");

    xTaskCreate(start_task, "start_task", 4096, NULL, 3, &start_task_handle);
    vTaskStartScheduler();
    while (1) {}
}

void start_task(void *pvParameters)
{
    (void)pvParameters;
    
    exmc_synchronous_dynamic_ram_init(EXMC_SDRAM_DEVICE0);
    lv_init();
    lv_gpu_gd32_ipa_init();
    lv_port_disp_init();
    
    xTaskCreate(task1, "task_ui", 2048, NULL, TASK_UI_PRIORITY, &task1_handle);
    xTaskCreate(task2, "task_parse", 2048, NULL, TASK_PARSE_PRIORITY, &task2_handle);

    vTaskDelete(NULL);
}

/* ===== 任务 1：LVGL 屏幕刷新任务 ===== */
void task1(void *pvParameters)
{
    (void)pvParameters;
    
    // 🚨 核心修改 1：调用全新的 AI 热力图仪表盘创建函数
    create_dashboard_ui(); 
    
    uint32_t last_ui_update = 0;
    while (1)
    {
        // 限制 UI 刷新率 (约 30FPS)
        if ((xTaskGetTickCount() - last_ui_update) > 33)
        {
            // 🚨 核心修改 2：调用全新的统一刷新函数 (更新大字报、热力点和重心)
            update_dashboard_ui();
            
            last_ui_update = xTaskGetTickCount();
        }
        
        lv_tick_inc(5); 
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

/* ===== 任务 2：数据解析与 30Hz 绝对均匀双发任务 ===== */
void task2(void *pvParameters)
{
    (void)pvParameters;
    const uint16_t PACKET_SIZE = sizeof(CombinedDataPacket); // 94字节

    static CombinedDataPacket latest_packet = {0};
    latest_packet.header = 0xAA;
    latest_packet.tail = 0x55;

    // 🚨 核心新增：蓝牙生命周期锁 (默认上锁)
    static uint8_t bluetooth_is_alive = 0; 

    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(33); 

    while (1)
    {
        // ==========================================
        // 阶段一：无情解析 
        // ==========================================
        if(ready_buffer_id != 0)
        {
            uint8_t *p_raw = (uint8_t *)((ready_buffer_id == 1) ? rx_buffer_A : rx_buffer_B);
            uint16_t current_len = ready_rx_len; 
            ready_buffer_id = 0; 

            for (uint16_t i = 0; i + PACKET_SIZE <= current_len; i++) 
            {
                if (p_raw[i] == 0xAA && p_raw[i + PACKET_SIZE - 1] == 0x55) 
                {
                    CombinedDataPacket *p_data = (CombinedDataPacket *)&p_raw[i];

                    // 🎯 收到真数据！解锁节拍器！
                    bluetooth_is_alive = 1;

                    memcpy(&latest_packet, p_data, PACKET_SIZE);

                    // 🎯 修复左右脚反转
                    memcpy((void*)g_left_adc, p_data->right_adc, sizeof(g_left_adc));
                    memcpy((void*)g_left_imu, p_data->right_imu, sizeof(g_left_imu));
                    memcpy((void*)received_adc_values, p_data->left_adc, sizeof(received_adc_values));

                    i += PACKET_SIZE - 1;
                }
            }
        }

        // ==========================================
        // 阶段二：绝对均匀重采样与转发 (30Hz 节拍器)
        // ==========================================
        if ((xTaskGetTickCount() - xLastWakeTime) >= xFrequency)
        {
            xLastWakeTime = xTaskGetTickCount(); 

            if (bluetooth_is_alive) 
            {
                latest_packet.timestamp = xTaskGetTickCount();
                usart_forward_send_packet((uint8_t *)&latest_packet, PACKET_SIZE);

                // ==========================================
                // 📊 核心计算：无限循环的 1 分钟风险评估
                // ==========================================
                if (g_ai_result <= 7) {
                    g_1min_frame_counter++;

                    // 记录高危动作：0(痛性跛行), 1(盲态探步), 3(偏瘫)
                    if (g_ai_result == 0 || g_ai_result == 1 || g_ai_result == 3) {
                        g_1min_abnormal_counter++;
                    }

                    // 满 1 分钟！(30Hz * 60秒 = 1800帧)
                    if (g_1min_frame_counter >= 1800) {
                        // 结算这 1 分钟的最终得分，交给屏幕去展示
                        g_abnormal_ratio = (g_1min_abnormal_counter * 100) / 1800;
                        if (g_abnormal_ratio > 100) g_abnormal_ratio = 100;
                        
                        // 🎯 核心：清零重新开始，实现永不停歇的滚动评估！
                        g_1min_frame_counter = 0;
                        g_1min_abnormal_counter = 0;
                    }
                }
            }
        }

        // 阶段三：让出 CPU 1ms
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
