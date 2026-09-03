# GD32H737 端侧推理

GD32H737VMT6 是整条链路的最后一站，也是唯一的"算子"。它通过 USART1（PD5/PD6，1.5 Mbps，DMA0 通道 0 环形接收）从 GD32F470 汇聚节点拿到 94 字节双足合并包（1 字节头 0xAA + 4 字节时间戳 + 左 16 路 ADC + 左 6 轴 IMU + 右 16 路 ADC + 右 6 轴 IMU + 1 字节包尾），把每帧 44 个通道按 ADC×0.002f、IMU×0.00024414f 缩放后写进一个 45×44 的二维环形帧池；攒满 45 帧（30 Hz 下 1500 ms 窗口）就做一次"阴阳装箱"——先把 45 帧的 32 路 ADC 连续铺成 1440 个 float，再把 45 帧的 12 轴 IMU 铺成 540 个 float，拼成 1980 维一维张量喂给兆易 GD_LIB 的双流 1D-CNN（STRIDED_SLICE 把张量劈成 ADC/IMU 两支，各走 CONV_2D→MAXPOOL×2，CONCATENATION 后接两层 FULLY_CONNECTED 和 SOFTMAX，共 14 个算子、74 920 字节权重），取 8 类置信度的 argmax，然后把结果索引覆写到转发包的包尾字节，沿 USART1 原路回传给 F470 去点屏和报警。窗口每次前滑 15 帧，即每 500 ms 出一个结论。全部推理在 Cortex-M7 上以 float32 完成，I/D Cache 均打开，主频 600 MHz 由内部 IRC64M 倍频得到。

## 硬件配置

> 所有数值均从代码读出，路径相对 ROOT。`main.c` = `4.工程代码/1.GD32H737VMT6程序/h737vmt6_AI1.0/main.c`；`it.c` = 同目录 `gd32h7xx_it.c`；`nn.h` / `nn.c` = `4.工程代码/1.GD32H737VMT6程序/GD32H7_study/GD32_H7_AI/1.0/nn_model_configure.h` / `.c`；`system.c` = `GD32H7_study/GD32H7xx_Demo_Suites_V2.1.0/GD32H7xx_Firmware_Library/CMSIS/GD/GD32H7xx/Source/system_gd32h7xx.c`。

### 芯片与时钟

| 项 | 值 | 出处 |
|---|---|---|
| MCU | GD32H737VM（Cortex-M7 + 双精度 FPU） | `GD32H737_Base.uvprojx` `<Device>` / `<Cpu>` |
| 系统主频 | **600 MHz**，PLL0 输入源为**内部 IRC64M**（不用外部晶振） | `system.c:43`（作者放开的 `__SYSTEM_CLOCK_600M_PLL0_IRC64M`）；`system.c:89-105` 的 #if 链决定优先级 |
| 内核供电 | 强制 LDO 模式 `pmu_smps_ldo_supply_config(PMU_LDO_SUPPLY)` | `system.c:215`（作者插入） |
| I-Cache / D-Cache | 均使能 | `main.c:128` `SCB_EnableICache()`、`main.c:129` `SCB_EnableDCache()` |
| NVIC 分组 | `NVIC_PRIGROUP_PRE4_SUB0`（4 位抢占、0 位子优先级） | `main.c:127` |
| Flash | `0x08000000`，3840 KB | `uvprojx <Cpu> IROM(0x08000000,0x03C0000)`；`Objects/GD32H737_Base.sct:5-6` |
| RAM（RW+ZI 落点） | AXI SRAM `0x24000000` 832 KB；ITCM `0x00000000` 64 KB | `sct:12`、`sct:15` |
| 其他可用 RAM（未用于 RW） | DTCM `0x20000000` 128 KB、`0x30000000` 16 KB、`0x30004000` 16 KB | `uvprojx <Cpu>` XRAM/XRAM2/XRAM3 |
| 栈 / 堆 | Stack 0x1000（4 KB）、Heap 0x800（2 KB），SDK 默认未改 | `.../CMSIS/GD/GD32H7xx/Source/ARM/startup_gd32h7xx.s:33` / `:44` |

### 串口（唯一对外接口，收发同一个 USART）

