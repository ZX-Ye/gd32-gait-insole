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

/*********************************************************************
 * INCLUDES
 */
#include "CONFIG.h"
#include "devinfoservice.h"
#include "gattprofile.h"
#include "peripheral.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// 声明我们在 main 里写的打印函数
extern void UART0_Printf(const char *fmt, ...);

// 强行夺取 PRINT 宏的控制权
#undef PRINT
#define PRINT UART0_Printf

#include "bmi270.h"
#include "bmi2.h"

// 实例化 BMI270 设备
static struct bmi2_dev bmi270_dev;
static uint8_t bmi270_dev_addr = BMI2_I2C_PRIM_ADDR; // 0x68

// ==========================================================
// ? 极速二进制传感器数据包 (固定 44 字节)
// ==========================================================
#pragma pack(1)
typedef struct {
    int16_t adc[16];   // 16路 FSR 压力数据 (32字节)
    int16_t imu[6];    // 6轴 IMU 姿态数据 (12字节)
} SensorDataPacket_t;
#pragma pack()


// =======================================================================
// ? BMI270 硬件 I2C 读写接口 (适配 CH583 硬件 I2C 寄存器)
// 引脚: PB12(SDA), PB13(SCL)
// =======================================================================
int8_t ch583_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    uint8_t addr = (*((uint8_t*)intf_ptr)) << 1; // WCH库要求地址左移1位
    
    while( I2C_GetFlagStatus( I2C_FLAG_BUSY ) != RESET );
    I2C_GenerateSTART( ENABLE );
    while( !I2C_CheckEvent( I2C_EVENT_MASTER_MODE_SELECT ) );
    I2C_Send7bitAddress( addr, I2C_Direction_Transmitter );
    while( !I2C_CheckEvent( I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED ) );
    I2C_SendData( reg_addr );
    while( !I2C_CheckEvent( I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
    
    I2C_GenerateSTART( ENABLE );
    while( !I2C_CheckEvent( I2C_EVENT_MASTER_MODE_SELECT ) );
    I2C_Send7bitAddress( addr, I2C_Direction_Receiver );
    while( !I2C_CheckEvent( I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED ) );
    
    for(uint32_t i=0; i<len; i++) {
        if( i == len-1 ) {
            I2C_AcknowledgeConfig( DISABLE );
            I2C_GenerateSTOP( ENABLE );
        }
        while( I2C_GetFlagStatus( I2C_FLAG_RXNE ) == RESET );
        reg_data[i] = I2C_ReceiveData();
    }
    I2C_AcknowledgeConfig( ENABLE );
    return 0;
}

int8_t ch583_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr) {
    uint8_t addr = (*((uint8_t*)intf_ptr)) << 1;
    
    while( I2C_GetFlagStatus( I2C_FLAG_BUSY ) != RESET );
    I2C_GenerateSTART( ENABLE );
    while( !I2C_CheckEvent( I2C_EVENT_MASTER_MODE_SELECT ) );
    I2C_Send7bitAddress( addr, I2C_Direction_Transmitter );
    while( !I2C_CheckEvent( I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED ) );
    I2C_SendData( reg_addr );
    while( !I2C_CheckEvent( I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
    
    for(uint32_t i=0; i<len; i++) {
        I2C_SendData( reg_data[i] );
        while( !I2C_CheckEvent( I2C_EVENT_MASTER_BYTE_TRANSMITTED ) );
    }
    I2C_GenerateSTOP( ENABLE );
    return 0;
}

void ch583_delay_us(uint32_t period, void *intf_ptr) {
    mDelayuS(period);
}

/*********************************************************************
 * CONSTANTS & MACROS
 */
#define SBP_PERIODIC_EVT_PERIOD              20     // 80Hz (12.5ms)
#define SBP_READ_RSSI_EVT_PERIOD             3200
#define SBP_PARAM_UPDATE_DELAY               6400
#define SBP_PHY_UPDATE_DELAY                 2400
#define DEFAULT_ADVERTISING_INTERVAL         80
#define DEFAULT_DISCOVERABLE_MODE            GAP_ADTYPE_FLAGS_GENERAL
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL    8
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL    12
#define DEFAULT_DESIRED_SLAVE_LATENCY        0
#define DEFAULT_DESIRED_CONN_TIMEOUT         300
#define WCH_COMPANY_ID                       0x07D7

// =================== 硬件引脚配置 ===================
#define PIN_FSR_POWER       GPIO_Pin_18 // 传感器动态供电脚
#define PIN_MUX_1314        GPIO_Pin_11 // PA11 控制 ADC13/14
#define PIN_MUX_1516        GPIO_Pin_10 // PA10 控制 ADC15/16

static const uint8_t ADC_DIRECT_CHANNELS[12] = {
    CH_EXTIN_0,  CH_EXTIN_1,  CH_EXTIN_2,  CH_EXTIN_3, 
    CH_EXTIN_4,  CH_EXTIN_5,  CH_EXTIN_6,  CH_EXTIN_7, 
    CH_EXTIN_8,  CH_EXTIN_9,  CH_EXTIN_10, CH_EXTIN_11 
};

#define MUX_COM1_CH         CH_EXTIN_12
#define MUX_COM2_CH         CH_EXTIN_13

/*********************************************************************
 * GLOBAL VARIABLES
 */
static uint8_t Peripheral_TaskID = INVALID_TASK_ID;
static peripheralConnItem_t peripheralConnList;
static uint16_t peripheralMTU = ATT_MTU_SIZE;




// 新增：ADC 真实数据、字符串缓冲、校准值
static uint16_t current_adc_values[16]; 
static char ble_tx_str_buf[256];        
static signed short RoughCalib_Value = 0; 

// =======================================================================
// 左脚 FSR 传感器物理映射表 (消除硬件走线镜像带来的顺序错乱)
// 数组的 index (0-15) 代表标准输出顺序，数组的 value 代表对应的 ADC 真实通道
// =======================================================================
static const uint8_t LEFT_FOOT_MAP[16] = {
    3, 4, 2, 5, 0, 11, 8, 1, 15, 7, 10, 14, 6, 9, 12, 13 
    // TODO: 拿着左脚鞋垫按压测试，把这里的 0-15 替换成你实际测出的通道号
};

// =======================================================================
// =================== BMI270 I2C 极简 Ping 测试引擎 ===================
// =======================================================================
#define IIC_SCL_PIN   GPIO_Pin_13  // PB13
#define IIC_SDA_PIN   GPIO_Pin_12  // PB12

#define SDA_OUT()   GPIOB_ModeCfg(IIC_SDA_PIN, GPIO_ModeOut_PP_5mA)
#define SDA_IN()    GPIOB_ModeCfg(IIC_SDA_PIN, GPIO_ModeIN_PU)
#define SDA_HIGH()  GPIOB_SetBits(IIC_SDA_PIN)
#define SDA_LOW()   GPIOB_ResetBits(IIC_SDA_PIN)
#define SDA_READ()  GPIOB_ReadPortPin(IIC_SDA_PIN)

#define SCL_OUT()   GPIOB_ModeCfg(IIC_SCL_PIN, GPIO_ModeOut_PP_5mA)
#define SCL_HIGH()  GPIOB_SetBits(IIC_SCL_PIN)
#define SCL_LOW()   GPIOB_ResetBits(IIC_SCL_PIN)

static void IIC_Delay(void) { mDelayuS(2); }

static void IIC_Init(void) {
    SCL_OUT(); SDA_OUT();
    SCL_HIGH(); SDA_HIGH();
}

static void IIC_Start(void) {
    SDA_OUT();
    SDA_HIGH(); SCL_HIGH(); IIC_Delay();
    SDA_LOW(); IIC_Delay();
    SCL_LOW();
}

static void IIC_Stop(void) {
    SDA_OUT();
    SCL_LOW(); SDA_LOW(); IIC_Delay();
    SCL_HIGH(); IIC_Delay();
    SDA_HIGH(); IIC_Delay();
}

static uint8_t IIC_Wait_Ack(void) {
    uint8_t ack = 0;
    SDA_IN();
    SCL_HIGH(); IIC_Delay();
    if(SDA_READ()) ack = 1; // 1 表示 NACK (没收到应答)
    SCL_LOW(); IIC_Delay();
    return ack;
}

static void IIC_Send_Byte(uint8_t txd) {
    SDA_OUT();
    for(uint8_t i = 0; i < 8; i++) {
        if(txd & 0x80) SDA_HIGH();
        else SDA_LOW();
        txd <<= 1;
        SCL_HIGH(); IIC_Delay();
        SCL_LOW(); IIC_Delay();
    }
}

static uint8_t IIC_Read_Byte(uint8_t ack) {
    uint8_t receive = 0;
    SDA_IN();
    for(uint8_t i = 0; i < 8; i++) {
        SCL_HIGH(); IIC_Delay();
        receive <<= 1;
        if(SDA_READ()) receive++;
        SCL_LOW(); IIC_Delay();
    }
    SDA_OUT();
    if(ack) SDA_LOW(); else SDA_HIGH(); // 发送 ACK 或 NACK
    SCL_HIGH(); IIC_Delay();
    SCL_LOW(); IIC_Delay();
    return receive;
}




// 蓝牙广播包
static uint8_t scanRspData[] = {
    0x0E, GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    0xE6, 0x99, 0xBA,  // 智
    0xE8, 0x83, 0xBD,  // 能
    0xE9, 0x9E, 0x8B,  // 鞋
    0xE5, 0x9E, 0xAB,  // 垫
    'L',               // L (左脚)
    0x05, GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
    LO_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL), HI_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL),
    LO_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL), HI_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL),
    0x02, GAP_ADTYPE_POWER_LEVEL, 0 
};

