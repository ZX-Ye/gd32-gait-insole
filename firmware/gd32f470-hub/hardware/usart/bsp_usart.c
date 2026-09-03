/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#include "bsp_usart.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"




char rx_buffer_A[UART_RX_BUFFER_SIZE];
char rx_buffer_B[UART_RX_BUFFER_SIZE];
volatile uint8_t ready_buffer_id = 0; 
volatile uint16_t ready_rx_len = 0; // 🚨 新增：记录这块缓存到底收了多少字节！
char *p_write_buf = rx_buffer_A;
// 🚨 write_index 变量被废弃了，因为现在由 DMA 硬件自动计算写入长度

/* 🚨 新增：DMA 接收引擎初始化函数 */
void usart_dma_rx_config(void)
{
    dma_single_data_parameter_struct dma_init_struct;

    rcu_periph_clock_enable(RX_DMA_RCU);
    dma_deinit(RX_DMAx, RX_DMA_CHy);

    dma_init_struct.direction           = DMA_PERIPH_TO_MEMORY;
    dma_init_struct.memory0_addr        = (uint32_t)rx_buffer_A; // 初始指向 A 区
    dma_init_struct.memory_inc          = DMA_MEMORY_INCREASE_ENABLE;
    dma_init_struct.periph_memory_width = DMA_PERIPH_WIDTH_8BIT;
    dma_init_struct.number              = UART_RX_BUFFER_SIZE;
    dma_init_struct.periph_addr         = (uint32_t)&USART_DATA(BSP_USART); // UART4 数据寄存器
    dma_init_struct.periph_inc          = DMA_PERIPH_INCREASE_DISABLE;
    dma_init_struct.priority            = DMA_PRIORITY_ULTRA_HIGH; // 最高优先级
    dma_single_data_mode_init(RX_DMAx, RX_DMA_CHy, &dma_init_struct);

    dma_channel_subperipheral_select(RX_DMAx, RX_DMA_CHy, RX_DMA_SUB);
    
    // 使能 DMA 通道，并开启串口的 DMA 接收请求
    dma_channel_enable(RX_DMAx, RX_DMA_CHy);
    usart_dma_receive_config(BSP_USART, USART_DENR_ENABLE);
}

void usart_gpio_config(uint32_t band_rate)
{
    /* 开启时钟 */
    rcu_periph_clock_enable(BSP_USART_TX_RCU);
    rcu_periph_clock_enable(BSP_USART_RX_RCU);
    rcu_periph_clock_enable(BSP_USART_RCU);
    
    /* 配置引脚复用功能 */
    gpio_af_set(BSP_USART_TX_PORT, BSP_USART_AF, BSP_USART_TX_PIN);    
    gpio_af_set(BSP_USART_RX_PORT, BSP_USART_AF, BSP_USART_RX_PIN);    
    
    /* 配置GPIO模式 */
    gpio_mode_set(BSP_USART_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_USART_TX_PIN);
    gpio_mode_set(BSP_USART_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_USART_RX_PIN);
    
    gpio_output_options_set(BSP_USART_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_USART_TX_PIN);
    gpio_output_options_set(BSP_USART_RX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_USART_RX_PIN);

    /* 串口参数配置 */
    usart_deinit(BSP_USART);
    usart_baudrate_set(BSP_USART, band_rate);
    usart_parity_config(BSP_USART, USART_PM_NONE);
    usart_word_length_set(BSP_USART, USART_WL_8BIT);
    usart_stop_bit_set(BSP_USART, USART_STB_1BIT);

    /* 使能串口及收发 */
    usart_transmit_config(BSP_USART, USART_TRANSMIT_ENABLE);
    usart_receive_config(BSP_USART, USART_RECEIVE_ENABLE);
    
    /* 🚨 核心修改：只开启空闲中断，绝不开接收中断 */
    nvic_irq_enable(BSP_USART_IRQ, 3, 0);
    usart_interrupt_enable(BSP_USART, USART_INT_IDLE); // 仅 IDLE

    usart_enable(BSP_USART);
    
    /* 🚨 启动 DMA 引擎 */
    usart_dma_rx_config();
}

void usart_send_data(uint8_t ucch)
{
    usart_data_transmit(BSP_USART, ucch);
    while(RESET == usart_flag_get(BSP_USART, USART_FLAG_TBE));
}