| 项 | 值 | 出处 |
|---|---|---|
| 外设 | **USART1** | `main.c:103` `rcu_periph_clock_enable(RCU_USART1)` |
| TX 引脚 | **PD5**，AF7，推挽，上拉，`GPIO_OSPEED_60MHZ` | `main.c:105-107` |
| RX 引脚 | **PD6**，AF7，推挽，上拉，`GPIO_OSPEED_60MHZ` | `main.c:109-111` |
| 波特率 | **1 500 000 bps**（1.5 Mbps） | `main.c:114` `usart_baudrate_set(USART1, 1500000U)` |
| 数据格式 | 8N1（未调 word length / parity / stop，用复位默认） | `main.c:113-121`（只调 deinit + baudrate + 收发使能） |
| 收方向 | DMA 接收使能 | `main.c:119` `usart_dma_receive_config(USART1, USART_RECEIVE_DMA_ENABLE)` |
| 发方向 | 纯 CPU 轮询逐字节发，每字节后忙等 TBE 再空转 150 次 nop | `main.c:245-249` |
| 溢出恢复 | 主循环检测 `USART_FLAG_ORERR` → 清标志、读走 RDATA、停 DMA、清 FTF、重置传输数为 94、重开 DMA | `main.c:160-168` |

### DMA

| 项 | 值 | 出处 |
|---|---|---|
| 控制器 / 通道 | **DMA0 通道 0**（另使能 DMAMUX 时钟） | `main.c:80-81`、`main.c:85` |
| 请求源 | `DMA_REQUEST_USART1_RX` | `main.c:95` |
| 方向 / 宽度 | 外设→内存，8 bit | `main.c:89`、`main.c:92` |
| 传输长度 | **94 字节** | `main.c:88` `dma_init.number = 94` |
| 模式 | **循环（circular）**，内存地址自增，外设地址不增 | `main.c:94`、`main.c:91`、`main.c:90` |
| 优先级 | `DMA_PRIORITY_HIGH` | `main.c:93` |
| 中断 | 传输完成 FTF；`DMA0_Channel0_IRQn` 抢占 0 / 子 0 | `main.c:97`、`main.c:120` |
| ISR 动作 | 清 FTF 标志，置 `data_ready = 1`（`volatile uint8_t`，定义在 it.c） | `it.c:16-23`、`it.c:13` |
| 缓冲区 | `rx_buffer[96]` / `tx_buffer[96]`，均 `__attribute__((aligned(32)))`（为 D-Cache 行对齐） | `main.c:47-48` |
| Cache 一致性 | 每帧 `SCB_InvalidateDCache_by_Addr(rx_buffer, 96)` | `main.c:174` |

### 上电同步时序（自定义"护城河"）

| 步骤 | 值 | 出处 |
|---|---|---|
| 1. 空转等待 | `for(volatile int delay=0; delay<200000000; delay++)`（约 1.3 s @600 MHz，注释写"傻等 2 秒"） | `main.c:147` |
| 2. 抓总线静默期 | 连续 1 000 000 次读到 PD6（RX 脚）为高才认为总线空闲，中间只要变低就清零重数 | `main.c:148-152` |
| 3. 排雷 | 清 ORERR、清 FERR、空读一次 RDATA | `main.c:153-156` |
| 4. 启动 | `dma_channel_enable(DMA0, DMA_CH0)` | `main.c:157` |

### 数据包格式（94 字节，`#pragma pack(1)`）

| 偏移 | 字段 | 类型 / 长度 | 出处 |
|---|---|---|---|
| 0 | `header`，固定 `0xAA` | uint8 ×1 | `main.c:52`；校验 `main.c:177`；回传时重写 `main.c:235` |
| 1 | `timestamp` | uint32 ×1（4 B） | `main.c:53`；**回传时被 `debug_frame_count` 覆写** `main.c:236` |
| 5 | `left_adc[16]` | uint16 ×16（32 B） | `main.c:54` |
| 37 | `left_imu[6]`（AX AY AZ GX GY GZ） | int16 ×6（12 B） | `main.c:55` |
| 49 | `right_adc[16]` | uint16 ×16（32 B） | `main.c:56` |
| 81 | `right_imu[6]` | int16 ×6（12 B） | `main.c:57` |
| 93 | `tail` | uint8 ×1 | `main.c:58`；**推理结果覆写点** `main.c:237-241` |
| — | 合计 | **94 字节**（1+4+32+12+32+12+1） | 与 `main.c:88` 的 DMA number 94、`main.c:232/245` 的 memcpy/发送长度一致 |

包尾语义：有结果时写 argmax 索引 0–7（`main.c:238`）；开机还没出过结果时写占位 `0x55`（`main.c:240`，下游 GD32F470 用 `g_ai_result <= 7` 过滤掉它）。

### 推理链参数（全部实读）