static uint8_t advertData[] = {
    0x02, GAP_ADTYPE_FLAGS, DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,
    0x03, GAP_ADTYPE_16BIT_MORE, LO_UINT16(SIMPLEPROFILE_SERV_UUID), HI_UINT16(SIMPLEPROFILE_SERV_UUID)
};

static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "Simple Peripheral";

/*********************************************************************
 * LOCAL FUNCTIONS DECLARATIONS
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent);
static void performPeriodicTask(void);
static void simpleProfileChangeCB(uint8_t paramID, uint8_t *pValue, uint16_t len);
static void peripheralParamUpdateCB(uint16_t connHandle, uint16_t connInterval, uint16_t connSlaveLatency, uint16_t connTimeout);
static void peripheralInitConnItem(peripheralConnItem_t *peripheralConnList);
static void peripheralRssiCB(uint16_t connHandle, int8_t rssi);
static void peripheralChar4Notify(uint8_t *pValue, uint16_t len);

static gapRolesCBs_t Peripheral_PeripheralCBs = { peripheralStateNotificationCB, peripheralRssiCB, peripheralParamUpdateCB };
static gapRolesBroadcasterCBs_t Broadcaster_BroadcasterCBs = { NULL, NULL };
static gapBondCBs_t Peripheral_BondMgrCBs = { NULL, NULL, NULL };
static simpleProfileCBs_t Peripheral_SimpleProfileCBs = { simpleProfileChangeCB };

/*********************************************************************
 * CUSTOM HARDWARE FUNCTIONS
 */
