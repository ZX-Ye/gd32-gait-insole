# GD32F470 汇聚枢纽

GD32F470ZGT6 是整条链路的"总站"。上游：BLE Peripheral 侧的 GD32VW553 把左右脚合成的 94 字节 CombinedDataPacket 用 1.5 Mbps 串口推给 F470 的 UART4（PC12/PD2），F470 用 DMA0-CH0 + IDLE 中断 + 双 1 KB 乒乓缓冲收，在 task_parse 里按"帧头 0xAA + 第 94 字节 0x55"滑窗对齐取包。下游：F470 做 30 Hz 绝对均匀重采样后一包双发——UART3(PC10/PC11) 给 PC 上位机，USART1(PD5/PD6) 给 GD32H737 端侧推理节点；H737 每 15 帧（500 ms）跑一次双流 1D-CNN，把 8 类结果覆写到包尾字节沿 PD6 回传，F470 的 USART1 定长状态机把它取出成 g_ai_result。本地显示走 TLI + IPA 硬件加速：帧缓存 768 KB 定址在 EXMC SDRAM 0xC0000000，LVGL 只用两块 800×40 的内部 SRAM 绘制缓冲（共 128 000 B），IPA 负责把绘制块搬进显存。UI 是一块医疗风格大字报（中文 48 号字 + 英文副标 + RGB565 警示色），外加一个永不停歇的一分钟滚动风险窗（1800 帧结算，索引 0/1/3 记为高危）。技术要点：三路 1.5 Mbps 全部落在 APB1=50 MHz 上；两个串口 ISR 的优先级(3/4)高于 FreeRTOS 的 syscall 上限(5)，所以裸吃字节、不碰任何内核 API；转发在临界区里忙等完成，保证 94 字节不被撕开。

## 硬件配置

下表所有 `文件:行号` 均相对 `4.工程代码/2.GD32F470ZGT6程序/final_wireless_v16.0/`。★ = 本仓库收录的文件；其余为例程原件（数值从原件读出，代码不收）。

### 时钟与内核

| 项目 | 配置值 | 出处 |
|---|---|---|
| MCU | GD32F470ZGT6（Cortex-M4F，1 MB Flash / 144 pin） | 目录名 `2.GD32F470ZGT6程序`；注意 Keil 里选的器件是 `GD32F470VE`（★Project/15_lvgl_freertos_test5.uvprojx `<Device>`），见 known_issues #9 |
| SYSCLK | 200 MHz，25 MHz HXTAL，PLL PSC=25 / N=400 / P=2 / Q=9 | Firmware/CMSIS/GD/GD32F4xx/Source/system_gd32f4xx.c:54（`__SYSTEM_CLOCK_200M_PLL_25M_HXTAL`）、:825 起 `system_clock_200m_25m_hxtal()` 内 PLL 注释行 |
| 总线 | AHB = 200 MHz，APB2 = 100 MHz，APB1 = **50 MHz**（三路串口全在 APB1） | system_gd32f4xx.c 的 `RCU_AHB_CKSYS_DIV1` / `RCU_APB2_CKAHB_DIV2` / `RCU_APB1_CKAHB_DIV4` |
| NVIC 分组 | `NVIC_PRIGROUP_PRE4_SUB0`（4 bit 抢占，与 configPRIO_BITS=4 对齐） | ★User/main.c:77 |
| FreeRTOS | V10.5.1；tick 1000 Hz；32 级优先级；最小栈 130 字；heap_4，堆 **70 KB**；syscall 优先级上限 5 | ★freeRTOS/include/FreeRTOSConfig.h:69 / :71 / :73 / :110 / :184（实测 .map:10022 `ucHeap … 71680`） |
| 任务 | start_task 4096 字/prio 3；task_ui 2048 字/prio 4；task_parse 2048 字/prio 5 | ★User/main.c:89, :103, :104（宏在 :64-65） |

### UART4 — 接收 VW553（上游）

