# CH583M 鞋垫采集端

CH583M 是整条链路最前端的采集节点，一只鞋垫一颗（CH583M 为 WCH RISC-V + BLE 5.x SoC，NoneOS + TMOS 事件调度，PLL 60 MHz）。它做三件事：一是把 16 路柔性压阻阵列读成数字量——片上 ADC 只够 12 路直连，剩下 4 路靠两片 2:1 模拟开关分时复用（PA11 选通传感器 13/14 走 ADC ch12，PA10 选通 15/16 走 ADC ch13）凑满 16 路；采样时才用 PB18 给阵列上电，采完立刻断电，抑制阵列长期偏压导致的漏电与漂移，每通道先做一次「假读」冲刷采样保持电容里上一通道的残余电荷，再取 4 次过采样平均。二是用片上硬件 I2C（PB12/PB13，400 kHz）挂 Bosch 官方 BMI270 驱动，取加速度 + 陀螺共六轴（各 100 Hz、±8 g / ±2000 dps）。三是把 16×int16 压力 + 6×int16 IMU 打成 44 字节紧凑二进制包（#pragma pack(1)，无帧头、不做浮点、不做 ASCII 化），以 BLE Peripheral 身份通过自定义服务 0xFFE0 的 CHAR4（0xFFE4）Notify 出去，周期 20 个 TMOS tick = 12.5 ms（80 Hz）。左右两只鞋垫各自广播「智能鞋垫L」/「智能鞋垫R」，由 GD32VW553 双 Central 同时连接，两只脚的 44 字节在汇聚端拼成 94 字节帧继续往 GD32F470 / GD32H737 送。链路上还有两个作者刻意加的细节：连接建立 625 ms 后主动请求 LE Coded PHY 换取穿戴场景下的链路余量；PB0 用 PWM6 做呼吸灯，未连接时恒定低亮、连上后 2 秒一呼一吸，做平方非线性映射。左右固件同源，实质差异只有 5 处。

## 硬件配置

> 除注明外，行号均指 `left_insole2.0/APP/peripheral.c`（左脚为基准，注释完好）；右脚同名项的行号在括号里给出（右脚文件比左脚少 13 行左右，因为删掉了软 I2C ping 函数、映射表位置也不同）。

### 引脚

| 功能 | 引脚 / 通道 | 说明 | 文件:行号 |
|---|---|---|---|
| FSR 直连 ADC 通道 ×12 | `CH_EXTIN_0` … `CH_EXTIN_11` | 数组 `ADC_DIRECT_CHANNELS[12]`，对应输出索引 0-11 | `left/APP/peripheral.c:112-116`（右 :111-115）；采样循环 `:381-383` |
| 传感阵列供电脚 | **PB18** | `PIN_FSR_POWER = GPIO_Pin_18`，推挽 5 mA；采样前置高、采完立刻置低（动态供电） | 定义 `:108`（右 :107）；配置输出 `:263`（右 :250）；初始拉低 `:265`；上电 `:369`（右 :329）；断电 `:396`（右 :356）|
| 模拟开关 1 选通脚 | **PA11** | `PIN_MUX_1314 = GPIO_Pin_11`，选通传感器 13/14 → ADC ch12 | 定义 `:109`（右 :108）；配置输出 `:264`（右 :251）；置高/置低 `:386`/`:391`（右 :346/:351）|
| 模拟开关 2 选通脚 | **PA10** | `PIN_MUX_1516 = GPIO_Pin_10`，选通传感器 15/16 → ADC ch13 | 定义 `:110`（右 :109）；同上 `:264`、`:386`/`:391` |
| 复用公共端 1 | `CH_EXTIN_12` | `MUX_COM1_CH`；选通脚为高时→输出索引 12，为低时→索引 13 | 定义 `:118`（右 :117）；采样 `:388`/`:393`（右 :348/:353）|
| 复用公共端 2 | `CH_EXTIN_13` | `MUX_COM2_CH`；选通脚为高时→输出索引 14，为低时→索引 15 | 定义 `:119`（右 :118）；采样 `:389`/`:394`（右 :349/:354）|
| BMI270 I2C SDA | **PB12** | 硬件 I2C；软 ping 引擎里 `IIC_SDA_PIN` 同脚 | `:149`、`:275`（右 :136、:262）|
| BMI270 I2C SCL | **PB13** | 硬件 I2C；软 ping 引擎里 `IIC_SCL_PIN` 同脚 | `:148`、`:275`（右 :135、:262）|
| BMI270 INT | **PB11** | 仅配为上拉输入，**未使用中断**（注释原文：「目前仅作输入，不用中断」）| `:276`（右 :263）|
| 状态呼吸灯 | **PB0**（PWM6，低电平点亮）| 推挽 5 mA + `R8_PWM_OUT_EN \|= RB_PWM6_OUT_EN` | `:414-418`（右 :373-377）；状态机 `:550-583` |
| 调试串口 UART0 TX | **PB7** | 先 `GPIOB_SetBits` 再配推挽输出 | `left/APP/peripheral_main.c:74,76` |
| 调试串口 UART0 RX | **PB4** | 配上拉输入 | `left/APP/peripheral_main.c:75` |