| 项 | 值 | 出处 |
|---|---|---|
| 窗口长度 `WINDOW_FRAMES` | **45 帧** = 1500 ms @ 30 Hz | `main.c:33` |
| 每帧特征数 `FEATURES_PER_FRAME` | **44**（32 路 ADC + 12 轴 IMU） | `main.c:34` |
| 总特征 `TOTAL_FEATURES` | **1980** = 45×44 | `main.c:35`；与 `nn.h:30` `INPUT_SIZE 1980` 一致 |
| 推理步长 `INFERENCE_STEP` | **15 帧** = 500 ms @ 30 Hz | `main.c:36` |
| ADC 特征块 `ADC_FEATURES` | **32**（左 16 + 右 16） | `main.c:41` |
| IMU 特征块 `IMU_FEATURES` | **12**（左 6 + 右 6） | `main.c:42` |
| **ADC 缩放** | **`× 0.002f`** | `main.c:181`（right_adc）、`main.c:182`（left_adc） |
| **IMU 缩放** | **`× 0.00024414f`**（= 1/4096；训练说明写的是 0.000066，不一致，见 known_issues） | `main.c:183`（right_imu）、`main.c:184`（left_imu） |
| 帧池 | `static float frame_buffer[45][44]`（7920 B） | `main.c:63` |
| 写帧顺序（**注意是右脚在前**） | right_adc[0..15] → left_adc[0..15] → right_imu[0..5] → left_imu[0..5] | `main.c:181-184` |
| **"阴阳装箱"第一块（ADC 整块）** | 外层 45 帧、内层 32 通道，连续铺 **1440** 个 float 到 `ai_input_buffer[0..1439]` | **`main.c:193-198`** |
| **"阴阳装箱"第二块（IMU 整块）** | 外层 45 帧、内层 12 通道（取 `frame_buffer[frame][32+m]`），续铺 **540** 个 float 到 `ai_input_buffer[1440..1979]` | **`main.c:200-205`** |
| 装箱理由 | Edge Impulse 把两个 Raw Data 模块首尾拼接，训练侧对应 `Lambda t[:, 0:672]` / `t[:, 672:924]` 的切片解耦 | `main.c:38-42` 注释；训练侧 `4.工程代码/6.模型训练代码/Neural_network_architecture.py:29-34` |
| 触发条件 | `current_frame_count >= 45` | `main.c:189` |
| 调用 | `nn_model_invoke(&finnal1)` | `main.c:208` |
| 判决 | 遍历 8 个 float 取 argmax，结果存 `latest_ai_result`（uint8，初值 0xFF） | `main.c:210-221`、`main.c:68` |
| 窗口前滑 | 保留 30 帧：`memmove(frame_buffer[0], frame_buffer[15], 30*44*4)`，`current_frame_count = 30` | `main.c:225-227` |
| 发送前固定延时 | `for(volatile int delay=0; delay<200000; delay++)` | `main.c:243` |

### 模型（GD_LIB 描述）

| 项 | 值 | 出处 |
|---|---|---|
| 输入 | 1980 个 float32 = 7920 B（`user_input_size = TOTAL_FEATURES*4`） | `nn.h:30-31`；`main.c:133-134` |
| 输出 | 8 个 float32 = 32 B（`user_output_size = 8*4`） | `nn.h:35-38`；`main.c:139` |
| 算子链（14 个） | `STRIDED_SLICE, CONV_2D, MAX_POOL_2D, CONV_2D, MAX_POOL_2D, STRIDED_SLICE, CONV_2D, MAX_POOL_2D, CONV_2D, MAX_POOL_2D, CONCATENATION, FULLY_CONNECTED, FULLY_CONNECTED, SOFTMAX` — 两个 STRIDED_SLICE 把 1980 维劈成 ADC/IMU 双流，各走两组 conv+pool，CONCATENATION 汇合后两层全连接 + softmax | `nn.c:21-27`（回调数组）、`nn.c:30-35`（算子名表） |
| 权重 | `model_paras_arr[33960]` + `model_paras_data[40960]` = **74 920 B**，两块都放 RAM（`MODEL_ARR_MAPPING_IN_FLASH 0` / `MODEL_DATA_MAPPING_IN_FLASH 0`） | `nn.c:79`、`nn.c:1784`；`nn.h:109`、`nn.h:112` |
| 峰值工作区 | `static_buffer_peak[13344]`（32 字节对齐）、`ai_layer_mem[1180]` | `nn.h:56`、`nn.h:58`；`nn.c:41`、`nn.c:52` |
| 数据类型 | float32（`OUT_TYPE 0`，`optimize_level 0`，`using_sdram 0`，无 LSTM） | `nn.h:45`；`nn.c:65`、`nn.c:67`、`nn.c:70` |
| 模型名 / 平台名 | `"finnal4"` / `"GD32H759I_EVAL"`（平台名与实际板子不符，见 known_issues） | `nn.c:37`、`nn.c:38` |
| 静态占用（链接结果） | RO 102 452 B（100.05 kB）、RW+ZI 112 808 B（110.16 kB）、ROM 177 508 B（173.35 kB） | `h737vmt6_AI1.0/Listings/GD32H737_Base.map:3185-3187` |

### 8 类输出的索引 → 标签映射