| 项目 | 配置值 | 出处 |
|---|---|---|
| 外设/引脚 | UART4，TX = **PC12**，RX = **PD2**，复用 **AF8** | ★Hardware/usart/bsp_usart.h:8-20；配置动作 ★bsp_usart.c:44-57 |
| 参数 | 1 500 000 bps，8 位，无校验，1 停止位 | ★bsp_usart.c:60-64；波特率实参 ★main.c:82 |
| 中断 | **只开 IDLE，不开 RBNE**；NVIC 抢占优先级 3 | ★bsp_usart.c:71-72 |
| DMA | **DMA0 / 通道0 / 子外设4**（UART4_RX），外设→内存，8 bit，`DMA_PRIORITY_ULTRA_HIGH`，单次 1024 | ★bsp_usart.h:26-29；★bsp_usart.c:17-38 |
| 乒乓双缓冲 | `rx_buffer_A` / `rx_buffer_B`，各 `UART_RX_BUFFER_SIZE = 1024` B；IDLE 里算 `1024 - dma_transfer_number_get()` 得长度，切 `dma_memory_address_config(..., DMA_MEMORY_0, ...)` | 缓冲定义 ★bsp_usart.c:9-13（宏 ★bsp_usart.h:22）；乒乓切换 ★bsp_usart.c:158-174 |
| 错误处理 | 进 ISR 先吞 ORERR/FERR（读 STAT0 再读 DATA） | ★bsp_usart.c:131-138 |
| printf | `fputc` 重定向到 BSP_USART = UART4（即上游那根线） | ★bsp_usart.c:118-122 |

### UART3 — 发 PC 上位机

| 项目 | 配置值 | 出处 |
|---|---|---|
| 外设/引脚 | UART3，PC10 / PC11，复用 **AF8**，50 MHz 输出速度 | ★Hardware/usart/bsp_usart.c:194-199 |
| 参数 | 1 500 000 bps，**只使能发送**（`USART_TRANSMIT_ENABLE`，不开接收） | ★bsp_usart.c:201-204 |
| 注意 | 头文件里另有一组 `BSP_UART3_* = USART2 + AF7` 的历史宏（★bsp_usart.h:41-52）和死函数 `usart3_config()`（★bsp_usart.c:86-103），与实际用的 UART3+AF8 冲突 | 见 known_issues #8 |

### USART1 — 与 GD32H737 双向

| 项目 | 配置值 | 出处 |
|---|---|---|
| 外设/引脚 | USART1，TX = **PD5**，RX = **PD6**，复用 **AF7** | ★Hardware/usart/bsp_usart.c:210-215 |
| 参数 | 1 500 000 bps，收发都开 | ★bsp_usart.c:217-222（H737 侧同为 1500000，见 1.GD32H737VMT6程序/h737vmt6_AI1.0/main.c:114） |
| 中断 | **RBNE**（接收缓冲非空），NVIC 抢占优先级 4 | ★bsp_usart.c:223-224 |
| 回传解析 | 94 字节定长状态机：`rx_idx==0` 时死等 0xAA；满 94 字节校验尾字节（`==0x55 || <=7`）后取 `rx_buf[1..4]` 为帧号、`rx_buf[93]` 为 AI 类别 | ★bsp_usart.c:256-292（帧号 :285，AI 结果 :286） |
| 发送 | `usart_forward_send_packet()` 在 `taskENTER_CRITICAL()` 里对 UART3 和 USART1 逐字节忙等双发，保证 94 字节不被撕开 | ★bsp_usart.c:230-247；调用点 ★main.c:193 |

### 数据协议与节拍