void FSR_Hardware_Init(void) {
    GPIOB_ModeCfg(PIN_FSR_POWER, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(PIN_MUX_1314 | PIN_MUX_1516, GPIO_ModeOut_PP_5mA);
    GPIOB_ResetBits(PIN_FSR_POWER);
    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_1_4);
    RoughCalib_Value = ADC_DataCalib_Rough();
}

void BMI270_Hardware_Init(void) {
    int8_t rslt;
    
    PRINT("\r\n--- BMI270 Hardware I2C Init ---\r\n");
    // 1. 初始化硬件引脚
    GPIOB_ModeCfg( GPIO_Pin_12 | GPIO_Pin_13, GPIO_ModeIN_PU ); // PB12(SDA), PB13(SCL)
    GPIOB_ModeCfg( GPIO_Pin_11, GPIO_ModeIN_PU );               // PB11(INT - 目前仅作输入，不用中断)
    
    // 2. 初始化 CH583 硬件 I2C (400kHz)
    I2C_Init( I2C_Mode_I2C, 400000, I2C_DutyCycle_16_9, I2C_Ack_Enable, I2C_AckAddr_7bit, 0 );

    // 3. 挂载 Bosch 驱动接口
    bmi270_dev.intf = BMI2_I2C_INTF;
    bmi270_dev.read = ch583_i2c_read;
    bmi270_dev.write = ch583_i2c_write;
    bmi270_dev.delay_us = ch583_delay_us;
    bmi270_dev.intf_ptr = &bmi270_dev_addr;
    bmi270_dev.read_write_len = 32; // I2C burst 限制
    bmi270_dev.config_file_ptr = NULL; 

    // 4. 唤醒并加载配置 (这个过程耗时较长，因为要下载 8KB 微码)
    rslt = bmi270_init(&bmi270_dev);
    if(rslt == BMI2_OK) {
        PRINT(">>> BMI270 Driver Load SUCCESS!\r\n");
        
        // 5. 开启并配置 加速度计(ACC) 和 陀螺仪(GYRO)
        uint8_t sens_list[2] = { BMI2_ACCEL, BMI2_GYRO };
        struct bmi2_sens_config config[2];
        
        config[0].type = BMI2_ACCEL;
        config[0].cfg.acc.odr = BMI2_ACC_ODR_100HZ;     // 100Hz 足以覆盖你的 40Hz 发送
        config[0].cfg.acc.range = BMI2_ACC_RANGE_8G;
        config[0].cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
        
        config[1].type = BMI2_GYRO;
        config[1].cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
        config[1].cfg.gyr.range = BMI2_GYR_RANGE_2000;
        config[1].cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
        
        bmi2_set_sensor_config(config, 2, &bmi270_dev);
        bmi2_sensor_enable(sens_list, 2, &bmi270_dev);
        PRINT(">>> BMI270 Sensors Enabled!\r\n");
    } else {
        PRINT(">>> [ERROR] BMI270 Init FAILED: %d\r\n", rslt);
    }
}

