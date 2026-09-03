/********************************** (C) COPYRIGHT *******************************
 * Derived from the WCH CH583 EVT "BLE/Peripheral" example.
 *
 * Original notice, reproduced verbatim from the WCH SDK:
 *
 *   Author             : WCH
 *   Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 *   Attention: This software (modified or not) and binary are used for
 *   microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *
 * Modified 2026 by the project authors: 16-channel piezoresistive sampling
 * (analog mux + dummy-read + 4x oversampling), Bosch BMI270 I2C glue,
 * 44-byte binary sensor packet, LE Coded PHY request, PWM breathing LED.
 * Modifications are licensed under Apache-2.0; see LICENSE and NOTICE.
 * The WCH-derived portions remain subject to the notice above.
 *******************************************************************************/

/******************************************************************************/
/* 头文件包含 */
#include "CONFIG.h"
#include "HAL.h"
#include "gattprofile.h"
#include "peripheral.h"
#include <stdio.h>
#include <stdarg.h>

// 1. 手搓一个支持格式化的 UART0 打印函数
void UART0_Printf(const char *fmt, ...) {
    char buf[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    uint16_t len = 0;
    while(buf[len] != '\0') len++;
    UART0_SendString((uint8_t*)buf, len);
}

// 2. 强行夺取 PRINT 宏的控制权
#undef PRINT
#define PRINT UART0_Printf

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#endif

/*********************************************************************
 * @fn      Main_Circulation
 *
 * @brief   主循环
 *
 * @return  none
 */
__HIGH_CODE
__attribute__((noinline))
void Main_Circulation()
{
    while(1)
    {
        // 蓝牙协议栈的无尽心跳，一切的动力源泉
        TMOS_SystemProcess();
    }
}

/*********************************************************************
 * @fn      main
 *
 * @brief   主函数
 *
 * @return  none
 */
int main(void)
{
// mDelaymS(2000);
#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
    PWR_DCDCCfg(ENABLE);
#endif
    SetSysClock(CLK_SOURCE_PLL_60MHz);
// #if(defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
//     GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
//     GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
// #endif

    // 【核心修改】：初始化你的 UART0 (PB7 作为 TX)
    GPIOB_SetBits(GPIO_Pin_7);
    GPIOB_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
    UART0_DefInit(); // 默认波特率依然是 115200

    PRINT("\r\n============================\r\n");
    PRINT("CH583M UART0 Redirect OK!\r\n");
    PRINT("Lib Version: %s\n", VER_LIB);
    PRINT("============================\r\n");
    
    CH58X_BLEInit();
    LL_SetTxPowerLevel(LL_TX_POWEER_6_DBM);
    HAL_Init();
    GAPRole_PeripheralInit();
    Peripheral_Init();
    
    // 进入主循环
    Main_Circulation();
}
