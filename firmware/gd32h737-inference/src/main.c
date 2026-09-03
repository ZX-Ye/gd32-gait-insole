/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#include "gd32h7xx.h"
#include "gd32h7xx_dma.h"
#include <string.h> 
#include <stdio.h>
#include "gd_nn_interface.h"    
#include "nn_model_configure.h" 

// =========================================================================
// 🚨🚨🚨 核心救命补丁：ARM Compiler 6 (AC6) 彻底禁用半主机模式！
// =========================================================================
__asm(".global __use_no_semihosting\n\t");

void _sys_exit(int x) { 
    (void)x; 
    while(1);
}

void _ttywrch(int ch) {
    (void)ch;
}

FILE __stdout;

int fputc(int ch, FILE *f) {
    (void)f; 
    return ch; 
}
// =========================================================================

// ==========================================
// 🚀 核心修改 1：对齐 30Hz 模型的窗口与步长
// ==========================================
#define WINDOW_FRAMES       45    // 对应 1500ms 窗口 @ 30Hz
#define FEATURES_PER_FRAME  44    
#define TOTAL_FEATURES      (WINDOW_FRAMES * FEATURES_PER_FRAME) // 自动变为 1980
#define INFERENCE_STEP      15    // 对应 500ms 步长 @ 30Hz

// ==========================================
// 🌟 核心新增：定义两块 Raw Data 模块的特征数量
// ==========================================
#define ADC_FEATURES        32  // 左16 + 右16
#define IMU_FEATURES        12  // 左6 + 右6

extern volatile uint8_t data_ready;
volatile uint32_t debug_frame_count = 0; 

__attribute__((aligned(32))) uint8_t rx_buffer[96]; 
__attribute__((aligned(32))) uint8_t tx_buffer[96];

#pragma pack(1)
typedef struct {
    uint8_t  header;          
    uint32_t timestamp;       
    uint16_t left_adc[16];    
    int16_t  left_imu[6];     
    uint16_t right_adc[16];   
    int16_t  right_imu[6];    
    uint8_t  tail;            
} CombinedDataPacket;
#pragma pack()

// 🌟 二维缓冲池，分离时间轴和张量拼接
static float frame_buffer[WINDOW_FRAMES][FEATURES_PER_FRAME]; 

static float ai_input_buffer[TOTAL_FEATURES];
static float* output_data_ptr[1];       
static int current_frame_count = 0;
static uint8_t latest_ai_result = 0xFF; 

static nn_uint8 model_paras_array_info_buf[8];
static const nn_uint8* model_paras_array_and_data[2] = {model_paras_arr, model_paras_data};
static nn_model finnal1; 

void gd_nn_measure_time_start(void) {}
float gd_nn_measure_time_get(float scale, uint32_t clock) { (void)scale; (void)clock; return 0.0f; }
void gd_nn_measure_time_stop(void) {}

void dma_config(void)
{
    rcu_periph_clock_enable(RCU_DMA0);
    rcu_periph_clock_enable(RCU_DMAMUX);

    dma_single_data_parameter_struct dma_init = {0};

    dma_deinit(DMA0, DMA_CH0);
    dma_init.periph_addr = (uint32_t)&USART_RDATA(USART1);
    dma_init.memory0_addr = (uint32_t)rx_buffer;
    dma_init.number = 94; 
    dma_init.direction = DMA_PERIPH_TO_MEMORY;
    dma_init.periph_inc = DMA_PERIPH_INCREASE_DISABLE;
    dma_init.memory_inc = DMA_MEMORY_INCREASE_ENABLE;     
    dma_init.periph_memory_width = DMA_PERIPH_WIDTH_8BIT; 
    dma_init.priority = DMA_PRIORITY_HIGH;
    dma_init.circular_mode = DMA_CIRCULAR_MODE_ENABLE; 
    dma_init.request = DMA_REQUEST_USART1_RX;          
    dma_single_data_mode_init(DMA0, DMA_CH0, &dma_init);
    dma_interrupt_enable(DMA0, DMA_CH0, DMA_INT_FTF);  
}

void minimal_usart_init(void) 
{
    rcu_periph_clock_enable(RCU_GPIOD);
    rcu_periph_clock_enable(RCU_USART1);

    gpio_af_set(GPIOD, GPIO_AF_7, GPIO_PIN_5);
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_5);
    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, GPIO_PIN_5);
    
    gpio_af_set(GPIOD, GPIO_AF_7, GPIO_PIN_6);
    gpio_mode_set(GPIOD, GPIO_MODE_AF, GPIO_PUPD_PULLUP, GPIO_PIN_6);
    gpio_output_options_set(GPIOD, GPIO_OTYPE_PP, GPIO_OSPEED_60MHZ, GPIO_PIN_6);

    usart_deinit(USART1);
    usart_baudrate_set(USART1, 1500000U); 

    usart_receive_config(USART1, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART1, USART_TRANSMIT_ENABLE); 
    
    usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE);
    nvic_irq_enable(DMA0_Channel0_IRQn, 0, 0);
    usart_enable(USART1);
    dma_config(); 
}