void BMI270_Soft_Ping_Test(void) {
    PRINT("\r\n--- Soft I2C Ping Radar ---\r\n");
    IIC_Init();
    
    // 探测 0x68 地址
    IIC_Start();
    IIC_Send_Byte(0x68 << 1); // 发送写命令
    if(IIC_Wait_Ack() == 0) {
        PRINT(">>> [BINGO!] BMI270 is ALIVE at Address 0x68!\r\n");
    } else {
        PRINT(">>> [FAIL] No response at 0x68.\r\n");
    }
    IIC_Stop();

    mDelaymS(10); // 稍微喘口气

    // 探测 0x69 地址 (防止 SDO 悬空或接错)
    IIC_Start();
    IIC_Send_Byte(0x69 << 1); // 发送写命令
    if(IIC_Wait_Ack() == 0) {
        PRINT(">>> [BINGO!] BMI270 is ALIVE at Address 0x69! (Check your SDO pin)\r\n");
    } else {
        PRINT(">>> [FAIL] No response at 0x69.\r\n");
    }
    IIC_Stop();
}

// 辅助函数：带“排空残留”和“过采样平滑”的健壮读取逻辑
static uint16_t sample_channel_robust(uint8_t ch) {
    ADC_ChannelCfg(ch);
    
    // ? 优化 1：给高阻抗传感器充足的电容充电时间（从 2us 放宽到 15us）
    mDelayuS(15); 
    
    // ? 优化 2：“假读”一次，直接丢弃！
    // 目的：用当前通道的电压去冲刷掉内部电容里残留的上一个通道的电荷
    ADC_ExcutSingleConver();
    
    // ? 优化 3：极速过采样求平均（轻量级硬件滤波，抹平底噪）
    uint32_t sum = 0;
    for(int j = 0; j < 4; j++) {
        sum += ADC_ExcutSingleConver();
    }
    
    return (uint16_t)((sum >> 2) + RoughCalib_Value); // 除以 4 取平均，加上校准值
}

