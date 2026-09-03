/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#include <stdint.h>
#include <string.h>
#include "wifi_export.h"
#include "gd32vw55x_platform.h"
#include "gd32vw55x_usart.h"
#include "gd32vw55x_dma.h"
#include "gd32vw55x_rcu.h"
#include "gd32vw55x_gpio.h"
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
#include "ble_datatrans_srv.h"
#include "app_dev_mgr.h"
#include "app_adv_mgr.h"
#include "app_conn_mgr.h"
#include "ble_conn.h"

#define RING_BUF_SIZE 4096
static uint8_t ring_buf[RING_BUF_SIZE];
static volatile uint16_t head = 0;
static volatile uint16_t tail = 0;

static uint8_t g_conn_idx = 0;
static volatile bool g_connected = false;
static volatile bool g_mtu_ready = false;     // ★ MTU 协商完成后才能发大数据
static volatile uint16_t g_mtu = 23;          // ★ 默认 MTU

// --- 连接回调 ---
static void my_conn_evt_handler(ble_conn_evt_t evt, union ble_conn_data *p_data) {
    if (evt == BLE_CONN_EVT_STATE_CHG) {
        ble_conn_state_chg_t *p_chg = (ble_conn_state_chg_t *)p_data;
        if (p_chg->state == BLE_CONN_STATE_CONNECTED) {
            g_conn_idx = p_chg->info.conn_info.conn_idx;
            g_connected = true;
            g_mtu_ready = false;
            g_mtu = 23;
            dbg_print(NOTICE, ">>> BLE Connected! <<<\r\n");
        } else if (p_chg->state == BLE_CONN_STATE_DISCONNECTD) {
            g_connected = false;
            g_mtu_ready = false;
            dbg_print(NOTICE, ">>> BLE Disconnected! <<<\r\n");
        }
    }
}

// --- USART0 + DMA 初始化 ---
void uart_dma_init(void)
{
    volatile uint32_t dummy;

    dbg_print(NOTICE, "USART0 DMA init...\r\n");

    rcu_periph_clock_enable(RCU_USART0);
    rcu_periph_clock_enable(RCU_DMA);
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);

    // PA8 AF2 = USART0_RX
    gpio_mode_set(GPIOA, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_8);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_8);
    gpio_af_set(GPIOA, GPIO_AF_2, GPIO_PIN_8);

    // PB15 AF7 = USART0_TX
    gpio_mode_set(GPIOB, GPIO_MODE_AF, GPIO_PUPD_NONE, GPIO_PIN_15);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_MAX, GPIO_PIN_15);
    gpio_af_set(GPIOB, GPIO_AF_7, GPIO_PIN_15);

    // 关闭 SDK 的 USART0 中断
    usart_disable(USART0);
    ECLIC_DisableIRQ(USART0_IRQn);

    usart_baudrate_set(USART0, 1500000U);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    USART_CTL0(USART0) &= ~((uint32_t)0x1F << 5);
    usart_enable(USART0);

    // 清标志
    while (usart_flag_get(USART0, USART_FLAG_RBNE)) { dummy = USART_RDATA(USART0); }
    usart_flag_clear(USART0, USART_FLAG_ORERR);
    usart_flag_clear(USART0, USART_FLAG_FERR);

    // DMA 配置
    dma_single_data_parameter_struct dma_init_struct;
    dma_deinit(DMA_CH4);

    dma_init_struct.periph_addr         = (uint32_t)&USART_RDATA(USART0);
    dma_init_struct.memory0_addr        = (uint32_t)ring_buf;
    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.number              = RING_BUF_SIZE;
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.circular_mode       = DMA_CIRCULAR_MODE_ENABLE;
    dma_init_struct.priority            = DMA_PRIORITY_HIGH;

    dma_single_data_mode_init(DMA_CH4, &dma_init_struct);
    dma_channel_subperipheral_select(DMA_CH4, DMA_SUBPERI4);

    usart_dma_receive_config(USART0, USART_RECEIVE_DMA_ENABLE);
    dma_channel_enable(DMA_CH4);

    usart_flag_clear(USART0, USART_FLAG_ORERR);
    usart_flag_clear(USART0, USART_FLAG_RBNE);

    dbg_print(NOTICE, "USART0 DMA OK!\r\n");
}

