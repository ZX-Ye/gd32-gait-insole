/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

/*!
    \file    main.c
    \brief   Main loop of GD32VW55x SDK.

    \version 2023-07-20, V1.0.0, firmware for GD32VW55x
*/

/*
    Copyright (c) 2023, GigaDevice Semiconductor Inc.

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#include <stdint.h>
#include "wifi_export.h"
#include "gd32vw55x_platform.h"
#include "uart.h"
#include "ble_init.h"
#include "gd32vw55x.h"
#include "wrapper_os.h"
#include "cmd_shell.h"
#include "atcmd.h"
#include "util.h"
#include "wlan_config.h"
#include "wifi_init.h"
#include "user_setting.h"
#include "version.h"
#include "_build_date.h"
#include "config_gdm32.h"
#ifdef CONFIG_FATFS_SUPPORT
#include "fatfs.h"
#endif
#include "ble_app_config.h"
#include "app_cmd.h"
#ifdef CONFIG_AZURE_IOT_SUPPORT
#include "azure_entry.h"
#endif

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "string.h"

#include <math.h>            // 用于 sin() 函数
#define PWM_PERIOD 1000      // 补充缺失的 PWM 周期宏定义

// 定义串口发送队列句柄
QueueHandle_t uart_tx_queue = NULL;

extern volatile uint8_t g_conn_idx_left;
extern volatile uint8_t g_conn_idx_right;

// 定义一帧数据的结构体（最大支持128字节）
typedef struct {
    uint16_t len;
    uint8_t  data[256];
} UartTxMsg_t;

/*!
    \brief      Init applications.
                This function is called to initialize all the applications.
    \param[in]  none.
    \param[out] none.
    \retval     none.
*/
static void application_init(void)
{
#if defined CONFIG_BASECMD || defined CONFIG_RF_TEST_SUPPORT || defined CONFIG_BLE_DTM_SUPPORT
    if (cmd_shell_init()) {
        dbg_print(ERR, "cmd shell init failed\r\n");
    }
#endif
//#ifdef CONFIG_ATCMD
//    if (atcmd_init()) {
//        dbg_print(ERR, "atcmd init failed\r\n");
//    }
//#endif
    util_init();

    user_setting_init();

#ifdef CFG_BLE_SUPPORT
#ifdef CONFIG_BLE_ALWAYS_ENABLE
    ble_init(true);
#else
    ble_init(false);
#endif  // CONFIG_BLE_DEFAULT_INIT
#endif  // CFG_BLE_SUPPORT

#ifdef CFG_WLAN_SUPPORT
    if (wifi_init()) {
        dbg_print(ERR, "wifi init failed\r\n");
    }
#endif
#ifdef CONFIG_FATFS_SUPPORT
#ifndef CONFIG_ATCMD
    fatfs_mk_mount(NULL);
#endif
#endif
#ifdef CFG_MATTER
    MatterInit();
#endif

#ifdef CONFIG_AZURE_F527_DEMO_SUPPORT
    azure_task_start();
#endif
}

#ifdef PLATFORM_OS_RTTHREAD
/*!
    \brief      Start task.
                This function is called to initialize all the applications in thread context.
    \param[in]  param parameter passed to the task
    \param[out] none
    \retval     none.
*/
static void start_task(void *param)
{
    (void)param;

    application_init();

    sys_task_delete(NULL);
}
#endif

/*!
    \brief      Main entry point.
                This function is called right after the booting process has completed.
    \param[in]  none
    \param[out] none
    \retval     none.
*/
void user_trans_uart_init(void);
void user_lighting_init(void); // 👈 【新增这一行】：提前声明，防止编译器找不到
int main(void)
{
    sys_os_init();
    platform_init();

    dbg_print(NOTICE, "SDK Version: %s\n", WIFI_GIT_REVISION);
    dbg_print(NOTICE, "Build date: %s\n", SDK_BUILD_DATE);
    dbg_print(NOTICE, "Image Version: %s%x.%x.%x.%03x\n",
            RE_CUSTOMER_NAME,
            (RE_IMG_VERSION >> 28),
            (RE_IMG_VERSION >> 20) & 0xFF,
            (RE_IMG_VERSION >> 12) & 0xFF,
            RE_IMG_VERSION & 0xFFF);


#ifdef PLATFORM_OS_RTTHREAD
    if (sys_task_create_dynamic((const uint8_t *)"start_task",
            START_TASK_STACK_SIZE, OS_TASK_PRIORITY(START_TASK_PRIO), start_task, NULL) == NULL) {
        dbg_print(ERR, "Create start task failed\r\n");
    }
#else
    user_trans_uart_init();
    application_init();
    user_lighting_init();  // 👈 【新增这一行】：正式点燃赛博呼吸引擎！
#endif

    sys_os_start();
}



#include "gd32vw55x_gpio.h"
#include "gd32vw55x_usart.h"
#include "gd32vw55x_rcu.h"




#include <stdlib.h>
#include <string.h>