H737 只回传裸索引，标签表在下游 GD32F470 里（`4.工程代码/2.GD32F470ZGT6程序/final_wireless_v16.0/Hardware/lcd_my_test/lcd_mytest.c:31-34` 中文、`:36-39` 英文），顺序是 Edge Impulse 的字母序：

| 索引 | 英文 | 中文 | 数据集里对应的采集文件名前缀 |
|---|---|---|---|
| 0 | ANTALGIC | 痛性跛行 | `unnormal_singol*` |
| 1 | BLIND PROBE | 盲态探步 | `eye*` |
| 2 | DOWNSTAIRS | 正在下楼 | `downstair*` |
| 3 | HEMIPLEGIC | 偏瘫步态 | `band*` |
| 4 | SITTING | 静止坐立 | `sit*` |
| 5 | STANDING | 静止站立 | `stand*` |
| 6 | UPSTAIRS | 正在上楼 | `upstair*` |
| 7 | WALKING | 正常走路 | `normal_walk*` |

（采集文件清单：`4.工程代码/6.模型训练代码/采集的数据/`，共 57 个 CSV；CSV 列头为 `timestamp,L_ADC_1..L_ADC_16,R_ADC_1..R_ADC_16,L_IMU_AX..L_IMU_GZ,R_IMU_AX..R_IMU_GZ`。）

### 准确率

**95.5%**，唯一来源是答辩 PPT 自述的 Edge Impulse 验证集结果，且数据集为**单被试单日采集**。仓库里不存在任何推理延迟、功耗、续航或成本的实测数据（板上计时通路已被 `main.c:74-76` 打桩成恒 0）。

## 编译与烧录

## 编译与烧录（GD32H737 推理固件）

### 1. 工具链

| 项目 | 要求 | 出处 |
|---|---|---|
| IDE | Keil MDK-ARM 5.x（uVision） | `GD32H737_Base.uvprojx` → `<ToolsetName>ARM-ADS` |
| 编译器 | **ARM Compiler 6（AC6）**，不能用 AC5 | uvprojx → `<uAC6>1` |
| 优化等级 | `-O2` | uvprojx → `<Optim>2` |
| C 库 | 标准库（**不**勾 MicroLIB） | uvprojx → `<useUlib>0` |
| 器件 | `GD32H737VM`，Cortex-M7 + 双精度 FPU | uvprojx → `<Device>GD32H737VM`、`<Cpu>... FPU3(DFPU)` |
| 器件支持包 | `GigaDevice.GD32H7xx_DFP.1.4.0` | uvprojx → `<PackID>` |
| 烧写算法 | `GD32H7xx_3840KB.FLM`，起始 `0x08000000`，长度 `0x3C0000` | uvprojx → `<FlashDriverDll>UL2CM3(... -FF0GD32H7xx_3840KB -FS08000000 -FL03C0000)` |
| 调试口 | SWD（ARM CoreSight SW-DP，IDCODE 0x0BD12477），ULINK2/CMSIS-DAP/GD-Link 均可 | `GD32H737_Base.uvoptx` 里的 `-N00("ARM CoreSight SW-DP") -D00(0BD12477)` |

### 2. 需要自备的厂商件（本仓库刻意不含）

| 需要的东西 | 从哪拿 |
|---|---|
| **GD32H7xx Firmware Library**（8 个文件：`system_gd32h7xx.c`、`ARM/startup_gd32h7xx.s`、`gd32h7xx_gpio.c`、`gd32h7xx_usart.c`、`gd32h7xx_rcu.c`、`gd32h7xx_pmu.c`、`gd32h7xx_misc.c`、`gd32h7xx_dma.c`，以及 CMSIS/外设的 Include 目录） | 兆易官网 gd32mcu.com「资料下载 → GD32H7 系列」下载 **GD32H7xx_Demo_Suites_V2.1.0**（本项目实测用的就是这一版，SDK 内文件时间戳 2025-02-19） |
| `gd32h7xx_libopt.h` | 同上，从 Demo Suites 任一例程目录（如 `GD32H759I_START_Demo_Suites/Projects/01_GPIO_Running_LED/`）**原样拷贝**，一个字都不用改 |
| `GigaDevice.GD32H7xx_DFP.1.4.0.pack` | 兆易 **GD32H7xx_AddOn_V1.4.0** 安装包内，或 Keil Pack Installer 在线装 |
| **`GD_LIB_CM7_v212.lib`（664 120 字节）+ `inc/` 下 18 个头文件 + `systick.c/.h`** | **必须自己向兆易索取**（代理商 / FAE / gd32mcu.com 的 GD32 AI 工具链页面）。这是兆易的闭源 NN 推理库，本仓库不再分发，理由见下面第 5 节 |