| 项目 | 配置值 | 出处 |
|---|---|---|
| `CombinedDataPacket` | `#pragma pack(1)`，**94 字节**：header 0xAA(1) + timestamp u32(4) + left_adc[16] u16(32) + left_imu[6] i16(12) + right_adc[16] u16(32) + right_imu[6] i16(12) + tail 0x55(1) | ★User/main.c:48-58 |
| 取包 | 滑窗判 `p[i]==0xAA && p[i+93]==0x55`，命中后跳过整包 | ★main.c:162-179 |
| 转发节拍 | 33 ms → 约 30 Hz（`pdMS_TO_TICKS(33)`），收到首包后才解锁（`bluetooth_is_alive`） | ★main.c:149, :186-193, :169 |
| 一分钟风险窗 | 1800 帧结算（30 Hz×60 s），高危类别 = 索引 **0 / 1 / 3**，`ratio = abnormal*100/1800`，清零后无限滚动 | ★main.c:198-215 |
| AI 更新率 | H737 每 **15** 帧（500 ms）出一个结果 | 4.工程代码/1.GD32H737VMT6程序/h737vmt6_AI1.0/main.c:36 `INFERENCE_STEP 15` |

### 8 分类标签表与 RGB565 色卡（★Hardware/lcd_my_test/lcd_mytest.c:31-53）

| 索引 | 中文（:31-34） | 英文（:36-39） | RGB565（:44-53） | 含义 |
|---|---|---|---|---|
| 0 | 痛性跛行 | ANTALGIC | 0xF800 红 | 高危 |
| 1 | 盲态探步 | BLIND PROBE | 0xF800 红 | 高危 |
| 2 | 正在下楼 | DOWNSTAIRS | 0xFD20 橙 | 预警 |
| 3 | 偏瘫步态 | HEMIPLEGIC | 0xF800 红 | 高危 |
| 4 | 静止坐立 | SITTING | 0x8410 灰蓝 | 安全 |
| 5 | 静止站立 | STANDING | 0x07FF 亮青 | 安全 |
| 6 | 正在上楼 | UPSTAIRS | 0xFD20 橙 | 预警 |
| 7 | 正常走路 | WALKING | 0x07E0 亮绿 | 安全 |

高危(0/1/3)会把整屏底色压成 0x330000、边框转 0x880000 并加 `LV_SYMBOL_WARNING`（:156-159）；RISK 数字 >30% 转红、>10% 转黄、否则绿（:142-148）。

### 800×480 显示链（TLI + IPA + SDRAM）