// --- 广播任务 ---
static void ble_adv_task(void *param) {
    (void)param;
    ble_wait_ready();

    app_adv_stop(0, true);
    app_adv_param_t adv_param;
    memset(&adv_param, 0, sizeof(app_adv_param_t));
    adv_param.type = 0;
    adv_param.prop = 0x0003;
    adv_param.own_addr_type = 0;
    adv_param.disc_mode = 2;
    adv_param.adv_intv = 160;
    adv_param.ch_map = 0x07;
    adv_param.pri_phy = 1;
    adv_param.sec_phy = 1;
    adv_param.max_data_len = 0x1F;

    static uint8_t adv_data[] = {
        0x02, 0x01, 0x06,
        0x0E, 0x09, 'V', 'W', '5', '5', '3', '_', 'G', 'a', 't', 'e', 'w', 'a', 'y'
    };
    app_adv_set_adv_data(adv_data, sizeof(adv_data));
    app_adv_create(&adv_param);
    dbg_print(NOTICE, "BLE Adv Created!\r\n");
    while(1) { sys_ms_sleep(5000); }
}

// --- 透传任务 ---
// --- 透传任务 ---
static void ble_transmit_task(void *param) {
    (void)param;
    uint8_t tx_buf[244];
    static uint32_t send_count = 0;
    static uint32_t diag_tick = 0;
    static uint32_t connect_tick = 0;

    while(1) {
        // ★ 连接后等 3 秒让 MTU 协商完成，不用调 API
        if (g_connected && !g_mtu_ready) {
            connect_tick++;
            if (connect_tick > 3000) {  // 3000 x 1ms = 3秒
                g_mtu_ready = true;
                g_mtu = 509;  // 手机端协商后通常是 512，有效载荷 = 512-3 = 509
                dbg_print(NOTICE, "MTU ready: %d\r\n", g_mtu);
            }
        }
        if (!g_connected) {
            connect_tick = 0;
            g_mtu_ready = false;
        }

        diag_tick++;

        // 诊断：每 5000 次打印一次
        if (diag_tick % 5000 == 0) {
            uint32_t dma_cnt = dma_transfer_number_get(DMA_CH4);
            uint16_t cur_tail = RING_BUF_SIZE - dma_cnt;
            uint16_t avail = (cur_tail >= head) ? (cur_tail - head) : (RING_BUF_SIZE - head + cur_tail);
            dbg_print(NOTICE, "D:mtu=%d rdy=%d avl=%d h=%d t=%d\r\n",
                      g_mtu, g_mtu_ready, avail, head, cur_tail);
        }

        if (g_connected && g_mtu_ready) {
            tail = RING_BUF_SIZE - dma_transfer_number_get(DMA_CH4);

            uint16_t len = 0;
            if (tail >= head) len = tail - head;
            else len = RING_BUF_SIZE - head + tail;

            if (len >= 1) {
                uint16_t max_payload = g_mtu - 3;
                if (max_payload > 244) max_payload = 244;
                if (max_payload < 20) max_payload = 20;

                uint16_t tx_len = len;
                if (tx_len > max_payload) tx_len = max_payload;

                uint16_t temp_head = head;
                for (uint16_t i = 0; i < tx_len; i++) {
                    tx_buf[i] = ring_buf[temp_head];
                    temp_head = (temp_head + 1) % RING_BUF_SIZE;
                }

                ble_status_t ret = ble_datatrans_srv_tx(g_conn_idx, tx_buf, tx_len);
                if (ret == BLE_ERR_NO_ERROR) {
                    head = temp_head;
                    send_count += tx_len;
                    if (send_count <= tx_len) {
                        dbg_print(NOTICE, "1st TX! %dB [0]=%02X\r\n", tx_len, tx_buf[0]);
                    }
                } else if (ret == 13) {
                    // MTU 不够，降级到 20 字节重试
                    g_mtu = 23;
                    sys_ms_sleep(10);
                } else {
                    sys_ms_sleep(5);
                }
            }
        }
        sys_ms_sleep(1);
    }
}

// --- 初始化 ---
static void application_init(void)
{
    util_init();
    user_setting_init();
    ble_init(true);
    app_dm_init();
    app_adv_mgr_init();
    ble_datatrans_srv_init();

    ble_conn_callback_register(my_conn_evt_handler);

    uart_dma_init();

    sys_task_create(NULL, (const uint8_t *)"ble_adv", NULL, 1024, 0, 0, 2, ble_adv_task, NULL);
    sys_task_create(NULL, (const uint8_t *)"ble_tx", NULL, 1024, 0, 0, 2, ble_transmit_task, NULL);
}

int main(void)
{
    sys_os_init();
    platform_init();
    dbg_print(NOTICE, "\r\n--- GD32VW553 Boot ---\r\n");
    application_init();
    sys_os_start();
    while(1);
}