### 3. 目录摆放（**关键**，uvprojx 里的路径是硬编码相对路径）

`GD32H737_Base.uvprojx` 的 `<FilePath>` 和 `<IncludePath>` 全部写成 `..\GD32H7_study\...` 和 `..\h737vmt6_AI1.0\...`，所以必须还原成"两个兄弟目录"的结构：

```
<你的工作目录>/
├── h737vmt6_AI1.0/                         ← 本仓库 gd32h737-inference/ 平铺到这里
│   ├── GD32H737_Base.uvprojx               ← 本仓库 keil/
│   ├── main.c                              ← 本仓库 src/
│   ├── gd32h7xx_it.c                       ← 本仓库 src/
│   ├── gd32h7xx_it.h                       ← 本仓库 src/
│   └── gd32h7xx_libopt.h                   ← 自己从 SDK 拷
└── GD32H7_study/
    ├── GD32H7xx_Demo_Suites_V2.1.0/
    │   └── GD32H7xx_Firmware_Library/
    │       ├── CMSIS/GD/GD32H7xx/{Include,Source}/...   ← system_gd32h7xx.c 要打 2 处补丁
    │       └── GD32H7xx_standard_peripheral/{Include,Source}/...
    └── GD32_H7_AI/1.0/
        ├── GD_LIB_CM7_v212.lib             ← 兆易提供
        ├── inc/                            ← 兆易提供（含 operators/cmsis_nn、operators/third_party）
        ├── systick.c / systick.h           ← 兆易提供
        └── nn_model_configure.c / .h        ← 本仓库 model/ 的两个文件，覆盖掉 GD_LIB 自带的模板
```

工程的 6 条 include 路径（uvprojx `<IncludePath>`）依次是：Firmware_Library 的 CMSIS Include、标准外设 Include、`..\h737vmt6_AI1.0`、`..\GD32H7_study\GD32_H7_AI\1.0\inc`、`..\GD32H7_study\GD32_H7_AI\1.0`、`...\inc\operators\cmsis_nn`、`...\inc\operators\third_party`。

### 4. 打补丁 → 编译 → 烧录

1. 按 `patches` 字段改 `system_gd32h7xx.c` 两处（第 43 行放开 IRC64M 600 MHz；SystemInit 里加 `pmu_smps_ldo_supply_config(PMU_LDO_SUPPLY);`）。**不打这两处，板子起不来**。
2. 用 `nn_model_configure.c` / `nn_model_configure.h` 覆盖 `GD32_H7_AI/1.0/` 下同名文件（GD_LIB 自带的是空模板）。
3. 打开 `GD32H737_Base.uvprojx`，Target 选 `GD32H737`，Project → Build（`Objects/` 和 `Listings/` 会自动生成，本仓库没有收录它们）。参考产物规模（读自 `Listings/GD32H737_Base.map:3185-3187`）：Total RO 102 452 B（100.05 kB）、Total RW+ZI 112 808 B（110.16 kB）、Total ROM 177 508 B（173.35 kB）。
4. 烧录：Flash → Download，或 Debug → 全速跑。Scatter 由 uVision 自动生成：代码 `0x08000000`/3840 KB，RW+ZI 落在 `RW_IRAM1 0x24000000`（832 KB AXI SRAM）和 `RW_IRAM2 0x00000000`（64 KB ITCM）。

### 5. 编译期两个必知的坑

- **半主机（semihosting）必须关**。`main.c:11-27` 提供了 `__use_no_semihosting` + `_sys_exit` / `_ttywrch` / `FILE __stdout` / `fputc` 一整套桩。AC6 下如果不关，GD_LIB 内部的打印会触发 `BKPT`，脱开调试器上电就停住。这段不要删。
- **BENCHMARK 通路的三个符号**。`nn_model_configure.h:41` 定义了 `BENCHMARK`，GD_LIB 会引用 `gd_nn_measure_time_start/get/stop`；`main.c:74-76` 把它们实现成空函数、`get` 恒返回 `0.0f`。所以**这份固件不产出任何推理耗时数字**，仓库里也没有任何延迟/功耗/续航实测值。想量真实耗时请自己换成 DWT CYCCNT。

### 6. 关于 GD_LIB 与模型权重的许可（诚实说明，务必照抄进 README）

**结论：`GD_LIB_CM7_v212.lib` 不随本仓库分发；`nn_model_configure.c/.h` 随仓库分发，但保留兆易版权头并明确标注权属混合。**