> ⚠ `CH_EXTIN_n` 到物理 PAx/PBx 的对应表在 SDK 的 `StdPeriphDriver/inc/CH58x_adc.h` 和 CH583 数据手册里，**本仓库没有这两份东西**，所以上表只按代码原样给出通道号。PA10/PA11 被当数字输出用、同时代码又在采 `CH_EXTIN_10`/`CH_EXTIN_11`，以及 PB0 被 PWM6 占用、同时又采 `CH_EXTIN_12`/`CH_EXTIN_13` —— 这两处是否撞脚，必须对着原理图核实（已写进 known_issues）。

### 时钟与外设

| 项 | 配置 | 文件:行号 |
|---|---|---|
| 系统时钟 | `SetSysClock(CLK_SOURCE_PLL_60MHz)` → **60 MHz** | `left/APP/peripheral_main.c:67` |
| DC-DC | 使能（`DCDC_ENABLE == TRUE`；`readelf` 显示 `peripheral_main.o` 确实引用了 `PWR_DCDCCfg`）| `left/APP/peripheral_main.c:64-66` |
| 32 kHz 时钟源 | `CLK_OSC32K=2` | `mrs-project/Peripheral.wvproj` defined_symbols[1]（`.cproject` 同）|
| ADC 模式 | 单端外部通道，`ADC_ExtSingleChSampInit(SampleFreq_3_2, ADC_PGA_1_4)`（采样时钟 3.2 分频、PGA 1/4）；**每次采样前重新初始化一次**，防休眠丢寄存器 | `:266`（开机）、`:366`（每周期，右 :326）|
| ADC 粗校准 | 开机做一次 `ADC_DataCalib_Rough()`，结果存 `RoughCalib_Value`，加到每个读数上 | `:267`、变量 `:134`、使用 `:361` |
| 单通道采样时序 | 切通道 → 等 15 µs → 假读 1 次丢弃 → 连续 4 次转换取平均 | `:345-362`（右 :305-322）|
| 整轮采样时序 | 上电 → 等 **1000 µs** Vref 建立 → 空跑 2 次 → 12 路直连 → 选通脚高、等 50 µs、采 2 路 → 选通脚低、等 50 µs、采 2 路 → 断电 | `:364-397`（右 :324-357）|
| 硬件 I2C | `I2C_Init(I2C_Mode_I2C, **400000**, I2C_DutyCycle_16_9, I2C_Ack_Enable, I2C_AckAddr_7bit, 0)` | `:279`（右 :266）|
| BMI270 从地址 | `0x68`（`BMI2_I2C_PRIM_ADDR`）；WCH 库要求地址左移 1 位，适配层里做的 | `:24`、`:42`、`:70` |
| BMI270 突发长度 | `read_write_len = 32` | `:287`（右 :274）|
| 加速度计 | ODR **100 Hz**，量程 **±8 g**，`BMI2_ACC_NORMAL_AVG4` | `:300-302`（右 :287-289）|
| 陀螺仪 | ODR **100 Hz**，量程 **±2000 dps**，`BMI2_GYR_NORMAL_MODE` | `:305-307`（右 :292-294）|
| PWM（呼吸灯）| `PWMX_CLKCfg(4)` → 60 MHz / 4 = **15 MHz**；`PWMX_Cycle_256` → 约 **58.5 kHz**；未连接固定占空 85/256，已连接 5/步、20 ms/步、做平方非线性映射 | `:415-416`、`:560`、`:566-576`（右 :374-375）|
| UART0 | `UART0_DefInit()` → **115200 8N1** | `left/APP/peripheral_main.c:77` |

