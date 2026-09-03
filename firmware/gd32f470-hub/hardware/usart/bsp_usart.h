/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#ifndef _BSP_USART_H
#define _BSP_USART_H

#include "gd32f4xx.h"
#include "systick.h"

/* ===== 梁山派 GD32F470 UART4 串口配置 (PC12/PD2) ===== */
#define BSP_USART_TX_RCU  RCU_GPIOC   // 串口TX的端口时钟
#define BSP_USART_RX_RCU  RCU_GPIOD   // 串口RX的端口时钟
#define BSP_USART_RCU     RCU_UART4   // 串口UART4的时钟

#define BSP_USART_TX_PORT GPIOC       // 串口TX的端口
#define BSP_USART_RX_PORT GPIOD       // 串口RX的端口
#define BSP_USART_AF      GPIO_AF_8   // UART4的复用功能
#define BSP_USART_TX_PIN  GPIO_PIN_12 // 串口TX的引脚
#define BSP_USART_RX_PIN  GPIO_PIN_2  // 串口RX的引脚

#define BSP_USART             UART4         // 使用UART4
#define BSP_USART_IRQ         UART4_IRQn    // UART4中断
#define BSP_USART_IRQHandler  UART4_IRQHandler // 中断服务函数名

#define UART_RX_BUFFER_SIZE   1024

/* ===== 🚨 新增：UART4 的 DMA 接收引擎宏定义 ===== */
// 查阅 GD32F470 手册，UART4_RX 对应 DMA0，通道0，子外设4
#define RX_DMAx       DMA0
#define RX_DMA_CHy    DMA_CH0
#define RX_DMA_SUB    DMA_SUBPERI4
#define RX_DMA_RCU    RCU_DMA0
/* ================================================= */

extern char rx_buffer_A[UART_RX_BUFFER_SIZE];
extern char rx_buffer_B[UART_RX_BUFFER_SIZE];
extern volatile uint8_t ready_buffer_id; 

void usart_gpio_config(uint32_t band_rate);
void usart_send_data(uint8_t ucch);
void usart_send_string(char *ucstr);
void usart_dma_rx_config(void); // 🚨 新增 DMA 初始化声明

/* ===== 增加：梁山派 -> VW553 的转发串口 UART3 (PC10/PC11) ===== */
#define BSP_UART3_TX_RCU  RCU_GPIOC
#define BSP_UART3_RX_RCU  RCU_GPIOC
#define BSP_UART3_RCU     RCU_USART2  // 注意：GD32的USART2就是UART3

#define BSP_UART3_TX_PORT GPIOC
#define BSP_UART3_RX_PORT GPIOC
#define BSP_UART3_AF      GPIO_AF_7   // USART2 复用引脚
#define BSP_UART3_TX_PIN  GPIO_PIN_10
#define BSP_UART3_RX_PIN  GPIO_PIN_11

#define BSP_UART3             USART2
void usart3_config(uint32_t band_rate);
void usart3_send_data(uint8_t data);
void usart_forward_config(uint32_t baud_rate); 
void usart_forward_send_packet(uint8_t *packet, uint16_t len);
void USART1_IRQHandler(void);

#endif
