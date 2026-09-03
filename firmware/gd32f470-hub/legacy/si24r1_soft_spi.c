/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#include "si24r1_soft_spi.h"
#include "systick.h" // 确保你的工程里有这个，用于 delay_1ms

/* 寄存器命令定义 */
#define CMD_READ_REG    0x00
#define CMD_WRITE_REG   0x20
#define CMD_RD_RX_PLOAD 0x61
#define CMD_WR_TX_PLOAD 0xA0
#define CMD_FLUSH_TX    0xE1
#define CMD_FLUSH_RX    0xE2
#define CMD_NOP         0xFF

/* 寄存器地址 */
#define REG_CONFIG      0x00
#define REG_EN_AA       0x01
#define REG_EN_RXADDR   0x02
#define REG_SETUP_AW    0x03
#define REG_SETUP_RETR  0x04
#define REG_RF_CH       0x05
#define REG_RF_SETUP    0x06
#define REG_STATUS      0x07
#define REG_RX_ADDR_P0  0x0A
#define REG_TX_ADDR     0x10
#define REG_RX_PW_P0    0x11

/* 本地地址设置 */
const uint8_t RX_ADDRESS[5] = {0x34, 0x43, 0x10, 0x10, 0x01};

/* GPIO & SPI 初始化 */
void SI24_Init(void)
{
    /* 1. 开启时钟 */
    rcu_periph_clock_enable(SI24_SPI_RCC);
    rcu_periph_clock_enable(SI24_IRQ_RCC);
    rcu_periph_clock_enable(RCU_SYSCFG); // EXTI需要

    /* 2. 配置输出引脚: SCK, MOSI, CSN, CE */
    gpio_mode_set(SI24_SPI_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, SI24_SCK_PIN | SI24_MOSI_PIN | SI24_CSN_PIN | SI24_CE_PIN);
    gpio_output_options_set(SI24_SPI_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, SI24_SCK_PIN | SI24_MOSI_PIN | SI24_CSN_PIN | SI24_CE_PIN);

    /* 3. 配置输入引脚: MISO */
    gpio_mode_set(SI24_SPI_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLDOWN, SI24_MISO_PIN);

    /* 4. 配置中断引脚: IRQ (PD2) */
    gpio_mode_set(SI24_IRQ_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, SI24_IRQ_PIN);

    /* 5. 配置 EXTI2 中断 (PD2 下降沿触发) */
    syscfg_exti_line_config(EXTI_SOURCE_GPIOD, GPIO_PIN_2);
    exti_init(EXTI_2, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_interrupt_flag_clear(EXTI_2);
    exti_interrupt_enable(EXTI_2);
    
    /* 6. 配置 NVIC (优先级设置为 6,0 - 配合 FreeRTOS) */
    nvic_irq_enable(EXTI2_IRQn, 6, 0);

    /* 7. 默认状态 */
    SI24_CE_LOW();
    SI24_CSN_HIGH();
    SI24_SCK_LOW();
}

/* 软件模拟 SPI 读写字节 (极致性能版) */
uint8_t SI24_SPI_RW(uint8_t data)
{
    uint8_t temp = 0;

    // 因为 GD32F470 跑到 200MHz，寄存器操作速度极快（约 5ns）
    // 为了防止 SI24R1 芯片反应不过来，保留少许 NOP 延时（您可以根据实际情况增减 NOP）

    /* --- Bit 7 --- */
    if(data & 0x80) SI24_MOSI_HIGH(); else SI24_MOSI_LOW();
    SI24_SCK_HIGH();
    __asm("nop"); __asm("nop"); 
    temp <<= 1; if(SI24_MISO_READ()) temp++;
    SI24_SCK_LOW();
    __asm("nop"); __asm("nop");

    /* --- Bit 6 --- */
    if(data & 0x40) SI24_MOSI_HIGH(); else SI24_MOSI_LOW();
    SI24_SCK_HIGH();
    __asm("nop"); __asm("nop"); 
    temp <<= 1; if(SI24_MISO_READ()) temp++;
    SI24_SCK_LOW();
    __asm("nop"); __asm("nop");

    /* --- Bit 5 --- */
    if(data & 0x20) SI24_MOSI_HIGH(); else SI24_MOSI_LOW();
    SI24_SCK_HIGH();
    __asm("nop"); __asm("nop"); 
    temp <<= 1; if(SI24_MISO_READ()) temp++;
    SI24_SCK_LOW();
    __asm("nop"); __asm("nop");

    /* --- Bit 4 --- */
    if(data & 0x10) SI24_MOSI_HIGH(); else SI24_MOSI_LOW();
    SI24_SCK_HIGH();
    __asm("nop"); __asm("nop"); 
    temp <<= 1; if(SI24_MISO_READ()) temp++;
    SI24_SCK_LOW();
    __asm("nop"); __asm("nop");

    /* --- Bit 3 --- */
    if(data & 0x08) SI24_MOSI_HIGH(); else SI24_MOSI_LOW();
    SI24_SCK_HIGH();
    __asm("nop"); __asm("nop"); 
    temp <<= 1; if(SI24_MISO_READ()) temp++;
    SI24_SCK_LOW();
    __asm("nop"); __asm("nop");

    /* --- Bit 2 --- */
    if(data & 0x04) SI24_MOSI_HIGH(); else SI24_MOSI_LOW();
    SI24_SCK_HIGH();
    __asm("nop"); __asm("nop"); 
    temp <<= 1; if(SI24_MISO_READ()) temp++;
    SI24_SCK_LOW();
    __asm("nop"); __asm("nop");

    /* --- Bit 1 --- */
    if(data & 0x02) SI24_MOSI_HIGH(); else SI24_MOSI_LOW();
    SI24_SCK_HIGH();
    __asm("nop"); __asm("nop"); 
    temp <<= 1; if(SI24_MISO_READ()) temp++;
    SI24_SCK_LOW();
    __asm("nop"); __asm("nop");

    /* --- Bit 0 --- */
    if(data & 0x01) SI24_MOSI_HIGH(); else SI24_MOSI_LOW();
    SI24_SCK_HIGH();
    __asm("nop"); __asm("nop"); 
    temp <<= 1; if(SI24_MISO_READ()) temp++;
    SI24_SCK_LOW();
    __asm("nop"); __asm("nop");

    return temp;
}

/* 写寄存器 */
uint8_t SI24_Write_Reg(uint8_t reg, uint8_t value)
{
    uint8_t status;
    SI24_CSN_LOW();
    status = SI24_SPI_RW(CMD_WRITE_REG | reg);
    SI24_SPI_RW(value);
    SI24_CSN_HIGH();
    return status;
}

/* 写缓冲区 */
uint8_t SI24_Write_Buf(uint8_t reg, uint8_t *pBuf, uint8_t len)
{
    uint8_t status, i;
    SI24_CSN_LOW();
    status = SI24_SPI_RW(CMD_WRITE_REG | reg);
    for(i = 0; i < len; i++) SI24_SPI_RW(pBuf[i]);
    SI24_CSN_HIGH();
    return status;
}

/* 配置为接收模式 */
void SI24_RX_Mode(void)
{
    SI24_CE_LOW();
    
    SI24_Write_Buf(REG_RX_ADDR_P0, (uint8_t*)RX_ADDRESS, 5); // 设置接收地址
    SI24_Write_Reg(REG_EN_AA, 0x01);      // 开启通道0自动应答
    SI24_Write_Reg(REG_EN_RXADDR, 0x01);  // 开启通道0接收地址
    SI24_Write_Reg(REG_RF_CH, 40);        // 射频通道 40 (2440MHz)
    SI24_Write_Reg(REG_RX_PW_P0, 32);     // 接收数据长度 32字节
    SI24_Write_Reg(REG_RF_SETUP, 0x0F);   // 0dBm, 2Mbps
    
    /* CONFIG: 16位CRC, 上电, 接收模式 */
    SI24_Write_Reg(REG_CONFIG, 0x0F); 
    
    SI24_CE_HIGH(); // 拉高CE启动接收
}
