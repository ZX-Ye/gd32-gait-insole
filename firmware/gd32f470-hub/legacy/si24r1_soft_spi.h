/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#ifndef SI24R1_SOFT_SPI_H
#define SI24R1_SOFT_SPI_H

#include "gd32f4xx.h"

/* ===== 硬件引脚定义 (PC8-PC12, PD2) ===== */
#define SI24_SPI_PORT             GPIOC
#define SI24_SPI_RCC              RCU_GPIOC

#define SI24_SCK_PIN              GPIO_PIN_8    // PC8
#define SI24_MOSI_PIN             GPIO_PIN_9    // PC9
#define SI24_MISO_PIN             GPIO_PIN_10   // PC10
#define SI24_CSN_PIN              GPIO_PIN_11   // PC11
#define SI24_CE_PIN               GPIO_PIN_12   // PC12

#define SI24_IRQ_PORT             GPIOD
#define SI24_IRQ_RCC              RCU_GPIOD
#define SI24_IRQ_PIN              GPIO_PIN_2    // PD2

/* ===== GPIO 操作宏 (极速寄存器版本) ===== */
// GD32F4系列: GPIO_BOP为置位寄存器, GPIO_BC为清除寄存器, GPIO_ISTAT为输入状态寄存器
#define SI24_SCK_HIGH()           (GPIO_BOP(SI24_SPI_PORT) = (uint32_t)SI24_SCK_PIN)
#define SI24_SCK_LOW()            (GPIO_BC(SI24_SPI_PORT)  = (uint32_t)SI24_SCK_PIN)

#define SI24_MOSI_HIGH()          (GPIO_BOP(SI24_SPI_PORT) = (uint32_t)SI24_MOSI_PIN)
#define SI24_MOSI_LOW()           (GPIO_BC(SI24_SPI_PORT)  = (uint32_t)SI24_MOSI_PIN)

#define SI24_MISO_READ()          ((GPIO_ISTAT(SI24_SPI_PORT) & (uint32_t)SI24_MISO_PIN) ? 1 : 0)

#define SI24_CSN_HIGH()           (GPIO_BOP(SI24_SPI_PORT) = (uint32_t)SI24_CSN_PIN)
#define SI24_CSN_LOW()            (GPIO_BC(SI24_SPI_PORT)  = (uint32_t)SI24_CSN_PIN)

#define SI24_CE_HIGH()            (GPIO_BOP(SI24_SPI_PORT) = (uint32_t)SI24_CE_PIN)
#define SI24_CE_LOW()             (GPIO_BC(SI24_SPI_PORT)  = (uint32_t)SI24_CE_PIN)

/* ===== 函数声明 ===== */
void SI24_Init(void);           // 初始化GPIO和中断
void SI24_RX_Mode(void);        // 进入接收模式
uint8_t SI24_SPI_RW(uint8_t data); // SPI读写
uint8_t SI24_Read_Reg(uint8_t reg);
uint8_t SI24_Write_Reg(uint8_t reg, uint8_t value);

#endif