排除 .lib 的依据（都实查过）：
- `GD32_H7_AI/` 整个目录里 **没有任何 license / SLA 文件**，唯一一份 LICENSE 在 `inc/operators/third_party/LICENSE`，是内嵌 gemmlowp/fixedpoint 的 Apache-2.0，管不到 .lib。
- 对 .lib 做 `strings` 搜 copyright / licen / gigadevice / proprietary，**零命中**——二进制里没有版权声明，因此技术上也无法满足"再分发时必须复现版权声明"这一条。
- 仓库里能找到的兆易正式许可是 `GD32H7xx_Demo_Suites_V2.1.0/SOFTWARE LICENSE AGREEMENT SLA-GD0001-version1.1.pdf`。它第 2 条 (iii) 确实允许二进制再分发，但要求同时满足：复现版权声明 + 限制条款 + 免责声明 + 出口管制与合规声明；再分发物"execute solely and exclusively on the devices manufactured by or for GigaDevice"；并向下游转授同等 sublicense。而且这份 SLA 是随 Demo Suites 发的，**并不覆盖单独渠道拿到的 GD_LIB AI 包**。
- 综上，默认判断就是不收录。请自行向兆易索取，并遵守随包给你的那份 SLA。

`nn_model_configure.c/.h` 的处理方案（权属混合，逐条说清）：
- 文件外壳（第 1-14 行版权头、第 41-62 行的 buffer 定义、宏结构）是**兆易 AI 转换工具生成的模板**，版权头写的是 `Copyright (c) 2023, GigaDevice Semiconductor Inc.`。注意：这两个文件的版权头**只有免责声明段，没有 `gd_nn_*.h` 里那段 "Redistribution and use ... are permitted provided that" 的授权段**——也就是说兆易没在这两个文件上明确给再分发许可。
- 文件里真正有价值的部分是**作者自己的训练产出**：`nn_model_configure.c:79` 的 `model_paras_arr[33960]` + `:1784` 的 `model_paras_data[40960]`（合计 **74 920 字节** 权重）、`:21-27` 的 14 个算子回调、`:30-35` 的算子名表、`nn_model_configure.h:30/35/56/58` 的尺寸宏。这些换个模型就全变。
- **本仓库的做法**：收录这两个文件，(a) 原样保留兆易版权头一个字不改；(b) 在 `model/README.md` 里写明"本文件由兆易 GD32 AI 转换工具从作者的 Keras 模型生成，模板部分版权归兆易，权重与算子表由作者的模型决定"；(c) 写明"编译本文件还需要你自己从兆易获取 `GD_LIB_CM7_v212.lib` 与 `inc/`，本仓库不提供"。
- **兜底方案**：如果兆易对此有异议，把这两个文件从仓库删掉，改为只发布两段权重的裸数据（`model_paras_arr.bin` 33 960 B + `model_paras_data.bin` 40 960 B）加一份格式说明——权重本身是作者的资产，不受兆易模板的许可牵连。建议开源前顺手去兆易官方论坛/FAE 处确认一次。
- 另外提醒：把权重转成 GD_LIB 格式的**上游模型文件（.tflite / .h5 / .eim）在整个仓库里都不存在**（全库搜 `*.tflite *.h5 *.onnx *.keras *.eim *.pb` 零命中），所以第三方拿到权重也无法重跑转换、只能直接用。这一条已列入 known_issues。

## 本目录未收录哪些文件，为什么

**总量**：本子系统 18 165 个文件，其中 GD32H7_study 一个目录就占 18 124 个文件 / 1 240 MB。收录 6 个文件，排除 18 159 个 / 约 1 239 MB（≈99.96% 的体积）。

**一、编译中间产物与 IDE 缓存（GD32H7_study 内）**
- .o/.d/.crf/.dep/.lst/.map/.axf/.htm/.lnp/.sct/.iex：**10 017 个文件 / 986.4 MB**。单个最大的是 alll/tinyml_test2.0/MDK-ARM/Listings/Project.map（10.4 MB）和 alll/test3 的 9.3 MB map，八个官方例程各自的 MDK-ARM/Objects 都在 81~99 MB。
- .uvoptx / .uvguix.* / .ewt / .ewd / .eww：**359 个文件 / 21.2 MB**。

**二、h737vmt6_AI1.0 里排除的 35 个文件**
- Objects/（20 个：.o .d .axf .htm .lnp .sct .iex .dep）、Listings/GD32H737_Base.map — 编译产物。
- GD32H737_Base.uvoptx（13 KB，含个人调试断点/窗口布局）、GD32H737_Base.uvguix.Administrator（183 KB，Keil 界面布局）、RTE/_GD32H737/RTE_Components.h、EventRecorderStub.scvd — Keil 自动生成。
- gd32h7xx_libopt.h — 已用 diff 逐字节验证与 SDK 的 GD32H759I_START_Demo_Suites/Projects/01_GPIO_Running_LED/gd32h7xx_libopt.h **完全相同**（时间戳也是 SDK 批次的 2月19 2025），且带 GigaDevice BSD-3 版权头，属纯 SDK 原件，让使用者自己从 SDK 拷。
- **main - 副本.c（9213 字节，6月2 11:00）** — 备份文件，按铁律排除，但它信息量很大：这是 21 帧 / 6 类 的**上一代**固件（WINDOW_FRAMES 21、INFERENCE_STEP 7、user_output_size = 6*4），也就是与仓库里那份 21 帧训练脚本对应的版本。此事已单独写进 known_issues，不靠这个文件传递信息。