### BLE 与数据速率

| 项 | 配置 | 文件:行号 |
|---|---|---|
| 角色 | BLE Peripheral（`GAPRole_PeripheralInit()` + `GAPRole_PeripheralStartDevice()`）| `peripheral_main.c:87`、`peripheral.c:509` |
| 发射功率 | `LL_SetTxPowerLevel(LL_TX_POWEER_6_DBM)` → **+6 dBm** | `left/APP/peripheral_main.c:85` |
| 广播间隔 | `DEFAULT_ADVERTISING_INTERVAL 80` × 0.625 ms = **50 ms**（min=max）| `:99`、`:434-436` |
| 期望连接间隔 | `8`–`12` × 1.25 ms = **10–15 ms** | `:101-102`、`:423-430`、`:522-524` |
| Slave latency / 超时 | 0 / `300` × 10 ms = **3 s** | `:103-104` |
| 参数更新延时 | `SBP_PARAM_UPDATE_DELAY 6400` tick × 0.625 ms = **4 s** | `:97`、`:640` |
| RSSI 读取周期 | `SBP_READ_RSSI_EVT_PERIOD 3200` tick = **2 s** | `:96`、`:543` |
| PHY 切换 | 连上后 `1000` tick ≈ **625 ms** 主动请求 `GAP_PHY_BIT_LE_CODED`（收发都要），`GAP_PHY_OPTIONS_NOPRE` 让底层自选 S=2/S=8 | `:529-537`、`:643` |
| **采样 / 发包周期** | `SBP_PERIODIC_EVT_PERIOD 20` tick × 0.625 ms = **12.5 ms → 80 Hz**（TMOS tick = 0.625 ms，由 `:483` 的「32 tick × 0.625 ms = 20 ms」注释反推确认）| `:95`（右 :94，注释写错成 40 Hz）、`:515`、`:639` |
| 广播名 | 扫描响应包内嵌 UTF-8 字节「智能鞋垫」（`E6 99 BA / E8 83 BD / E9 9E 8B / E5 9E AB`）+ `'L'` 或 `'R'`，AD 长度字节 0x0E | 左 `:222-233`（'L' 在 :228）/ 右 `:209-220`（'R' 在 :215）|
| GAP 设备名属性 | 仍是 WCH 默认 `"Simple Peripheral"`（与广播名不一致，见 known_issues）| `:240`（右 :227）|
| 配对 | `GAPBOND_PAIRING_MODE_WAIT_FOR_REQ`、MITM=TRUE、bonding=TRUE、`IO_CAP_DISPLAY_ONLY`、passkey **0** | `:440-451` |
| 服务 / 特征 | Service UUID **0xFFE0**，Notify 特征 CHAR4 UUID **0xFFE4** | `Profile/include/gattprofile.h:36,42`（WCH 原件，未收录）|
| **数据包格式** | `#pragma pack(1) struct { int16_t adc[16]; int16_t imu[6]; }` = **44 字节**，小端；ADC 段先过映射表再减固定底噪 1530；IMU 段依次 acc.x/y/z、gyr.x/y/z 原始值 | 结构体 `:29-34`；填充 `:707-731`（右 :666-700）|
| 通道映射表 | 左：`LEFT_FOOT_MAP[16] = {3,4,2,5,0,11,8,1,15,7,10,14,6,9,12,13}`（文件作用域）；右：`TX_SENSOR_MAP[16] = {1,5,4,0,15,10,11,14,8,7,13,2,6,12,3,9}`（函数内 static）| 左 `:140-143`；右 `:678-681` |
| 底噪常量 | 所有 16 路、左右脚统一减 **1530** | 左 `:719`；右 `:688` |
| MTU | `peripheralMTU` 初值 `ATT_MTU_SIZE`(23)，每次建链复位；仅被动记录对端的 `ATT_MTU_UPDATED_EVENT`。发送前校验 `len > peripheralMTU - 3` 则丢包 | `:126`、`:612-615`、`:637`、`:744-748` |
| BLE 缓冲 / 堆 | `BLE_BUFF_MAX_LEN=251`；`MEM_BUF[BLE_MEMHEAP_SIZE/4]`，实测 **6144 字节** | `.cproject` defined_symbols；`peripheral_main.c:30` + `readelf -sW obj/APP/peripheral_main.o` |