void usart3_config(uint32_t band_rate)
{
    rcu_periph_clock_enable(BSP_UART3_TX_RCU);
    rcu_periph_clock_enable(BSP_UART3_RCU);
    
    gpio_af_set(BSP_UART3_TX_PORT, BSP_UART3_AF, BSP_UART3_TX_PIN);    
    gpio_af_set(BSP_UART3_RX_PORT, BSP_UART3_AF, BSP_UART3_RX_PIN);    
    
    gpio_mode_set(BSP_UART3_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_UART3_TX_PIN);
    gpio_mode_set(BSP_UART3_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_UART3_RX_PIN);
    
    gpio_output_options_set(BSP_UART3_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_UART3_TX_PIN);

    usart_deinit(BSP_UART3);
    usart_baudrate_set(BSP_UART3, band_rate);
    usart_enable(BSP_UART3);
    usart_transmit_config(BSP_UART3, USART_TRANSMIT_ENABLE);
}

void usart3_send_data(uint8_t data)
{
    usart_data_transmit(BSP_UART3, data);
    while(RESET == usart_flag_get(BSP_UART3, USART_FLAG_TBE));
}

void usart_send_string(char *ucstr)
{
    while(*ucstr) {
        usart_send_data((uint8_t)(*ucstr++));
    }
}

int fputc(int ch, FILE *f)
{
     usart_send_data(ch);
     return ch;
}

/* 🚨 终极抗风暴版：DMA + IDLE 乒乓接收中断 */
void BSP_USART_IRQHandler(void)
{
    // =========================================================
    // 🛡️ 1. 错误拦截器：无情粉碎溢出错误 (ORERR) 与帧错误，防止死机！
    // =========================================================
    // 🚨 修复：使用 usart_flag_get 获取硬件错误状态标志
    if(usart_flag_get(BSP_USART, USART_FLAG_ORERR) != RESET ||
       usart_flag_get(BSP_USART, USART_FLAG_FERR) != RESET)
    {
        // 按照 GD32 硬件手册，先读 STAT0，再读 DATA 即可清除所有硬件报错
        volatile uint32_t err_temp = USART_STAT0(BSP_USART);
        err_temp = USART_DATA(BSP_USART);
        (void)err_temp; 
    }

    // =========================================================
    // 📦 2. 正常数据接收：空闲中断处理
    // =========================================================
    if(usart_interrupt_flag_get(BSP_USART, USART_INT_FLAG_IDLE) != RESET)
    {
        // 清除 IDLE 标志
        volatile uint32_t temp = USART_STAT0(BSP_USART);
        temp = USART_DATA(BSP_USART);
        (void)temp; 

        // 暂停 DMA 准备结算
        dma_channel_disable(RX_DMAx, RX_DMA_CHy);
        
        // 🚨 【关键修复】：彻底清除 DMA 的“传输完成”残留标志！
        // 否则如果发生过满载，GD32 会直接拒绝下一次 DMA 启动
        dma_flag_clear(RX_DMAx, RX_DMA_CHy, DMA_FLAG_FTF);
        
        // 结算这包数据有多长
        uint16_t rx_len = UART_RX_BUFFER_SIZE - dma_transfer_number_get(RX_DMAx, RX_DMA_CHy);

        // 🚨 修复 1：把 < 改成 <=，允许缓冲区刚好被填满时也能处理
        if(rx_len > 0 && rx_len <= UART_RX_BUFFER_SIZE) 
        {
            // 乒乓切换
            if(p_write_buf == rx_buffer_A) {
                ready_buffer_id = 1;
                ready_rx_len = rx_len;  // 🚨 修复 2：千万别漏了这句！把真实长度交给 task2！
                p_write_buf = rx_buffer_B;
                dma_memory_address_config(RX_DMAx, RX_DMA_CHy, DMA_MEMORY_0, (uint32_t)rx_buffer_B);
            } else {
                ready_buffer_id = 2;
                ready_rx_len = rx_len;  // 🚨 修复 2：这句也不能漏！
                p_write_buf = rx_buffer_A;
                dma_memory_address_config(RX_DMAx, RX_DMA_CHy, DMA_MEMORY_0, (uint32_t)rx_buffer_A);
            }
        }

        // 重置弹匣，重新开启 DMA
        dma_transfer_number_config(RX_DMAx, RX_DMA_CHy, UART_RX_BUFFER_SIZE);
        dma_channel_enable(RX_DMAx, RX_DMA_CHy);
    }
}

// ... 前面保持你原来的 usart_dma_rx_config 和 BSP_USART_IRQHandler 等代码不变 ...

/* ==========================================================
   🚀 双路高速转发引擎：UART3 (PC上位机) + USART1 (H737计算节点)
   ========================================================== */
