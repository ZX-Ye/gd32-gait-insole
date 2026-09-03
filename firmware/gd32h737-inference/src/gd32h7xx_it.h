/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#ifndef GD32H7XX_IT_H
#define GD32H7XX_IT_H

#include "gd32h7xx.h"

void NMI_Handler(void);
void HardFault_Handler(void);
void USART1_IRQHandler(void); // 我们需要的串口桥接中断

#endif