## 编译与烧录

## 工具链

| 项 | 值 | 出处 |
|---|---|---|
| IDE | MounRiver Studio（MRS，Eclipse CDT 系）| `.project` 的 `com.mounriver.*` natures；`Peripheral.launch:2` `com.mounriver.debug.gdbjtag.openocd.launchConfigurationType` |
| 编译器 | MRS 自带 `RISC-V Embedded GCC`，前缀 `riscv-none-embed-` | `.cproject`（`value="riscv-none-embed-"`，`target.rvGcc.8`）|
| 架构 | rv32i + RVM/RVC/RVA 扩展，ilp32、无 FPU | `.cproject`：`target.arch.rv32i`、`abi.integer.ilp32`、`abi.fp.none`、`isa.fp.none` |
| 优化 / 标准 | `-Os`、`gnu99`（C++ 侧 gnu++11）、`-ffunction-sections -fdata-sections -fno-common -fsigned-char` | `Peripheral.wvproj` buildConfig/optimization、ccompiler/optimization |
| 链接 | `-nostartfiles`、`--specs=nosys.specs`、`--gc-sections`、`--print-memory-usage`、picolibc disabled | `.cproject` / `Peripheral.wvproj` clinker |
| 链接脚本 | `${project}/Ld/Link.ld`（来自 SDK 的 `EVT/EXAM/SRC/Ld/Link.ld`）| `Peripheral.wvproj` clinker/general/scriptFiles[0] |
| 链接库 | `libISP583.a`、`libCH58xBLE.a` | `Peripheral.wvproj` clinker/libraries |
| 目标 | CH583M，NoneOS（无 RTOS，靠 BLE 库自带的 TMOS 事件调度）| `.template`：`MCU=CH583M`、`RTOS=NoneOS` |
| 全局宏 | `DEBUG=1`、`CLK_OSC32K=2`、`BLE_BUFF_MAX_LEN=251` | `Peripheral.wvproj` ccompiler/preprocessor/defined_symbols |
| include 路径 | `${project}/` 下的 `Startup`、`APP/include`、`Profile/include`、`StdPeriphDriver/inc`、`HAL/include`、`Ld`、`LIB`、`RVMSIS` | `Peripheral.wvproj` ccompiler/includes |
| 排除编译 | `HAL/Profile`、`HAL/KEY.c`、`HAL/LED.c`（另有 12 条 `CH57x_*.c` 是模板残留，见 known_issues）| `Peripheral.wvproj` excludeResources |

## SDK 不在本仓库里 —— 必须自己去下

**这一点是复现本子系统最大的坑：CH583 SDK 本体一个文件都没进仓库。** 证据：`obj/APP/peripheral.d` 的依赖列表全部指向作者 Windows(WSL) 机器上的
`d:/<中文目录>/code/insole/ch583/EVT/EXAM/BLE/HAL/include/CONFIG.h`、
`.../EVT/EXAM/SRC/StdPeriphDriver/inc/CH58x_*.h`、`.../EVT/EXAM/BLE/LIB/CH58xBLE_LIB.H` —— 也就是说 `ch583/EVT/` 整个目录在仓库之外。

需要的是 **WCH 官方 CH583 EVT 评估包**（内含 `EVT/EXAM/SRC/StdPeriphDriver`（CH58x 外设库 + libISP583.a）、`EVT/EXAM/SRC/{Ld,RVMSIS,Startup}`、`EVT/EXAM/BLE/{HAL,LIB}`（libCH58xBLE.a + CONFIG.h）、以及 `EVT/EXAM/BLE/Peripheral` 例程）。两个获取渠道：