void usart_forward_config(uint32_t baud_rate)
{
    /* ----------------------------------------------------
       1. 配置 PC10 / PC11 -> UART3 (连接 PC 上位机)
       GD32F4 对应的复用功能为 AF8
       ---------------------------------------------------- */
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_UART3);
    
    gpio_af_set(GPIOC, GPIO_AF_8, GPIO_PIN_10 | GPIO_PIN_11);    
    gpio_mode_set(GPIOC, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_10 | GPIO_PIN_11);
    gpio_output_options_set(GPIOC, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_10 | GPIO_PIN_11);

    usart_deinit(UART3);
    usart_baudrate_set(UART3, baud_rate);
    usart_transmit_config(UART3, USART_TRANSMIT_ENABLE);
    usart_enable(UART3);

   
    /* ----------------------------------------------------
       2. 配置 PD5 / PD6 -> USART1 (连接 GD32H737 边缘计算)
       ---------------------------------------------------- */
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_USART1);
    
    gpio_af_set(GPIOD, GPIO_AF_7, GPIO_PIN_5 | GPIO_PIN_6);    
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_5 | GPIO_PIN_6);
    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_5 | GPIO_PIN_6);

    usart_deinit(USART1);
    usart_baudrate_set(USART1, baud_rate);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE);
    
    // 🚨 新增：开启 USART1 的接收与 RXNE 中断
    usart_receive_config(USART1, USART_RECEIVE_ENABLE); 
    nvic_irq_enable(USART1_IRQn, 4, 0);             // 优先级设置为 4
    usart_interrupt_enable(USART1, USART_INT_RBNE); // 接收缓冲区非空中断

    usart_enable(USART1);
}

/* 一键双发：把 94 字节结构体同时打给 PC 和 H737 */
void usart_forward_send_packet(uint8_t *packet, uint16_t len)
{
    // 🎯 核心护城河：进入临界区，禁止任何任务抢占，一口气发完！
    taskENTER_CRITICAL(); 
    
    for(uint16_t i = 0; i < len; i++) {
        // 1. 发射给上位机 (UART3)
        usart_data_transmit(UART3, packet[i]);
        while(RESET == usart_flag_get(UART3, USART_FLAG_TBE));
        
        // 2. 发射给 H737 边缘计算 (USART1)
        usart_data_transmit(USART1, packet[i]);
        while(RESET == usart_flag_get(USART1, USART_FLAG_TBE));
    }
    
    // 🎯 发送完毕，退出临界区，恢复系统调度
    taskEXIT_CRITICAL(); 
}


extern volatile uint32_t g_h7_frame_count; // 引入刚刚在 main.c 定义的变量

extern volatile uint32_t g_h7_frame_count; 
extern volatile uint8_t  g_ai_result; // 引入你在 main.c 定义的 AI 结果全局变量

/* 🎯 USART1 中断：轻量级定长状态机 (抗 FreeRTOS 打断，专杀错位丢包) */
void USART1_IRQHandler(void)
{
    static uint8_t rx_buf[94];
    static uint8_t rx_idx = 0;

    // 1. 核心护城河：无情拦截溢出错误 (ORERR)，防止 FreeRTOS 调度导致死机
    if(usart_flag_get(USART1, USART_FLAG_ORERR) != RESET) {
        volatile uint32_t err = USART_STAT0(USART1);
        volatile uint32_t data = USART_DATA(USART1);
        (void)err; (void)data;
			
			// 🎯 增加这句：既然丢包错位了，当前包直接作废，重头等 0xAA！
        rx_idx = 0;
    }

    // 2. 正常数据接收 (仅靠 RBNE 单字节硬吃，完全无视时间停顿)
    if(usart_interrupt_flag_get(USART1, USART_INT_FLAG_RBNE) != RESET) {
        uint8_t data = usart_data_receive(USART1);
        
        // 🚨 严格死守包头：如果当前还没存数据，且来的不是 0xAA，直接扔掉，原地等下一个 0xAA
        if(rx_idx == 0 && data != 0xAA) return; 
        
        rx_buf[rx_idx++] = data;
        
        // 攒满完整的一包 94 字节
        if(rx_idx == 94) { 
            // 🚨 严格校验包尾：要么是初始的 0x55，要么是 H737 篡改后的分类结果 (0~5)
            if(rx_buf[93] == 0x55 || rx_buf[93] <= 7) { 
                // 完美匹配！提取帧数和 AI 结果
                g_h7_frame_count = rx_buf[1] | (rx_buf[2] << 8) | (rx_buf[3] << 16) | (rx_buf[4] << 24);
                g_ai_result = rx_buf[93]; // 🎯 终于拿回了 H737 的心血！
            }
            // 无论这包数据是好是坏，只要长度够了，立刻清零重置状态机，准备吃下一帧
            rx_idx = 0; 
        }
    }
}