void FSR_Sample_All(void) {
    // ? 修复 1：每次从休眠唤醒后，重新初始化 ADC 增益配置（防止休眠导致寄存器丢失）
    ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_1_4);

    // 供电
    GPIOB_SetBits(PIN_FSR_POWER);

    // ? 修复 2：给内部基准电压 (Vref) 充足的“暖机”建立时间
    // 之前只给了 200us，刚从休眠醒来根本不够，必须拉长到 1000us (1ms)
    mDelayuS(1000); 

    // ? 修复 3：找个通道“空跑”两次，强行激活内部的采样保持电容
    ADC_ChannelCfg(ADC_DIRECT_CHANNELS[0]);
    ADC_ExcutSingleConver();
    ADC_ExcutSingleConver();

    // --- 1. 采样直连通道 ---
    for(int i = 0; i < 12; i++) {
        current_adc_values[i] = sample_channel_robust(ADC_DIRECT_CHANNELS[i]); 
    }

    // --- 2. 采样复用通道 ---
    GPIOA_SetBits(PIN_MUX_1314 | PIN_MUX_1516);
    mDelayuS(50); 
    current_adc_values[12] = sample_channel_robust(MUX_COM1_CH); 
    current_adc_values[14] = sample_channel_robust(MUX_COM2_CH); 

    GPIOA_ResetBits(PIN_MUX_1314 | PIN_MUX_1516);
    mDelayuS(50);
    current_adc_values[13] = sample_channel_robust(MUX_COM1_CH); 
    current_adc_values[15] = sample_channel_robust(MUX_COM2_CH); 

    GPIOB_ResetBits(PIN_FSR_POWER);
}

/*********************************************************************
 * PUBLIC FUNCTIONS
 */
void Peripheral_Init()
{
    Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);
    
    FSR_Hardware_Init(); // 硬件初始化
    //BMI270_Soft_Ping_Test(); // <--- 就跑这一个！

    BMI270_Hardware_Init();

    // =======================================================
    // ? ? 新增：初始化 PB0 (PWM6) 呼吸灯硬件
    // =======================================================
    GPIOB_ModeCfg(GPIO_Pin_0, GPIO_ModeOut_PP_5mA); // 配置PB0为推挽输出
    PWMX_CLKCfg(4);                  // PWM时钟 = 60MHz / 4 = 15MHz
    PWMX_CycleCfg(PWMX_Cycle_256);   // 周期256，PWM频率约 58.5kHz，毫无频闪
    PWM6_ActDataWidth(255);          // 初始全高电平 (低电平点亮，所以此时为全暗)
    R8_PWM_OUT_EN |= RB_PWM6_OUT_EN; // 启动 PWM6 输出 (? 正确的寄存器操作)
    // =======================================================

    {
        uint8_t  initial_advertising_enable = TRUE;
        uint16_t desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
        uint16_t desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;

        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
        GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t), &desired_min_interval);
        GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t), &desired_max_interval);
    }

    {
        uint16_t advInt = DEFAULT_ADVERTISING_INTERVAL;
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advInt);
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advInt);
        GAP_SetParamValue(TGAP_ADV_SCAN_REQ_NOTIFY, ENABLE);
    }

    {
        uint32_t passkey = 0; 
        uint8_t  pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
        uint8_t  mitm = TRUE;
        uint8_t  bonding = TRUE;
        uint8_t  ioCap = GAPBOND_IO_CAP_DISPLAY_ONLY;
        GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    GGS_AddService(GATT_ALL_SERVICES);           
    GATTServApp_AddService(GATT_ALL_SERVICES);   
    DevInfo_AddService();                        
    SimpleProfile_AddService(GATT_ALL_SERVICES); 

    GGS_SetParameter(GGS_DEVICE_NAME_ATT, sizeof(attDeviceName), attDeviceName);

    {
        uint8_t charValue1[SIMPLEPROFILE_CHAR1_LEN] = {1};
        uint8_t charValue2[SIMPLEPROFILE_CHAR2_LEN] = {2};
        uint8_t charValue3[SIMPLEPROFILE_CHAR3_LEN] = {3};
        uint8_t charValue4[SIMPLEPROFILE_CHAR4_LEN] = {4};
        uint8_t charValue5[SIMPLEPROFILE_CHAR5_LEN] = {1, 2, 3, 4, 5};

        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR1, SIMPLEPROFILE_CHAR1_LEN, charValue1);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR2, SIMPLEPROFILE_CHAR2_LEN, charValue2);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR3, SIMPLEPROFILE_CHAR3_LEN, charValue3);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, SIMPLEPROFILE_CHAR4_LEN, charValue4);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR5, SIMPLEPROFILE_CHAR5_LEN, charValue5);
    }

    peripheralInitConnItem(&peripheralConnList);
    SimpleProfile_RegisterAppCBs(&Peripheral_SimpleProfileCBs);
    GAPRole_BroadcasterSetCB(&Broadcaster_BroadcasterCBs);
    tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);

    // ... 在函数最末尾加上这句：启动呼吸灯 TMOS 任务
    tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);
    
    // ? 新增：启动呼吸灯循环事件 (每20ms进一次中断，32 tick * 0.625ms = 20ms)
    tmos_start_task(Peripheral_TaskID, SBP_LED_BREATH_EVT, 32);
}