| 项目 | 配置值 | 出处 |
|---|---|---|
| 面板 | 800×480，RGB565（`LAYER_PPF_RGB565`），单层 LAYER0，开 dither | Hardware/RGB/lcd.c:85, :101-102；分辨率宏 Hardware/RGB/lcd.h:11-12 |
| 时序 | HSPW 10 / HBP 150 / HACT 800 / HFP 15；VSPW 10 / VBP 140 / VACT 480 / VFP 40；HS/VS/DE 均低有效 | Hardware/RGB/lcd.h:27-35；极性 lcd.c:58-61 |
| 像素时钟 | PLLSAI N=192 → VCO 192 MHz，R=3 → 64 MHz，`RCU_PLLSAIR_DIV2` → **32 MHz**。按 975×670 总像素算，刷新率约 49 Hz | lcd.c:42（41 行是被注释掉的 216 版本）、lcd.c:47 |
| TLI 引脚 | HSYNC **PC6** / VSYNC **PA4** / PCLK **PG7** / DE **PF10**；R7-R3 = PG6, PA8, PA12, PA11, PB0；G7-G2 = PD3, PC7, PB11, PB10, PG10, PA6；B7-B3 = PB9, PB8, PA3, PG12, PG11。AF14（PB0/PG10/PG12 为 AF9） | lcd.c:118-176（AF 设置 :132-155） |
| 背光/DISP | **PD13** 推挽输出，`lcd_disp_off()`/`lcd_disp_on()` 拉低/拉高 | lcd.c:183-188, :199, :210 |
| IPA | `RCU_IPA` 开钟 + `nvic_irq_enable(IPA_IRQn, 0, 2)`；`LV_USE_GPU_GD32_IPA 1` | lcd.c:112-115；★LVGL/lv_conf.h:776, :780 |
| IPA 搬运 | disp_flush 裸写寄存器：FPCTL=RGB565、FMADDR=LVGL 缓冲、DMADDR=`framebuf + 2*(800*y1+x1)`、DLOFF=行偏移、IMS=宽<<16\|高、CTL\|=TEN 后忙等 | ★LVGL/porting/lv_port_disp_template.c:118-144 |
| 帧缓存 | `uint16_t ltdc_lcd_framebuf0[800][480] __attribute__((at(0xC0000000)))` = 768 000 B，直接钉在 SDRAM | Hardware/RGB/lcd.c:8、lcd.h:38/41（.map 里为 `.ARM.__AT_0xC0000000` 段） |
| 绘制缓冲 | **两块 800×40** RGB565 = 64 000 B×2，放内部 SRAM；`full_refresh = 0` | ★lv_port_disp_template.c:50-54, :72, :91（实测 .map:9994 该文件 .bss = 128 108 B） |
| SDRAM | EXMC Device0 @ **0xC0000000**；16 bit 总线；9 列 / 13 行地址；4 内部 bank；CAS=2；SDCLK = 2×HCLK（100 MHz）；突发读开；流水线延迟 1 HCLK | Hardware/SDRAM/exmc_sdram.h:56；Hardware/SDRAM/exmc_sdram.c:160-168（时序 :144-156）；初始化调用 ★main.c:98 |
| LVGL | v8.4.0；LV_COLOR_DEPTH 16；LV_MEM_SIZE 48 KB（LV_MEM_CUSTOM=0）；刷新周期 15 ms；LV_USE_PERF_MONITOR **1**（右下角） | ★LVGL/lv_conf.h:27, :49-52, :81, :285-287 |
| UI 刷新 | task_ui 每 5 ms 跑一次 `lv_timer_handler()`，`update_dashboard_ui()` 限速到 33 ms（约 30 FPS） | ★main.c:118-132 |

### 其它板载资源（例程 BSP，本工程只调用 LED 初始化）

| 项目 | 配置值 | 出处 |
|---|---|---|
| LED1-4 | **PE3 / PD7 / PG3 / PA5**，推挽 50 MHz | Hardware/led/bsp_led.h:8-25；调用 ★main.c:79 |
| 触摸 | Hardware/touch 已编译，但 `lv_port_indev_init()` 全工程未调用 → 触摸没接进 LVGL | 无调用点（grep 全工程） |

### 存储器占用（作者最后一次构建）

| 项目 | 数值 | 出处 |
|---|---|---|
| Code + RO Data | 405 912 B（396.40 kB） | Project/Listings/GD32F450.map:12341 |
| RW + ZI Data | 1 023 840 B（999.84 kB，其中 768 000 B 是 0xC0000000 的 SDRAM 显存，不占片内 RAM） | .map:12342 |
| 链接布局 | Flash 0x08000000 长 0x100000；RAM 0x20000000 长 0x40000（STACK 与 FreeRTOS 堆显式前置） | ★Project/Objects/GD32F450.sct（全文见 build_instructions） |

## 编译与烧录

## 工具链

| 项目 | 版本/说明 |
|---|---|
| IDE | Keil MDK-ARM（µVision5）。工程 `uAC6=0`，即用 **ARM Compiler 5 (armcc)**；AC6 编不过（大量 GBK 注释依赖 `--no-multibyte-chars`，且 `__attribute__((at()))` 语法在 AC6 下需改写） |
| Device Pack | GigaDevice **GD32F4xx DFP**（工程里选的器件名是 `GD32F470VE`，见 known_issues #9） |
| 优化等级 | `-O3`（uvprojx `<Optim>4`）；`Misc Controls: --no-multibyte-chars`；`useUlib=1`（用 ARM 标准库，printf 靠 bsp_usart.c:118 的 `fputc` 重定向） |
| 下载器 | CMSIS-DAP / DAP-Link（`FlashDriverDll=UL2CM3`），SWD |