* WCH 官网 CH583 产品页的「EVT 评估开发系统」压缩包（`CH583EVT.ZIP`）；
* GitHub 上 WCH 自己维护的 `openwch/ch583` 仓库（同一份 EVT 目录树）。

**版本对齐依据**：本仓库里那 4 个未改动的 WCH 例程文件（`Profile/gattprofile.c/.h`、`Profile/devinfoservice.c/.h`）时间戳全部是 **2023-08-07 10:03**，说明作者用的是 2023 年 8 月前后那一版 EVT。拿到 SDK 后，第一次上电看串口横幅里 `PRINT("Lib Version: %s\n", VER_LIB)`（`peripheral_main.c:81`）打出来的 BLE 库版本号，把它记进你自己的 README，后人就不用猜了。

## 作者代码怎么放进 SDK

`.project` 的 linkedResources 把 `HAL`→`PARENT-1-PROJECT_LOC/HAL`、`LIB`→`PARENT-1-PROJECT_LOC/LIB`、`Ld`/`RVMSIS`/`Startup`/`StdPeriphDriver`→`PARENT-2-PROJECT_LOC/SRC/*` 全部写成了相对路径，**所以工程目录必须恰好放在 `ch583/EVT/EXAM/BLE/<工程目录>/`，上一级是 `BLE/`，上两级是 `EXAM/`**。放错位置 MRS 会报一堆 linked folder 找不到。

左脚（右脚同理，把 `left` 换成 `right`）：

```bash
# 0. 解压 EVT 包，得到 .../ch583/EVT/
# 1. 以 WCH 的 Peripheral 例程为底座复制一份 —— 这一步的目的是白拿 Profile/ 和 APP/include/peripheral.h
cp -r ch583/EVT/EXAM/BLE/Peripheral  ch583/EVT/EXAM/BLE/left_insole2.0

cd ch583/EVT/EXAM/BLE/left_insole2.0
# 2. 用本仓库的作者源码覆盖 APP/ 下的两个 .c
cp <repo>/firmware/ch583-insole/left/app/peripheral.c        APP/peripheral.c
cp <repo>/firmware/ch583-insole/left/app/peripheral_main.c   APP/peripheral_main.c
#    如果例程底座里还留着 main.c 之类的旧入口，从构建里排除掉，避免 main 重定义

# 3. 放 Bosch BMI270 驱动（本仓库随附，BSD-3-Clause）
cp <repo>/firmware/ch583-insole/third-party/bosch-bmi270/*.{c,h}  APP/

# 4. 打 SDK 补丁：往 APP/include/peripheral.h 加一行事件号（详见 patches 字段）
#    #define SBP_LED_BREATH_EVT      0x0020

# 5. 覆盖工程脚手架（左右两份逐字节相同，仓库里只存了一份）
cp <repo>/firmware/ch583-insole/mrs-project/{.cproject,.project,.template,Peripheral.wvproj,Peripheral.launch} .

# 6. Profile/ 保持 SDK 原样，不要动
```

然后 MRS 里 `File → Open Projects from File System…` 选到 `left_insole2.0`，`Project → Build All`。产物是 `obj/Peripheral.elf` 和 `obj/Peripheral.hex`。

**参考体积**（`size -A` 读仓库里现成的左脚 ELF，`-Os` + `DEBUG=1`）：代码 `.highcode` 8240 B + `.text` 158512 B + `.highcode_lp` 196 B ≈ 167 KB；RAM `.data` 1208 B + `.bss` 7804 B + `.stack` 512 B ≈ 9.5 KB，其中 `MEM_BUF`（BLE 协议栈堆）单独占 6144 B。

## 烧录

照 `.template` 里作者的配置来：WCH-Link，目标 `obj/Peripheral.hex`，起始地址 `0x00000000`，Erase All + Program + Verify + Reset 全开，`SDIPrintf=false`（所以调试口不占用，串口打印走的是真 UART0/PB7）。MRS 里点 `Flash Download` 即可；没有 WCH-Link 的话用 WCHISPTool 走 CH583 自带 USB/串口 BootLoader 烧同一个 hex。

