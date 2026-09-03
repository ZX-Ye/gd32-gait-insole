/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#include "gd32h7xx_it.h"
#include "gd32h7xx.h"
#include "gd32h7xx_dma.h"

/* 必须包含的中断处理（防止程序卡死） */
void NMI_Handler(void) {}
void HardFault_Handler(void) { while(1); }
void MemManage_Handler(void) { while(1); }
void BusFault_Handler(void) { while(1); }
void UsageFault_Handler(void) { while(1); }

// 🚨 真正的定义在这里！分配内存！
volatile uint8_t data_ready = 0;

/* 🎯 纯粹的 DMA 接收完成中断 */
void DMA0_Channel0_IRQHandler(void)
{
    if(dma_interrupt_flag_get(DMA0, DMA_CH0, DMA_INT_FLAG_FTF) != RESET) {
        dma_interrupt_flag_clear(DMA0, DMA_CH0, DMA_INT_FLAG_FTF);
        
        // 这一包 94 字节已经稳稳地躺在 rx_buffer 里了
        data_ready = 1; 
    }
}