## 需要自备的第三方代码（本仓库一律不再分发）

| 依赖 | 版本 | 获取方式 |
|---|---|---|
| GD32F4xx 标准外设固件库 + CMSIS | GD32F4xx_Firmware_Library **V3.0.x**（文件头版本行 `2022-03-09, V3.0.0`） | GigaDevice 官网 MCU 资源下载页，或直接用下面那份梁山派例程里自带的 `Firmware/` |
| 立创·梁山派 GD32F470 LVGL 例程 | 与本工程同源的那一版（含 `Hardware/RGB`、`Hardware/SDRAM`、`Hardware/touch`、`Hardware/SD`、`LVGL/lv_gpu_gd32_ipa.c/.h`） | 立创开源硬件平台「梁山派 GD32F470」资料包。**这是必需的**：RGB 屏 TLI 驱动、EXMC SDRAM 初始化、IPA 的 LVGL GPU 后端全在里面 |
| LVGL | **v8.4.0**（`LVGL/lvgl.h` 里 MAJOR 8 / MINOR 4 / PATCH 1 / INFO "dev"；v8.3 亦可，需自行核对 API） | github.com/lvgl/lvgl，release/v8.4 分支 |
| FreeRTOS Kernel | **V10.5.1**（FreeRTOS V202212.00 发行包） | github.com/FreeRTOS/FreeRTOS-Kernel，V10.5.1 tag。只需 `core/`(list/queue/tasks/timers/event_groups/stream_buffer/croutine) + `heap/heap_4.c` + `portable/RVDS/ARM_CM4F`(port.c/portmacro.h) + `include/` |

## 目录还原（作者代码放哪）

先把上面的例程解包成如下骨架（就是本工程原始结构），再把本仓库 `firmware/gd32f470-hub/` 里的文件按左→右覆盖进去：

```
final_wireless_v16.0/
├─ Firmware/            ← GD32F4xx 固件库原件（CMSIS + standard_peripheral），不动
├─ freeRTOS/
│   ├─ core/ heap/ port/            ← FreeRTOS 原件，不动（heap 只编 heap_4.c）
│   └─ include/FreeRTOSConfig.h     ← 覆盖成 freertos/FreeRTOSConfig.h（70 KB 堆）
│                                     并删掉 freeRTOS/FreeRTOSConfig.h 那份 32 KB 的陷阱
├─ Hardware/
│   ├─ RGB/ SDRAM/ touch/ SD/ spi/ rtc/ key/ led/ timer/ adc/  ← 例程原件，不动
│   ├─ usart/           ← 放 hardware/usart/bsp_usart.c + bsp_usart.h
│   ├─ lcd_my_test/     ← 放 hardware/ui-dashboard/lcd_mytest.c + lcd_mytest.h（新建此目录）
│   └─ si24r1/          ← 放 legacy/si24r1_soft_spi.c + .h（可选，历史遗留、从未调用）
├─ LVGL/
│   ├─ src/ demos/ lvgl.h lv_gpu_gd32_ipa.c lv_gpu_gd32_ipa.h  ← LVGL + 例程原件
│   ├─ lv_conf.h        ← 覆盖成 lvgl-port/lv_conf.h
│   └─ porting/
│       ├─ lv_port_disp_template.c   ← 覆盖成 lvgl-port/lv_port_disp_template.c
│       └─ ui_font_cn_48.c           ← 自己生成（见下）
├─ User/                ← 覆盖 user/main.c、main.h、gd32f4xx_it.c（保留例程的 systick.c/h、sys.c/h、gd32f4xx_libopt.h）
└─ Project/
    ├─ 15_lvgl_freertos_test5.uvprojx ← 覆盖成 keil/15_lvgl_freertos_test5.uvprojx
    └─ Objects/GD32F450.sct           ← 放 keil/GD32F450.sct（目录要先建出来）
```