// ==========================================================
// 📦 双路汇聚二进制协议 (修正负数 Bug，恒定 94 字节)
// ==========================================================
#pragma pack(1)
typedef struct {
    uint8_t  header;
    uint32_t timestamp;
    int16_t  left_adc[16];  // 🚨 改为 int16_t，完美容纳 -4096
    int16_t  left_imu[6];
    int16_t  right_adc[16]; // 🚨 改为 int16_t
    int16_t  right_imu[6];
    uint8_t  tail;
} CombinedDataPacket;
#pragma pack()

CombinedDataPacket g_sync_packet; // 全局仪表盘（快照池）

// 提前声明两个任务
static void ble_parse_task(void *param);
static void uart_tx_40hz_task(void *param);

// ==========================================================
// 🚀 新版队列投递函数 (代替回调进行封口组装)
// ==========================================================
// 1. 修改 user_trans_uart_send，增加丢包警告
void user_trans_uart_send(uint8_t source_tag, uint8_t *data, uint16_t len)
{
    if (uart_tx_queue == NULL || len > 250) return;

    UartTxMsg_t msg;
    msg.len = len + 1;
    msg.data[0] = source_tag;
    memcpy(&msg.data[1], data, len);
    msg.data[len + 1] = '\0';

    // 🚨 如果队列满了，打印警告而不是静默丢弃！
    if (xQueueSend(uart_tx_queue, &msg, 0) != pdTRUE) {
        dbg_print(WARNING, "⚠️ [WARN] 解析队列已满！%c 脚数据被丢弃！\r\n", source_tag);
    }
}

// 初始化透传串口与多任务
void user_trans_uart_init(void) {
    // ... (你的 GPIO 和 UART1 初始化代码保持不变) ...
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_UART1);
    gpio_af_set(GPIOB, GPIO_AF_7, GPIO_PIN_15);
    gpio_af_set(GPIOA, GPIO_AF_7, GPIO_PIN_8);
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_15);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_15);
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_8);

    usart_deinit(UART1);
    usart_baudrate_set(UART1, 1500000U);
    usart_word_length_set(UART1, USART_WL_8BIT);
    usart_stop_bit_set(UART1, USART_STB_1BIT);
    usart_parity_config(UART1, USART_PM_NONE);
    usart_receive_config(UART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(UART1, USART_TRANSMIT_ENABLE);
    usart_enable(UART1);

    // 🚨 队列长度从 10 扩大到 30
        uart_tx_queue = xQueueCreate(30, sizeof(UartTxMsg_t));

        // 🚨 将 ble_parse 的优先级提升到 3（保证 CPU 一有空就赶紧解包）
        sys_task_create_dynamic((const uint8_t *)"ble_parse", 1024, OS_TASK_PRIORITY(3), ble_parse_task, NULL);

        // 🚨 节拍器优先级保持 2 即可
        sys_task_create_dynamic((const uint8_t *)"uart_tx", 512, OS_TASK_PRIORITY(2), uart_tx_40hz_task, NULL);
}

// ==========================================================
// 🧠 任务 1：剥洋葱解析任务 (只管拼命解包，更新仪表盘)
// ==========================================================
static void ble_parse_task(void *param)
{
    UartTxMsg_t msg;
    while(1) {
        if (xQueueReceive(uart_tx_queue, &msg, portMAX_DELAY) == pdTRUE) {

            char foot_tag = msg.data[0];
            char *pa = strstr((char *)&msg.data[1], "a:");
            char *pm = strstr((char *)&msg.data[1], ";m:");

            // 解析并直接更新到全局快照中
            if (foot_tag == 'L') {
                if(pa) { pa += 2; for(int i=0; i<16; i++) { g_sync_packet.left_adc[i]  = (int16_t)strtol(pa, &pa, 10); if(*pa == ',' || *pa == ';') pa++; } }
                if(pm) { pm += 3; for(int i=0; i<6; i++)  { g_sync_packet.left_imu[i]  = (int16_t)strtol(pm, &pm, 10); if(*pm == ',' || *pm == '|') pm++;  } }
            }
            else if (foot_tag == 'R') {
                if(pa) { pa += 2; for(int i=0; i<16; i++) { g_sync_packet.right_adc[i] = (int16_t)strtol(pa, &pa, 10); if(*pa == ',' || *pa == ';') pa++; } }
                if(pm) { pm += 3; for(int i=0; i<6; i++)  { g_sync_packet.right_imu[i] = (int16_t)strtol(pm, &pm, 10); if(*pm == ',' || *pm == '|') pm++;  } }
            }
        }
    }
}

// ==========================================================
// ⏱️ 任务 2：绝对均匀节拍器 (40Hz 无情发送机器)
// ==========================================================
// ==========================================================
// ⏱️ 任务 2：绝对均匀节拍器 (40Hz 无情发送机器)
// ==========================================================
volatile uint8_t g_is_ble_connected = 0; // 🚨 删掉 extern，加上 = 0