在线调试：`Peripheral.launch` 已配好 OpenOCD（`${eclipse_home}toolchain/OpenOCD/bin/` 的 `wch-riscv.cfg`，gdb server TCL 端口 6666）+ `riscv-none-embed-gdb`，全部走 `${eclipse_home}` 变量，换机器不用改。

## 看数据

串口：**PB7 = TX，115200 8N1**（`UART0_DefInit()` 默认波特率，`peripheral_main.c:77`）。上电应看到 `CH583M UART0 Redirect OK!` + `Lib Version: ...`，BMI270 初始化成功打 `>>> BMI270 Driver Load SUCCESS!`，连上后每秒一行 `Sent 80 Binary Pkts! L-Foot ACC_X: <值>`。BMI270 死了会打 `>>> [ERROR] BMI270 Init FAILED: <码>`；这时可以把 `peripheral.c:407` 那行 `//BMI270_Soft_Ping_Test();` 的注释解开，用软 I2C 依次探 0x68 / 0x69 两个地址查是不是 SDO 悬空（右脚固件里这个函数已被删掉，需要从左脚拷回来）。

蓝牙：手机 nRF Connect 搜「智能鞋垫L」/「智能鞋垫R」→ 连接 → **先做 MTU 交换（要 ≥ 47）** → 订阅 Service `0xFFE0` / Characteristic `0xFFE4` 的 Notify，就能收到 44 字节裸包（前 32 字节 = 16×int16 压力，后 12 字节 = 6×int16 IMU，小端）。不做 MTU 交换会一个包都收不到，只在串口刷 `Too large noti`（原因见 known_issues 第 1 条）。

## 建议：把左右两份合成一份

左右两个工程 diff 下来实质差异只有 5 处，**其余 190 行差异全是编码损坏**，维护两份纯属负担。建议合并成单个 `firmware/ch583-insole/`，用一个编译宏切左右：

```c
/* app/include/insole_config.h —— 新增 */
#if !defined(INSOLE_LEFT) && !defined(INSOLE_RIGHT)
#  error "编译时请二选一：-DINSOLE_LEFT 或 -DINSOLE_RIGHT"
#endif

#ifdef INSOLE_LEFT
#  define INSOLE_SIDE_CHAR   'L'
#  define INSOLE_SIDE_STR    "L-Foot"
#  define INSOLE_MAC_LAST    0x02
#  define INSOLE_SENSOR_MAP  { 3, 4, 2, 5, 0, 11, 8, 1, 15, 7, 10, 14, 6, 9, 12, 13 }
#else
#  define INSOLE_SIDE_CHAR   'R'
#  define INSOLE_SIDE_STR    "R-Foot"
#  define INSOLE_MAC_LAST    0x03
#  define INSOLE_SENSOR_MAP  { 1, 5, 4, 0, 15, 10, 11, 14, 8, 7, 13, 2, 6, 12, 3, 9 }
#endif
```

五个替换点：`scanRspData` 末字节用 `INSOLE_SIDE_CHAR`（左 :228 / 右 :215）；映射表统一提到文件作用域 `static const uint8_t SENSOR_MAP[16] = INSOLE_SENSOR_MAP;`（左 :140 / 右 :678）；`MacAddr[6]` 末字节用 `INSOLE_MAC_LAST`（`peripheral_main.c:33`）；打印串用 `INSOLE_SIDE_STR`（左 :736 / 右 :703）；软 I2C ping 引擎以左脚为准整块保留（右脚是删掉的一方，保留不影响右脚行为，也顺手修掉右脚那一堆 `-Wunused-function`）。落地方式：在 MRS 里建两个 Build Configuration（left / right），各自的 `defined_symbols` 里加 `INSOLE_LEFT` / `INSOLE_RIGHT`，其余设置共用——反正两份 `.cproject` 本来就逐字节相同。