## 中文字体必须自己生成

仓库不含 `ui_font_cn_48.c`（字形取自 simhei.ttf，版权不明）。装 `lv_font_conv` 后，用文件头记录的原始参数一模一样地重跑一遍（把 simhei.ttf 换成你自己有权使用的黑体，字号/bpp 别改，否则 `lcd_mytest.c` 的排版会飘）：

```bash
npm i -g lv_font_conv
lv_font_conv --bpp 4 --size 48 --no-compress --stride 1 --align 1 \
  --font simhei.ttf \
  --symbols 痛性跛行盲态探步正在下楼偏瘫步态静止坐立静止站立正在上楼正常走路等待同步 \
  --format lvgl -o ui_font_cn_48.c
```

生成后放进 `LVGL/porting/`，`lcd_mytest.c:26` 的 `LV_FONT_DECLARE(ui_font_cn_48)` 就能对上。字体的 `.fallback` 指向 `lv_font_montserrat_48`，所以 `lv_conf.h` 里那一号字要么打开、要么把这行 fallback 改掉。

## 分散加载文件（keil/GD32F450.sct 全文，丢了照抄即可）

```
LR_IROM1 0x08000000 0x00100000  {    ; load region size_region
  ER_IROM1 0x08000000 0x00100000  {  ; load address = execution address
   *.o (RESET, +First)
   *(InRoot$$Sections)
   .ANY (+RO)
   .ANY (+XO)
  }
RW_IRAM1 0x20000000 0x00040000 {  ; 256KB内部RAM
  startup_gd32f450_470.o(STACK)   ; 栈
  *heap_4.o(+RW +ZI)              ; FreeRTOS堆
  .ANY (+RW +ZI)                  ; 其他变量（避免包含显存）
}
}
```

800×480 的 768 000 B 显存不在这里分配——它靠 `Hardware/RGB/lcd.c:8` 的 `__attribute__((at(0xC0000000)))` 直接钉在 EXMC SDRAM 上（.map 里体现为 `.ARM.__AT_0xC0000000` 段）。

## 编译与烧录

1. 打开 `Project/15_lvgl_freertos_test5.uvprojx`（Target 名叫 `GD32F450`，别被名字误导）。
2. Options → Target 确认 Xtal/器件；Options → Linker 确认 **不勾** "Use Memory Layout from Target Dialog"，Scatter File = `.\Objects\GD32F450.sct`。
3. Rebuild。参考体积（作者最后一次构建的 .map）：Code+RO 396.40 kB / RW+ZI 999.84 kB（其中 768 000 B 是 SDRAM 显存，不占片内 RAM）。
4. DAP-Link 接 SWD（SWDIO/SWCLK/GND/3V3），Download。烧写算法在工程里配的是 `GD32F4xx_512KB.FLM`，ZGT6 是 1 MB，当前固件 396 kB 装得下；要用满 1 MB 就换 1 MB 算法。
5. 上电自检顺序：屏幕应先出现深色底 + "等待同步 / WAITING SYNC..."（`lcd_mytest.c:82,90`）；接上 VW553 后 task2 收到首包才解锁节拍器（`main.c:169`）；左下角 `EVAL: n%` 开始走动说明 H737 的分类结果已经回传到位。

## 联调接线（三路都是 1.5 Mbps 8N1，共地）

- VW553(TX) → F470 **PD2**(UART4_RX)；F470 **PC12**(UART4_TX) 目前只用来吐 printf。
- F470 **PC10**(UART3_TX) → USB-TTL → PC 上位机（`smart_insole_display10.py`）。
- F470 **PD5**(USART1_TX) → H737 USART1_RX；H737 USART1_TX → F470 **PD6**(USART1_RX)，推理结果就从这根线覆写包尾回来。

## 本目录未收录哪些文件，为什么