static void peripheralInitConnItem(peripheralConnItem_t *peripheralConnList)
{
    peripheralConnList->connHandle = GAP_CONNHANDLE_INIT;
    peripheralConnList->connInterval = 0;
    peripheralConnList->connSlaveLatency = 0;
    peripheralConnList->connTimeout = 0;
}

uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events)
{
    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;
        if((pMsg = tmos_msg_receive(Peripheral_TaskID)) != NULL)
        {
            Peripheral_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            tmos_msg_deallocate(pMsg);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & SBP_START_DEVICE_EVT)
    {
        GAPRole_PeripheralStartDevice(Peripheral_TaskID, &Peripheral_BondMgrCBs, &Peripheral_PeripheralCBs);
        return (events ^ SBP_START_DEVICE_EVT);
    }

    if(events & SBP_PERIODIC_EVT)
    {
        if(SBP_PERIODIC_EVT_PERIOD) tmos_start_task(Peripheral_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD);
        performPeriodicTask();
        return (events ^ SBP_PERIODIC_EVT);
    }

    if(events & SBP_PARAM_UPDATE_EVT)
    {
        GAPRole_PeripheralConnParamUpdateReq(peripheralConnList.connHandle,
                                             DEFAULT_DESIRED_MIN_CONN_INTERVAL, DEFAULT_DESIRED_MAX_CONN_INTERVAL,
                                             DEFAULT_DESIRED_SLAVE_LATENCY, DEFAULT_DESIRED_CONN_TIMEOUT, Peripheral_TaskID);
        return (events ^ SBP_PARAM_UPDATE_EVT);
    }

    // ? 解开注释并修改这里！
    if(events & SBP_PHY_UPDATE_EVT)
    {
        PRINT("? Requesting LE Coded PHY (Long Range Mode)...\n");
        // 申请切换到 Coded PHY
        GAPRole_UpdatePHY(peripheralConnList.connHandle, 0, 
                          GAP_PHY_BIT_LE_CODED,    // TX 请求 Coded PHY
                          GAP_PHY_BIT_LE_CODED,    // RX 请求 Coded PHY
                          GAP_PHY_OPTIONS_NOPRE);  // 让底层自己协商 S=2 还是 S=8
        return (events ^ SBP_PHY_UPDATE_EVT);
    }

    if(events & SBP_READ_RSSI_EVT)
    {
        GAPRole_ReadRssiCmd(peripheralConnList.connHandle);
        tmos_start_task(Peripheral_TaskID, SBP_READ_RSSI_EVT, SBP_READ_RSSI_EVT_PERIOD);
        return (events ^ SBP_READ_RSSI_EVT);
    }

    // =======================================================
    // ? ? 新增：接管呼吸灯事件 (绝对不卡协议栈的非阻塞写法)
    // =======================================================
    if(events & SBP_LED_BREATH_EVT)
    {
        static uint8_t breath_dir = 0; // 0=变暗(高电平变长), 1=变亮(高电平变短)
        static int16_t breath_val = 255; 

        if(peripheralConnList.connHandle == GAP_CONNHANDLE_INIT) 
        {
            // --- 未连接：固定低亮度 1.1V ---
            // 因为你测的是1.1V，意味着高电平占空比为 1.1V/3.3V = 1/3 (约等于 85/256)。
            // (提示: 如果你觉得太亮，可以把 85 改成 180 或 200，数值越靠近255越暗)
            PWM6_ActDataWidth(85); 
        } 
        else 
        {
            // --- 已连接：2秒一呼一吸 (100次 * 20ms) ---
            // 步长 = 255 / 50 = 5.1
            if (breath_dir == 0) {
                breath_val += 5;
                if (breath_val >= 255) { breath_val = 255; breath_dir = 1; }
            } else {
                breath_val -= 5;
                if (breath_val <= 0) { breath_val = 0; breath_dir = 0; }
            }
            
            // 为了让呼吸灯柔和、深邃，符合人眼视觉感官，这里做了一个简单的平方非线性映射
            uint8_t duty = (uint8_t)(((uint32_t)breath_val * breath_val) / 255);
            PWM6_ActDataWidth(duty);
        }

        // TMOS 核心魔法：重新启动自己，形成 20ms 的定时闭环！
        tmos_start_task(Peripheral_TaskID, SBP_LED_BREATH_EVT, 32);
        
        return (events ^ SBP_LED_BREATH_EVT);
    }

    return 0;
}