static void uart_tx_40hz_task(void *param)
{
    // 初始化固定头尾
    g_sync_packet.header = 0xAA;
    g_sync_packet.tail   = 0x55;

    while(1) {
        // 绝对精准延时：每 25 毫秒醒来一次 (1000ms / 25ms = 40Hz)
        sys_ms_sleep(25);

        // 只要蓝牙连着，就抓取当前仪表盘的最新数据发走
        if (g_is_ble_connected) {

            g_sync_packet.timestamp = xTaskGetTickCount();

            // 🚨 调试代码：确认结构体已经凑齐发送了
            dbg_print(NOTICE, "DEBUG: Sending Packet! LeftConnected:%d, RightConnected:%d\r\n",
                     (g_conn_idx_left != 0xFF), (g_conn_idx_right != 0xFF));
            uint8_t *p_send = (uint8_t *)&g_sync_packet;

            for (uint16_t i = 0; i < sizeof(CombinedDataPacket); i++) {
                usart_data_transmit(UART1, p_send[i]);
                while(RESET == usart_flag_get(UART1, USART_FLAG_TBE));
            }

            // 🔥 在节拍器里翻转极光指示灯。
            // 只要灯保持绝对均匀闪烁，就证明 40Hz 恒定输出正在完美运行！
            gpio_bit_toggle(GPIOA, GPIO_PIN_5);
        }
    }
}

// ==========================================================
// 💡 [硬件层]：PA3/PA4 呼吸肺 (PWM)，PA5 数据突触 (纯GPIO)
// ==========================================================
void user_led_init(void)
{
    // 1. 开启时钟
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_TIMER1);

    // 2. 引脚模式配置
    // PA3: 复用模式 (PWM)
    // PA4: 普通输出 (闪烁)
    // PA5: 普通输出 (数据脉冲)
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_3);
    gpio_af_set(GPIOA, GPIO_AF_1, GPIO_PIN_3);

    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_4 | GPIO_PIN_5);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5);

    // 初始化电平为高 (熄灭)
    gpio_bit_set(GPIOA, GPIO_PIN_4 | GPIO_PIN_5);

    // 3. 定时器配置 (针对 PA3)
    timer_parameter_struct timer_initpara;
    timer_struct_para_init(&timer_initpara);
    timer_initpara.prescaler         = (SystemCoreClock / 1000000) - 1;
    timer_initpara.period            = PWM_PERIOD - 1;
    timer_init(TIMER1, &timer_initpara);

    timer_oc_parameter_struct timer_ocintpara;
    timer_channel_output_struct_para_init(&timer_ocintpara);
    timer_ocintpara.outputstate  = TIMER_CCX_ENABLE;
    timer_ocintpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;

    timer_channel_output_config(TIMER1, TIMER_CH_3, &timer_ocintpara);
    timer_channel_output_mode_config(TIMER1, TIMER_CH_3, TIMER_OC_MODE_PWM0);
    timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_3, PWM_PERIOD);

    timer_primary_output_config(TIMER1, ENABLE); // 关键！开启主输出
    timer_enable(TIMER1);
}
// 调节 PA3 或 PA4 亮度 (0.0~1.0)
void set_lung_brightness(uint8_t pin, float brightness) {
    if (brightness > 1.0f) brightness = 1.0f;
    if (brightness < 0.0f) brightness = 0.0f;
    uint32_t pulse = (uint32_t)(PWM_PERIOD * (1.0f - brightness)); // 低电平亮

    if (pin == 3) timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_3, pulse); // 控制左肺

}

// ==========================================================
// 🧠 [逻辑层]：有机呼吸引擎 (FreeRTOS 任务)
// ==========================================================
static void led_organic_task(void *param)
{
    float time_t = 0.0f;
    uint32_t counter = 0;

    while(1) {
        // --- A. PA3 呼吸逻辑 (呼吸频率随连接状态变化) ---
        float brightness;
        if (!g_is_ble_connected) {
            brightness = (sin(time_t) + 1.0f) / 2.0f * 0.8f;
            time_t += 0.05f;
        } else {
            brightness = (sin(time_t * 0.5f) + 1.0f) / 2.0f * 0.15f;
            time_t += 0.02f;
        }
        // 更新呼吸灯 PWM
        uint32_t pulse = (uint32_t)(PWM_PERIOD * (1.0f - brightness));
        timer_channel_output_pulse_value_config(TIMER1, TIMER_CH_3, pulse);

        // --- B. PA4 独立慢闪逻辑 (不依赖定时器，直接翻转 GPIO) ---
        // 20ms 循环一次，每 10 次翻转一次 = 200ms 翻转频率
        if (counter % 10 == 0) {
            gpio_bit_toggle(GPIOA, GPIO_PIN_4);
        }

        counter++;
        sys_ms_sleep(20);
    }
}

// 总初始化入口
void user_lighting_init(void) {
    user_led_init();
    sys_task_create_dynamic((const uint8_t *)"led_task", 512, OS_TASK_PRIORITY(2), led_organic_task, NULL);
}