若不想承担改动风险（作者已无法回到实验环境复测），退一步的方案是**保留两个目录但把公共部分抽出去**：`third-party/bosch-bmi270/` 和 `mrs-project/` 各存一份共用，`left/app/` 与 `right/app/` 只放各自的 `peripheral.c` + `peripheral_main.c`，并在 `firmware/ch583-insole/DIFF.md` 里逐条写清那 5 处差异（本文段可直接复用）。这也是本清单 include 字段采用的布局。

## 本目录未收录哪些文件，为什么

本子系统磁盘上共 216 个文件 / 17 MB，收录 14 个 / 约 830 KB，排除 202 个 / 约 16.2 MB。分类如下：

1) **编译产物 obj/（166 个文件，15 MB，占 88%）** — 左右各一套 `obj/`，含 .o .d .elf .hex .lst .map 以及 MRS 生成的 makefile / objects.mk / sources.mk / subdir.mk。全部排除。额外理由（隐私）：`obj/APP/peripheral.d`、`obj/Peripheral.map` 里嵌着作者机器的绝对路径 `d:/<中文目录>/code/insole/ch583/EVT/EXAM/...`，开源前必须去掉；已单独核查 5 个工程脚手架文件（.cproject/.project/.template/.wvproj/.launch）内**不含**任何此类路径，可以安全分发。

2) **WCH SDK 原版 Profile/（8 个文件，约 100 KB）** — `Profile/gattprofile.c`(23784 B)、`Profile/devinfoservice.c`(18997 B)、`Profile/include/gattprofile.h`、`Profile/include/devinfoservice.h`，左右各一份。判定为未改动 WCH 原件：文件头是完整的 `(C) COPYRIGHT ... Author: WCH ... Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.`，修改时间全部落在 2023-08-07 10:03:32~10:03:58 的 SDK 批量时间戳里，且左右两份 md5 完全一致（gattprofile.c = f545e7648dae12983e7168bb091e6106，devinfoservice.c = 889d9d55ccd03d5cccef874589085609）。这 4 个文件解压 SDK 的 Peripheral 例程就有，不再分发。

3) **APP/include/peripheral.h（2 个文件，约 4 KB）** — 不整份收录，改走 patches。它是 WCH 例程头文件（文件头 1-11 行是 WCH/南京沁恒版权块，Date: 2018/12/11），作者只加了 1 行 `#define SBP_LED_BREATH_EVT 0x0020`；左右两份唯一差异是右脚多了一个空行（左 68 行 / 右 69 行）。

4) **Bosch 驱动的右脚重复副本（5 个文件，772 KB）** — right_insole2.0/APP/bmi2.c、bmi2.h、bmi2_defs.h、bmi270.c、bmi270.h 与左脚逐个 md5 一致，只保留一份共用。

5) **工程脚手架的右脚重复副本（5 个文件，约 100 KB）** — right_insole2.0 的 .cproject / .project / .template / Peripheral.wvproj / Peripheral.launch 与左脚 `diff` 结果完全为空（逐字节相同），只保留一份。

6) **IDE 缓存 .mrs/（8 个文件，52 KB）** — `*-.snapshot` 是 MounRiver 的编辑器/工程快照缓存，`Peripheral.mrs-workspace` 是工作区状态，与构建无关。

7) **Eclipse 偏好 .settings/（8 个文件，64 KB）** — org.eclipse.cdt.codan.core.prefs(2020-06-06)、org.eclipse.cdt.ui.prefs(2022-01-26)、org.eclipse.core.resources.prefs(2023-07-06) 三个是 MRS 装机自带的原始时间戳，非作者产物；language.settings.xml(2025-08-08) 是 scanner discovery 索引缓存，会由 IDE 自动重建。

说明：本子系统里**没有**发现 __pycache__、`*副本*`、`*备份*`、`*.bak`、Keil/IAR 中间产物（.crf .lst .axf .uvoptx .ewt 等）——这些出现在 GD32 那几个子系统里，CH583 这边是 MRS/GCC 工程。

---

已知问题见 [`docs/KNOWN-ISSUES.md`](../../docs/KNOWN-ISSUES.md)，
需打进 SDK 的改动见 [`docs/BUILD-PATCHES.md`](../../docs/BUILD-PATCHES.md)。