static void Peripheral_ProcessGAPMsg(gapRoleEvent_t *pEvent)
{
    switch(pEvent->gap.opcode)
    {
        case GAP_SCAN_REQUEST_EVENT:
            break;
        case GAP_PHY_UPDATE_EVENT:
            PRINT("Phy update Rx:%x Tx:%x ..\n", pEvent->linkPhyUpdate.connRxPHYS, pEvent->linkPhyUpdate.connTxPHYS);
            break;
        default:
            break;
    }
}

static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    switch(pMsg->event)
    {
        case GAP_MSG_EVENT:
            Peripheral_ProcessGAPMsg((gapRoleEvent_t *)pMsg);
            break;
        case GATT_MSG_EVENT:
        {
            gattMsgEvent_t *pMsgEvent = (gattMsgEvent_t *)pMsg;
            if(pMsgEvent->method == ATT_MTU_UPDATED_EVENT)
            {
                peripheralMTU = pMsgEvent->msg.exchangeMTUReq.clientRxMTU;
                PRINT("mtu exchange: %d\n", peripheralMTU);
            }
            break;
        }
        default: break;
    }
}

static void Peripheral_LinkEstablished(gapRoleEvent_t *pEvent)
{
    gapEstLinkReqEvent_t *event = (gapEstLinkReqEvent_t *)pEvent;
    if(peripheralConnList.connHandle != GAP_CONNHANDLE_INIT)
    {
        GAPRole_TerminateLink(pEvent->linkCmpl.connectionHandle);
        PRINT("Connection max...\n");
    }
    else
    {
        peripheralConnList.connHandle = event->connectionHandle;
        peripheralConnList.connInterval = event->connInterval;
        peripheralConnList.connSlaveLatency = event->connLatency;
        peripheralConnList.connTimeout = event->connTimeout;
        peripheralMTU = ATT_MTU_SIZE;
        
        tmos_start_task(Peripheral_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD);
        tmos_start_task(Peripheral_TaskID, SBP_PARAM_UPDATE_EVT, SBP_PARAM_UPDATE_DELAY);
        tmos_start_task(Peripheral_TaskID, SBP_READ_RSSI_EVT, SBP_READ_RSSI_EVT_PERIOD);
        // ? 【新增这一行】：连接成功后，延时 1000 个 tick (约 625ms) 发起长距离模式切换
        tmos_start_task(Peripheral_TaskID, SBP_PHY_UPDATE_EVT, 1000);
        PRINT("Conn %x - Int %x \n", event->connectionHandle, event->connInterval);
    }
}

static void Peripheral_LinkTerminated(gapRoleEvent_t *pEvent)
{
    gapTerminateLinkEvent_t *event = (gapTerminateLinkEvent_t *)pEvent;
    if(event->connectionHandle == peripheralConnList.connHandle)
    {
        peripheralConnList.connHandle = GAP_CONNHANDLE_INIT;
        peripheralConnList.connInterval = 0;
        peripheralConnList.connSlaveLatency = 0;
        peripheralConnList.connTimeout = 0;
        tmos_stop_task(Peripheral_TaskID, SBP_PERIODIC_EVT);
        tmos_stop_task(Peripheral_TaskID, SBP_READ_RSSI_EVT);

        uint8_t advertising_enable = TRUE;
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advertising_enable);
    }
}