本子系统目录（4.工程代码/2.GD32F470ZGT6程序/final_wireless_v16.0）共 1447 个文件、204 MB，最终只收 14 个文件、约 4 100 行。排除的大类：

1. **Keil 编译产物 Project/Objects/：686 个文件、121 MB**（.o .crf .d .lst .axf .bin .hex .lnp .htm .iex 等）。唯一例外是从这个目录里救出来的 `GD32F450.sct`（见 include）。
2. **Project/Listings/：2 个文件、1.3 MB**（GD32F450.map 1.2 MB、startup_gd32f450_470.lst）。只从 map 里摘了体积数字当证据，文件本身不收。
3. **LVGL v8.4.0 源码树 LVGL/src + LVGL/demos：521 个文件、74 MB**（demos 里的 benchmark/music 图片素材占了 58 MB）。MIT 协议，但没必要再分发一份——让别人自己拉 v8.4/v8.3 分支。同目录的 lvgl.h、lv_gpu_gd32_ipa.c/.h（250 行，纯 ASCII、无中文无 emoji、LVGL 官方 DMA2D 移植风格）判为板级例程原件，也不收。
4. **GD32F4xx 标准外设固件库 Firmware/：139 个文件、3.2 MB**（CMSIS + GD32F4xx_standard_peripheral + usb_library + startup_gd32f450_470.s + system_gd32f4xx.c）。全部带 "Copyright (c) 2022, GigaDevice Semiconductor Inc."，时间戳集中在 2025-10-20 11:13，无一处改动。
5. **FreeRTOS Kernel V10.5.1 内核 freeRTOS/core+heap+port：35 个文件、1.4 MB**（tasks.c/queue.c/heap_1~5/port.c 等，Amazon 版权，批量时间戳未动）。只收走 include/FreeRTOSConfig.h，另外那份 freeRTOS/FreeRTOSConfig.h（32 KB 堆）是陷阱，不收但写进 known_issues。
6. **立创·梁山派 GD32F470 板级 BSP Hardware/ 其余 10 个目录：19 个文件、2.7 MB**：RGB/（lcd.c、lcd_ui.c、font.h、pic.h 单个 2.3 MB 开机图）、SD/（sdcard.c 2526 行）、touch/、spi/、rtc/、key/、led/、timer/、SDRAM/（exmc_sdram.c 455 行）、adc/（PC1 例程 ADC，2025-11-10 但内容是教程原样）。这些是 GBK 乱码注释、时间戳集中在 2025-10-20 11:14 的例程原件；作者只是调用者。硬配置数值我按 文件:行号 抄进了 hardware_config，代码本身不收。另含 Hardware/SDRAM.zip（8 KB 压缩包）。
7. **Hardware/zzu/（zzu.c 6 KB + zzu.h）**：作者用 LVGL 图片转换器生成的高校校徽点阵。虽是作者产物，但底图属于第三方商标、且全工程未调用，不收。
8. **LVGL/porting/ui_font_cn_48.c（3385 行、156 KB）**：作者用 lv_font_conv 生成的 48 号中文字体，**但字形取自 simhei.ttf（文件头 Opts 行写明）**，字体版权不明，不随仓库分发；改为在 build_instructions 里给出一模一样的重新生成命令。同目录 lv_port_indev/fs_template 三对文件是例程原件（且 lv_port_indev_init 从未被调用），不收。
9. **IDE 缓存与备份**：Project/*.uvoptx、*.uvguix.FFYUN、*.uvguix.Administrator、GD32F450.uvguix.Administrator、EventRecorderStub.scvd、Project/.vscode/（20 KB，含 keil-assistant.log、uv4.log.lock）、keilkilll.bat、Doc/readme.txt（例程说明，GBK 乱码）、空目录 App/。

---

已知问题见 [`docs/KNOWN-ISSUES.md`](../../docs/KNOWN-ISSUES.md)，
需打进 SDK 的改动见 [`docs/BUILD-PATCHES.md`](../../docs/BUILD-PATCHES.md)。