**三、GD_LIB AI 库（GD32H7_study/GD32_H7_AI/1.0/）——许可判定，重点**
- **GD_LIB_CM7_v212.lib（664 120 字节）：排除。** 依据有两条：(1) 用 `find -iname '*licen*'` 扫过整个 GD32_H7_AI 目录，**只有 inc/operators/third_party/LICENSE 一份 Apache-2.0**（那是内嵌的 gemmlowp/fixedpoint 第三方件），**这个 AI 包本身没有随附任何 SLA 或 license 文件**；(2) `strings` 扫遍 .lib，搜 copyright/licen/gigadevice/proprietary **零命中**，二进制里连版权声明都没有，没法"reproduce the copyright notice"；(3) 库里能看到 ~4600 个 obj 名，既有 arm_convolve_s8.o、arm_depthwise_conv_s8_opt.o 这类 CMSIS-NN 目标（ARM，Apache-2.0），也有 activation_hardswish_f32.o、add_broadcast.o 这类兆易自研目标，混合体，无法逐块确权；(4) 同仓库里能找到的兆易许可原文是 GD32H7xx_Demo_Suites_V2.1.0/SOFTWARE LICENSE AGREEMENT SLA-GD0001-version1.1.pdf，其第 2 条 (iii) 虽然允许二进制再分发，但附三个硬条件——必须复现版权声明/限制/免责/出口管制/合规声明、再分发物"execute solely and exclusively on the devices manufactured by or for GigaDevice"、且必须向下游转授同等 sublicense。往 GitHub 扔一个不带 SLA 全文、不带版权声明的 .lib，这三条一条都不满足。而且 SLA-GD0001 是随 Demo Suites 发的，**并不能自动覆盖 GD_LIB AI 包**（那是另一条获取渠道）。结论：不收录，让使用者自行向兆易索取。
- inc/ 下 18 个头文件（gd_nn_interface.h、gd_nn_basic_types.h、gd_nn_layer.h、gd_nn_layer_internal.h、gd_nn_report.h、gd_nn_support.h、gd_nn_tensor.h、gd_nn_types.h、nn_forward.h、operators/cmsis_nn/*.h 6 个、operators/third_party/{detect_platform.h,fixedpoint.h,LICENSE}）：**排除**。这些确实带完整的 GigaDevice BSD-3-Clause 授权段（"Redistribution and use in source and binary forms... are permitted provided that..."），技术上可以再分发，但它们是原封不动的 SDK 原件、作者一个字没改，收录只会给别人一个"这个仓库是 SDK 的一部分"的错觉，也违背"不再分发厂商 SDK 原件"的原则。
- systick.c / systick.h（12月2 2025）：**排除**。已 diff 过，与 Demo Suites 的 01_GPIO_Running_LED/systick.c **仅差第 5 行版本号和第 9 行版权年份**（2023 V1.0.0 vs 2024 V2.1.0），是老版 SDK 原件，作者未改。工程里链接它但代码从不调用（见 known_issues）。
- **成功版本1/nn_model_configure.c + .h（各 3835/134 行，460 KB）：排除**。diff 过：与收录的那份**只差第 37 行 model_name（\"finnal3\" vs \"finnal4\"）和第 183–1778 行的权重字节**，是另一次训练的权重。没有对应训练脚本、没有类别顺序说明、没有精度记录，收进来只是 460 KB 无法解释的字节。此事写进 known_issues。

**四、GD32H7_study/alll/ 下的 Edge Impulse 练手残留（test / test2 / test3 / fff）**
四份 EI 导出 C++ SDK，**每份 1379 个文件 / 25.3 MB，合计 101 MB**，全部排除。核实结论：
- 都是同一个 EI 项目：model-parameters/model_metadata.h:87 `EI_CLASSIFIER_PROJECT_ID 916703`、:89 `PROJECT_NAME "ZhiXuanYE-project-1"`、:101 `LABEL_COUNT 4`、:103 `FREQUENCY 10`、:99 `INTERVAL_MS 100`、:91 `NN_INPUT_FRAME_SIZE 208`；model_variables.h:49 类别是 `{ "jogging", "jumping", "standing", "walking" }`。10 Hz / 4 类 / 208 维，**与最终 30 Hz / 8 类 / 1980 维模型毫无关系**，是三月份的练手件（main.cpp、gd32_ei_porting.cpp 时间戳 3月2~3月4 2026）。
- **许可要纠正一处**：EI 的 SDK 不是 Apache-2.0。test/edge-impulse-sdk/classifier/ei_run_classifier.h 首行写的是 `The Clear BSD License / Copyright (c) 2025 EdgeImpulse Inc.`，目录里放的是 LICENSE 和 LICENSE.3-clause-bsd-clear；只有内嵌的 edge-impulse-sdk/tensorflow/LICENSE 是 Apache-2.0。Clear BSD 允许再分发，所以**排除的理由不是许可，而是它跟成品无关**——留着只会让读者误以为最终模型是 EI 在板上跑的（实际板上跑的是兆易 GD_LIB，EI 只用来训练）。
- 这四份里作者自写的部分只有 main.cpp（4.6~7.7 KB）和 gd32_ei_porting.cpp（约 1.1 KB 的 ei_malloc/ei_printf 移植桩），同样排除。

**五、GD32H7_study/alll/ 下的 8 个例程 + AI 移植试验**
- 00_FreeRTOS_template、01_GPIO_Running_LED、02_GPIO_Key_Polling_mode、03_EXTI_Key_Interrupt_mode、04_USART_HyperTerminal_Interrupt、05_TIMER_Key_EXTI、06_USB_MSC_Device、07_USB_MSC_Host：**共约 707 MB，全部排除**。这批**不是作者原创、也不是兆易官方原件，而是第三方教程包**——证据：每个 MDK-ARM 目录里同时存在 `GD32H759I_START.uvguix.FFYUN` 和 `.uvguix.Administrator` 两个 Keil 界面配置，FFYUN 是别人的 Windows 用户名；文件时间戳集中在 1月16~1月22 2026。里面确有中文注释的 main.c / Hardware/uart/bsp_uart.c，但那是教程作者的，不能当自己的开源。
- tinyml_test / tinyml_test2.0 / tinyml_test3.0（51~54 MB 各）：作者的 AI 移植试验，main.cpp 是自写的（tinyml_test3.0/main.cpp 10 987 字节，3月26 10:42，含 `__use_no_semihosting` 全套 _sys_* 桩 + ei_run_classifier 调用），但用的仍是 4 类练手模型、跑在 GD32H759I-START 官方评估板上（`#include "gd32h759i_start.h"`），与成品固件无继承关系，排除。
- Utilites/（20 KB，gd32h759i_start.c/.h）：SDK 板级支持包原件，排除。

**六、厂商 SDK 与文档原件**
- GD32H7xx_Demo_Suites_V2.1.0/（**2173 个文件 / 85.7 MB**）：兆易官方三套评估板 Demo（GD32H759I_EVAL / GD32H759I_START / GD32H757V_START）+ GD32H7xx_Firmware_Library + 原理图 PDF + SLA-GD0001 PDF。工程实际只用到其中 8 个文件（system_gd32h7xx.c、startup_gd32h7xx.s、gd32h7xx_{gpio,usart,rcu,pmu,misc,dma}.c）。全部排除，只把作者对 system_gd32h7xx.c 的两处改动写进 patches。已 diff 验证：gpio/usart/rcu/pmu/misc/dma 六个 .c 与 alll/ 下另一份副本**完全一致**，startup_gd32h7xx.s 是 SDK 默认的 Stack 0x1000 / Heap 0x800（作者未改；alll/ 那份改成 0x8000/0x10000 的是 EI 试验用的）。
- GD32H7xx_Demo_Suites_V2.1.0.7z（15.4 MB）×2 份（GD32H7_study/ 根下和 alll/硬件/ 下各一份）、GD32H7xx_AddOn_V1.4.0/（9.5 MB，含 GigaDevice.GD32H7xx_DFP.1.4.0.pack + IAR_GD32H7xx_ADDON.1.4.0.exe + SLA-GD0006 PDF）、GD32H7xx_AddOn_V1.4.0.7z（3.7 MB）、alll/硬件/（25 MB，Demo Suites 的又一份副本）：厂商安装包，一律排除，改为在 build_instructions 里给获取途径。
- AN109 GD32H73x_75x系列硬件开发指南_Rev1.6.pdf（2.7 MB）、GD32H759I-EVAL-V2.1.pdf（6.3 MB）等原理图/用户指南 PDF、BOM_研电赛终极板子_..._2026-05-19.xlsx（8 KB，硬件 BOM，归硬件子系统处理）：排除。

---

已知问题见 [`docs/KNOWN-ISSUES.md`](../../docs/KNOWN-ISSUES.md)，
需打进 SDK 的改动见 [`docs/BUILD-PATCHES.md`](../../docs/BUILD-PATCHES.md)。