static void peripheralRssiCB(uint16_t connHandle, int8_t rssi)
{
    PRINT("RSSI -%d dB \n", -rssi);
}

static void peripheralParamUpdateCB(uint16_t connHandle, uint16_t connInterval, uint16_t connSlaveLatency, uint16_t connTimeout)
{
    if(connHandle == peripheralConnList.connHandle)
    {
        peripheralConnList.connInterval = connInterval;
        peripheralConnList.connSlaveLatency = connSlaveLatency;
        peripheralConnList.connTimeout = connTimeout;
    }
}

static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    switch(newState & GAPROLE_STATE_ADV_MASK)
    {
        case GAPROLE_STARTED: PRINT("Initialized..\n"); break;
        case GAPROLE_ADVERTISING:
            if(pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT) {
                Peripheral_LinkTerminated(pEvent); PRINT("Advertising..\n");
            } else if(pEvent->gap.opcode == GAP_MAKE_DISCOVERABLE_DONE_EVENT) {
                PRINT("Advertising..\n");
            }
            break;
        case GAPROLE_CONNECTED:
            if(pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT) {
                Peripheral_LinkEstablished(pEvent); PRINT("Connected..\n");
            }
            break;
        case GAPROLE_CONNECTED_ADV: PRINT("Connected Advertising..\n"); break;
        case GAPROLE_WAITING:
            if(pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT) {
                Peripheral_LinkTerminated(pEvent);
            }
            break;
        default: break;
    }
}

static void performPeriodicTask(void)
{
    // 1. 获取最新鲜的 ADC 和 IMU 数据
    FSR_Sample_All();
    struct bmi2_sens_data sensor_data = {0};
    bmi2_get_sensor_data(&sensor_data, &bmi270_dev);
    
    // 2. 实例化我们的极速结构体
    SensorDataPacket_t packet;
    
    // 3. 填充 ADC 数据 (应用左脚映射并减去底噪)
    for(int i = 0; i < 16; i++) {
        packet.adc[i] = (int16_t)(current_adc_values[LEFT_FOOT_MAP[i]] - 1530);
    }
    
    // 4. 填充 IMU 数据 (直接装填，保持原始精度)
    packet.imu[0] = sensor_data.acc.x;
    packet.imu[1] = sensor_data.acc.y;
    packet.imu[2] = sensor_data.acc.z;
    packet.imu[3] = sensor_data.gyr.x;
    packet.imu[4] = sensor_data.gyr.y;
    packet.imu[5] = sensor_data.gyr.z;

    // 5. 将 44 字节纯净数据直接射入蓝牙协议栈！
    peripheralChar4Notify((uint8_t *)&packet, sizeof(SensorDataPacket_t));
    
    // 6. 串口监视 (降频打印，防止串口 TX 堵塞拖慢系统)
    static uint8_t print_cnt = 0;
    if(++print_cnt >= 80) { // 80Hz 下每秒只打印一次
        PRINT("Sent 80 Binary Pkts! L-Foot ACC_X: %d\r\n", packet.imu[0]);
        print_cnt = 0;
    }
}

static void peripheralChar4Notify(uint8_t *pValue, uint16_t len)
{
    attHandleValueNoti_t noti;
    if(len > (peripheralMTU - 3))
    {
        PRINT("Too large noti\n");
        return;
    }
    noti.len = len;
    noti.pValue = GATT_bm_alloc(peripheralConnList.connHandle, ATT_HANDLE_VALUE_NOTI, noti.len, NULL, 0);
    if(noti.pValue)
    {
        tmos_memcpy(noti.pValue, pValue, noti.len);
        if(simpleProfile_Notify(peripheralConnList.connHandle, &noti) != SUCCESS)
        {
            GATT_bm_free((gattMsg_t *)&noti, ATT_HANDLE_VALUE_NOTI);
        }
    }
}

static void simpleProfileChangeCB(uint8_t paramID, uint8_t *pValue, uint16_t len)
{
    // 预留给接收数据的回调
}