int main(void)
{
    nvic_priority_group_set(NVIC_PRIGROUP_PRE4_SUB0);
    SCB_EnableICache();
    SCB_EnableDCache();
    minimal_usart_init();

    // AI 初始化
    finnal1.user_input = ai_input_buffer;
    finnal1.user_input_size = TOTAL_FEATURES * 4;  
    finnal1.user_output = (void**)output_data_ptr;
    // ==========================================
    // 🚀 核心修改 2：适配 8 个动作类别！(原为 6)
    // ==========================================
    finnal1.user_output_size = 8 * 4;               
    finnal1.operators_cb_array = func_cb_arr;
    finnal1.model_paras_array = (const nn_uint8*)model_paras_array_and_data;
    finnal1.model_paras_array_dict = model_paras_array_info_buf;
    finnal1.report_ptr = NULL; 
    nn_model_init(&finnal1); 

    // 物理级时序对齐
    for(volatile int delay = 0; delay < 200000000; delay++); 
    uint32_t idle_count = 0;
    while(idle_count < 1000000) { 
        if(gpio_input_bit_get(GPIOD, GPIO_PIN_6) == SET) idle_count++;
        else idle_count = 0; 
    }
    usart_flag_clear(USART1, USART_FLAG_ORERR);
    usart_flag_clear(USART1, USART_FLAG_FERR);
    volatile uint32_t dummy_flush = USART_RDATA(USART1);
    (void)dummy_flush; 
    dma_channel_enable(DMA0, DMA_CH0);

    while(1) {
        if(usart_flag_get(USART1, USART_FLAG_ORERR) != RESET) {
            usart_flag_clear(USART1, USART_FLAG_ORERR);
            volatile uint32_t dummy = USART_RDATA(USART1);
            (void)dummy;
            dma_channel_disable(DMA0, DMA_CH0);
            dma_flag_clear(DMA0, DMA_CH0, DMA_FLAG_FTF);
            dma_transfer_number_config(DMA0, DMA_CH0, 94);
            dma_channel_enable(DMA0, DMA_CH0);
        }

        if (data_ready) {
            data_ready = 0; 
            debug_frame_count++;  

            SCB_InvalidateDCache_by_Addr((uint32_t *)rx_buffer, 96);
            CombinedDataPacket *rx_packet = (CombinedDataPacket *)rx_buffer;

            if(rx_packet->header == 0xAA) {
                int f_idx = 0;

                // 第一步：写入二维缓冲池
                for(int i=0; i<16; i++) frame_buffer[current_frame_count][f_idx++] = rx_packet->right_adc[i] * 0.002f;
                for(int i=0; i<16; i++) frame_buffer[current_frame_count][f_idx++] = rx_packet->left_adc[i]  * 0.002f;
                for(int i=0; i<6; i++)  frame_buffer[current_frame_count][f_idx++] = rx_packet->right_imu[i] * 0.00024414f;
                for(int i=0; i<6; i++)  frame_buffer[current_frame_count][f_idx++] = rx_packet->left_imu[i]  * 0.00024414f;

                current_frame_count++;
                
                // 第二步：满 45 帧，准备拆分装箱给模型！
                if (current_frame_count >= WINDOW_FRAMES) {
                    
                    int tensor_idx = 0;
                    
                    // 📦 装箱一：提取连续 45 帧的 ADC 数据 (1440 个特征)
                    for (int frame = 0; frame < WINDOW_FRAMES; frame++) {
                        for (int a = 0; a < ADC_FEATURES; a++) {
                            ai_input_buffer[tensor_idx++] = frame_buffer[frame][a];
                        }
                    }
                    
                    // 📦 装箱二：提取连续 45 帧的 IMU 数据 (540 个特征)
                    for (int frame = 0; frame < WINDOW_FRAMES; frame++) {
                        for (int m = 0; m < IMU_FEATURES; m++) {
                            ai_input_buffer[tensor_idx++] = frame_buffer[frame][ADC_FEATURES + m];
                        }
                    }

                    // 🐉 释放 AI 猛兽！
                    nn_model_invoke(&finnal1); 
                    
                    float* ai_output_buffer = output_data_ptr[0];
                    float max_score = ai_output_buffer[0];
                    latest_ai_result = 0;
                    // ==========================================
                    // 🚀 核心修改 3：历遍 8 个类别的置信度找最大值！
                    // ==========================================
                    for(int i = 1; i < 8; i++) {
                        if (ai_output_buffer[i] > max_score) {
                            max_score = ai_output_buffer[i];
                            latest_ai_result = (uint8_t)i; 
                        }
                    }
										

                    // 第三步：滑动二维窗口内存
                    int keep_frames = WINDOW_FRAMES - INFERENCE_STEP; 
                    memmove(frame_buffer[0], frame_buffer[INFERENCE_STEP], keep_frames * FEATURES_PER_FRAME * sizeof(float));
                    current_frame_count = keep_frames; 
                }
            }

            // 转发包和延时保护原封不动
            memcpy(tx_buffer, rx_buffer, 94);
            CombinedDataPacket *packet_to_send = (CombinedDataPacket *)tx_buffer;
            
            packet_to_send->header = 0xAA; 
            packet_to_send->timestamp = debug_frame_count; 
            if (latest_ai_result != 0xFF) {
                packet_to_send->tail = latest_ai_result; 
            } else {
                packet_to_send->tail = 0x55; 
            }

            for(volatile int delay = 0; delay < 200000; delay++);

            for(int i = 0; i < 94; i++) {
                usart_data_transmit(USART1, tx_buffer[i]);
                while(RESET == usart_flag_get(USART1, USART_FLAG_TBE));
                for(volatile int nop = 0; nop < 150; nop++); 
            }
        }
    }
}
