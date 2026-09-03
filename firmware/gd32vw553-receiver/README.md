# GD32VW553 无线收发对

GD32VW553 在整条链路里承担「无线中继」，由两颗**互不通信**的独立芯片组成，各跑一份独立固件：

**「接收」板 = BLE 双 Central + UART 汇聚出口。** 上电后自动扫描，用硬编码的左右脚 MAC 白名单锁定两只 CH583 鞋垫，各自建立连接、请求 MTU=247、在 CH583 的私有服务 0xFFE0 / 特征 0xFFE4 上写 CCCD 开 Notify。两路 ASCII 报文（`a:<16路ADC>;m:<6轴IMU>`）进入 FreeRTOS 队列，由 `ble_parse_task` 剥壳解析后写进同一个全局快照 `g_sync_packet`；另一个 `uart_tx_40hz_task` 以固定 40 Hz（25 ms）把快照打包成 94 字节二进制帧（头 0xAA / 尾 0x55），经 UART1 @1.5 Mbps 送给 GD32F470。异步 BLE 流 → 同步定频帧，是这一级的核心设计。

**「发送」板 = 纯字节管道 + BLE Peripheral。** USART0 + DMA 环形缓冲（4096 B）收 GD32F470/H737 回传的字节流，不解析包结构（所以 H737 覆写包尾的推理结果对它完全透明），直接用 SDK 的 datatrans 服务（Service 0x0101 / TX 0x0103 Notify）以广播名 `VW553_Gateway` 推给上位机 Python 面板。

技术要点：单 Central 一拖二的并发状态机（连接锁 `g_is_connecting` + 身份桥梁 `g_connecting_role`，绕开底层 MAC 不可靠的问题）、把连接间隔从 SDK 默认 7.5 ms 放宽到 20 ms 给两条链路留交替槽位、并**一律拒绝**从机发起的参数更新以独占控制权。

## 硬件配置

> 全部数值均从代码读出，`文件:行号` 相对 `4.工程代码/3.GD32VW553程序/`。

## 一、「接收」板 — BLE 双 Central 网关

### 1.1 上游无线链路（← 两只 CH583 鞋垫）

| 项 | 值 | 出处 |
|---|---|---|
| 角色 | BLE Central，同时连 2 个从机 | `接收/ble/app/app_conn_mgr.c:88-89`（`g_conn_idx_left` / `g_conn_idx_right`，0xFF = 未连接） |
| 左脚 MAC 白名单 | 代码数组 `{0x75,0x26,0xCD,0x88,0x19,0x70}`（小端）= **70:19:88:CD:26:75** | `接收/ble/app/app_conn_mgr.c:85` |
| 右脚 MAC 白名单 | 代码数组 `{0x72,0x26,0xCD,0x88,0x19,0x70}`（小端）= **70:19:88:CD:26:72** | `接收/ble/app/app_conn_mgr.c:86` |
| 匹配方式 | 扫描回调里 `memcmp(p_rpt->peer_addr.addr, MAC_xxx, 6)`，硬编码，无运行时配置 | `接收/ble/app/app_conn_mgr.c:100-121` |
| 鞋垫端 GATT 服务 UUID | **0xFFE0** | `接收/ble/app/app_conn_mgr.c:382` |
| 鞋垫端 Notify 特征 UUID | **0xFFE4** | `接收/ble/app/app_conn_mgr.c:383` |
| CCCD 描述符 UUID | **0x2902** | `接收/ble/app/app_conn_mgr.c:384` |
| 开 Notify 写入值 | `0x01 0x00` 写到 CCCD handle | `接收/ble/app/app_conn_mgr.c:399-400` |
| 连接间隔 | `BLE_CONN_FAST_INTV = 16` → **16 × 1.25 ms = 20 ms**（SDK 原值是 6 = 7.5 ms，作者放宽以给两条链路留交替槽位） | `接收/ble/app/app_conn_mgr.c:65`，用于 `:142` |
| Slave latency | `BLE_CONN_FAST_LATENCY = 0` | `接收/ble/app/app_conn_mgr.c:66` |
| 监督超时 | `BLE_CONN_FAST_SUPV_TOUT = 300` → **300 × 10 ms = 3000 ms** | `接收/ble/app/app_conn_mgr.c:67` |
| ce_len min/max | **4 / 4**（= 4 × 0.625 ms = 2.5 ms；SDK 原值是 0/0） | `接收/ble/app/app_conn_mgr.c:142-143` |
| MTU | 服务发现完成后主动 `ble_gattc_mtu_update(conn_idx, 247)` | `接收/ble/app/app_conn_mgr.c:375` |
| 从机参数更新请求 | **一律拒绝** `ble_conn_param_update_cfm(idx, false, 0, 0)`（SDK 原值是 `true, 2, 4`，作者注释「无论鞋垫提什么要求，一律拒绝」「永远掌握控制权」） | `接收/ble/app/app_conn_mgr.c:707`（原行保留在注释 `:704`） |
| 扫描参数 | **作者代码里没设**，沿用 MSDK `ble_scan` 模块默认值；只调 `ble_scan_enable()`/`ble_scan_disable()` | 使能点：`接收/ble/app/app_adapter_mgr.c:283`（冷启动）、`接收/ble/app/app_conn_mgr.c:409`（配完一只脚后找另一只）、`:433`（握手失败重扫）、`:460`（掉线重扫）；关闭点：`接收/ble/app/app_conn_mgr.c:119`（捕获到目标后上锁停扫） |
| 并发锁 | `g_is_connecting`（bool）+ `g_connecting_role`（'L'/'R'/'U'，作者称「身份桥梁」，因为底层回调里拿不到可靠的 MAC） | `接收/ble/app/app_conn_mgr.c:91-92`，判定于 `:467-474` |
| 发射功率查询 | 连上后调 `ble_conn_local_tx_pwr_get` / `ble_conn_peer_tx_pwr_get`，PHY = `BLE_GAP_PHY_1MBPS` | `接收/ble/app/app_conn_mgr.c:482-483` |
| 上游载荷格式 | ASCII 文本，`strstr` 找 `"a:"`（16 路 ADC，分隔符 `,` / `;`）和 `";m:"`（6 轴 IMU，分隔符 `,` / `|`），`strtol` 逐项转换 | `接收/app/main.c:280-291` |

### 1.2 下游 UART 出口（→ GD32F470）

| 项 | 值 | 出处 |
|---|---|---|
| 外设 | **UART1** | `接收/app/main.c:244`（`rcu_periph_clock_enable(RCU_UART1)`） |
| TX 引脚 | **PB15**，`GPIO_AF_7`，AF 推挽，上拉，10 MHz | `接收/app/main.c:245, 247, 248` |
| RX 引脚 | **PA8**，`GPIO_AF_7`，AF 上拉（只配置，代码里未收数据） | `接收/app/main.c:246, 249` |
| 波特率 | **1,500,000** | `接收/app/main.c:252` |
| 帧格式 | 8 位字长 / 1 停止位 / 无校验（8N1） | `接收/app/main.c:253-255` |
| 收发使能 | 收 + 发都开 | `接收/app/main.c:256-257` |
| GPIO 时钟 | RCU_GPIOA + RCU_GPIOB | `接收/app/main.c:242-243` |
| 发送方式 | 逐字节 `usart_data_transmit` + 死等 `USART_FLAG_TBE`（阻塞轮询，无 DMA、无中断） | `接收/app/main.c:324-327` |

### 1.3 94 字节汇聚帧（全项目核心协议，`#pragma pack(1)`）

| 偏移 | 字段 | 类型 | 大小 | 值 |
|---|---|---|---|---|
| 0 | `header` | uint8 | 1 | **0xAA** |
| 1 | `timestamp` | uint32 | 4 | `xTaskGetTickCount()`（FreeRTOS tick，ms） |
| 5 | `left_adc[16]` | **int16** | 32 | 左脚 16 路压阻 |
| 37 | `left_imu[6]` | int16 | 12 | 左脚 BMI270 六轴 |
| 49 | `right_adc[16]` | **int16** | 32 | 右脚 16 路压阻 |
| 81 | `right_imu[6]` | int16 | 12 | 右脚 BMI270 六轴 |
| 93 | `tail` | uint8 | 1 | **0x55**（接收板固定写死，见 known_issues 第 3 条） |

出处：结构体定义 `接收/app/main.c:201-211`；头尾赋值 `接收/app/main.c:307-308`；小端序（RISC-V）。ADC 用 int16 是作者的刻意选择（`:205` 注释「🚨 改为 int16_t，完美容纳 -4096」）。

### 1.4 FreeRTOS 流水线

| 项 | 值 | 出处 |
|---|---|---|
| 队列 | `xQueueCreate(30, sizeof(UartTxMsg_t))`，元素 = `{uint16 len; uint8 data[256];}` = 258 B，共约 7.7 KB | `接收/app/main.c:261`，结构体 `:75-78` |
| 单帧上限 | `len > 250` 直接丢弃 | `接收/app/main.c:225` |
| 任务 `ble_parse` | 栈 1024，优先级 **3**（作者提高优先级以尽快解包） | `接收/app/main.c:264`，实现 `:273-294` |
| 任务 `uart_tx` | 栈 512，优先级 **2**，`sys_ms_sleep(25)` → **40 Hz** 定频 | `接收/app/main.c:267`，实现 `:304-334`，节拍 `:312` |
| 任务 `led_task` | 栈 512，优先级 **2**，20 ms 周期 | `接收/app/main.c:423`，实现 `:390-418` |
| 连接状态标志 | `g_is_ble_connected`（0/1），两脚全断才归 0 | `接收/app/main.c:302`，清零 `接收/ble/app/app_conn_mgr.c:455-457` |
| Notify 拦截器挂载 | `ble_gattc_svc_reg(0xFFE0, insole_data_recv_cb)`，在 init 与每次服务发现完各挂一次 | `接收/ble/app/app_conn_mgr.c:906`（init）、`:387`（每连接） |
| 扫描回调挂载 | `ble_scan_callback_register(user_ble_scan_evt_handler)` | `接收/ble/app/app_conn_mgr.c:899` |

### 1.5 「接收」板指示灯

| 引脚 | 功能 | 配置 | 出处 |
|---|---|---|---|
| **PA3** | 呼吸灯（PWM），未连接时快呼吸（幅度 0.8、步进 0.05）、连上后慢弱呼吸（幅度 0.15、步进 0.02） | AF 模式 + `GPIO_AF_1` → **TIMER1_CH3**，`TIMER_OC_MODE_PWM0`，极性 HIGH，**低电平点亮** | 引脚 `接收/app/main.c:349-350`；定时器 `:359-375`；亮度换算 `:381`、`:406-407`；呼吸逻辑 `:396-404` |
| — | PWM 时基 | 预分频 `SystemCoreClock/1000000 - 1` → **1 MHz** 计数时钟；`period = PWM_PERIOD-1 = 999` → **1 kHz PWM** | `接收/app/main.c:66`（`PWM_PERIOD 1000`）、`:361-363` |
| **PA4** | 系统心跳，纯 GPIO 翻转，每 10 × 20 ms = **200 ms** 翻转一次 | 推挽输出，10 MHz，初始高（熄灭） | `接收/app/main.c:352-353, 356, 411-412` |
| **PA5** | 数据脉冲，**每发出一帧翻转一次** → 40 Hz 发帧 = 20 Hz 方波；灯闪得绝对均匀就说明 40 Hz 节拍没被打断 | 推挽输出，10 MHz，初始高 | `接收/app/main.c:352-353, 356, 331` |
| — | 时钟 | RCU_GPIOA + RCU_TIMER1 | `接收/app/main.c:342-343` |

---

## 二、「发送」板 — BLE Peripheral 网关

### 2.1 上游 UART 入口（← GD32F470）

| 项 | 值 | 出处 |
|---|---|---|
| 外设 | **USART0**（抢的是 SDK 的日志/AT 口，见 build_instructions「日志串口的坑」） | `发送/app/main.c:63` |
| RX 引脚 | **PA8**，`GPIO_AF_2`，推挽，`GPIO_OSPEED_MAX` | `发送/app/main.c:68-71` |
| TX 引脚 | **PB15**，`GPIO_AF_7`，推挽，`GPIO_OSPEED_MAX` | `发送/app/main.c:73-76` |
| 波特率 | **1,500,000** | `发送/app/main.c:82` |
| 接管 SDK 配置 | 先 `usart_disable(USART0)` + `ECLIC_DisableIRQ(USART0_IRQn)`，再清 `USART_CTL0` bit[9:5]（关掉 IDLEIE/RBNEIE/TCIE/TBEIE/PERRIE 全部中断使能） | `发送/app/main.c:79-80, 85` |
| 上电清标志 | 排空 RBNE，清 ORERR / FERR | `发送/app/main.c:89-91, 113-114` |
| DMA 通道 | **DMA_CH4**，子外设 **DMA_SUBPERI4** | `发送/app/main.c:95, 107-108` |
| DMA 方向 | `DMA_PERIPH_TO_MEMORY`，源 `&USART_RDATA(USART0)`，目标 `ring_buf` | `发送/app/main.c:97-99` |
| DMA 模式 | 8 位宽、外设地址不递增、内存地址递增、**循环模式**、优先级 HIGH | `发送/app/main.c:101-105` |
| 环形缓冲 | **4096 字节**，读指针 `head` 靠软件推进，写指针由 `dma_transfer_number_get(DMA_CH4)` 反算 | `发送/app/main.c:28-31, 176-177, 183-187` |
| GPIO 时钟 | RCU_USART0 + RCU_DMA + RCU_GPIOA + RCU_GPIOB | `发送/app/main.c:63-66` |

### 2.2 下游 BLE 链路（→ 上位机 PC）

| 项 | 值 | 出处 |
|---|---|---|
| 角色 | BLE Peripheral（广播 + Notify） | `发送/app/main.c:120-145` |
| **广播名** | **`VW553_Gateway`**（AD 结构 `0x0E 0x09` + 13 个 ASCII 字符） | `发送/app/main.c:137-140` |
| Flags AD | `0x02 0x01 0x06`（LE General Discoverable + BR/EDR Not Supported） | `发送/app/main.c:138` |
| 广播间隔 | `adv_intv = 160` → **160 × 0.625 ms = 100 ms**（与 SDK `APP_ADV_INT_MIN/MAX = 160` 一致，见 `接收/ble/app/app_adv_mgr.h:48-51`） | `发送/app/main.c:131` |
| 广播类型 | `type = 0` = `BLE_ADV_TYPE_LEGACY` | `发送/app/main.c:127`（枚举见 `ble/app/app_adv_mgr.h:41`） |
| 广播属性 | `prop = 0x0003`（可连接 + 可扫描，非定向 legacy） | `发送/app/main.c:128` |
| 本机地址类型 | `own_addr_type = 0` | `发送/app/main.c:129` |
| 发现模式 | `disc_mode = 2` | `发送/app/main.c:130` |
| 信道图 | `ch_map = 0x07`（信道 37 / 38 / 39 全开） | `发送/app/main.c:132` |
| PHY | `pri_phy = 1`、`sec_phy = 1`（1 Mbps） | `发送/app/main.c:133-134` |
| 广播数据长度上限 | `max_data_len = 0x1F` = 31 | `发送/app/main.c:135` |
| **对外 GATT 服务 UUID** | **0x0101**（`BLE_GATT_SVC_DATATRANS_SERVICE`）→ 128 位 `00000101-0000-1000-8000-00805f9b34fb` | `接收/ble/profile/datatrans/ble_datatrans_common.h:41`（SDK 原件，不收录） |
| **RX 特征（PC 写入）** | **0x0102**（`BLE_GATT_SVC_DATATRANS_RX_CHAR`）→ `00000102-0000-1000-8000-00805f9b34fb` | `接收/ble/profile/datatrans/ble_datatrans_common.h:42` |
| **TX 特征（Notify，上位机就订这个）** | **0x0103**（`BLE_GATT_SVC_DATATRANS_TX_CHAR`）→ **`00000103-0000-1000-8000-00805f9b34fb`** | `接收/ble/profile/datatrans/ble_datatrans_common.h:43`；上位机侧对照 `4.工程代码/5.上位机代码/smart_insole_display10.py:244-245` |
| 服务初始化 | `ble_datatrans_srv_init()` | `发送/app/main.c:231` |
| Notify 发送 API | `ble_datatrans_srv_tx(g_conn_idx, tx_buf, tx_len)` | `发送/app/main.c:203` |
| 单包上限 | `tx_buf[244]`，`max_payload = min(g_mtu-3, 244)`，下限夹到 20 | `发送/app/main.c:151, 190-192` |
| MTU 处理 | 连接后**软等 3000 次 1 ms 循环**（作者注释「3 秒」）后直接认定 `g_mtu = 509`；若 `ble_datatrans_srv_tx` 返回 13 则降级回 23（→ 载荷 20 B）重试 | `发送/app/main.c:36, 158-164, 210-213` |
| 连接状态回调 | 自注册 `my_conn_evt_handler` 到 `ble_conn_callback_register` | `发送/app/main.c:39-54, 233` |
| 任务 | `ble_adv`（栈 1024，优先级 2）、`ble_tx`（栈 1024，优先级 2，主循环 `sys_ms_sleep(1)`） | `发送/app/main.c:237-238, 219` |
| 诊断打印 | 每 5000 次循环打一行 `D:mtu=%d rdy=%d avl=%d h=%d t=%d` | `发送/app/main.c:174-180` |
| 初始化范围 | 只调 `util_init` / `user_setting_init` / `ble_init(true)` / `app_dm_init` / `app_adv_mgr_init` / `ble_datatrans_srv_init`；**不启 Wi-Fi、不启 cmd_shell、不启 atcmd** | `发送/app/main.c:224-239` |

## 编译与烧录

## 工具链与 SDK

- **芯片**：GD32VW553（RISC-V Nuclei N22 内核，Wi-Fi 6 + BLE 5.2 SoC）。注意它**不是** ARM 核，Keil MDK 用不了。
- **SDK**：**GD32VW55x_RELEASE_V1.0.3g**（版本号来自作者原始说明 `docs/overlay-note.txt`，别用别的小版本，作者的补丁行号是按这一版对的）。
- **SDK 从哪拿**：兆易创新官网 GD32VW553 资料页 / GD32MCU 开发资料下载区（GD32VW55x SDK），或 GigaDevice 的 GitHub 组织 `gd32-mcu` 相关仓库。**本仓库不再分发 SDK**（1 GB 级、含厂商二进制库与 Wi-Fi 固件）。
- **编译方式**：Nuclei RISC-V GCC（`riscv-nuclei-elf-gcc`）+ CMake ≥ 3.15，用 SDK 自带的 `MSDK/` 构建脚本；也可以用 Nuclei Studio 导入 SDK 工程。SDK 的 `ble/CMakeLists.txt` 是 `file(GLOB_RECURSE ...)` 递归收源码的，**你把文件放进目录就自动参与编译，不用手动加进工程列表**。
- **烧录**：GD-Link / J-Link，或 GigaDevice 的 `gd32vw553_dfu` / `GD32 All-In-One Programmer` 烧 `image-all.bin`。

## 作者代码怎么放进 SDK（两块板各来一遍）

SDK 解压后目标路径是 `GD32VW55x_RELEASE_V1.0.3g/MSDK/`，下面有 `app/` 和 `ble/` 两个目录。作者的原始做法是**整目录覆盖**，但本仓库只保留了改过的文件，所以改成**按文件覆盖**：

### A. 「接收」板（BLE 双 Central 网关）

1. 解压一份干净 SDK，命名成 `MSDK-receiver`。
2. 覆盖两个文件（保持路径）：
   - `gd32vw553-receiver/app/main.c` → `MSDK/app/main.c`
   - `gd32vw553-receiver/ble/app/app_conn_mgr.c` → `MSDK/ble/app/app_conn_mgr.c`
3. 手工套两个 patch（见 patches 字段）：
   - `MSDK/ble/app/app_adapter_mgr.c` 第 282 行后加 `ble_scan_enable();`
   - `MSDK/app/app_cfg.h` 第 134 行改成 `CONFIG_BLE_LIB  BLE_LIB_MAX`
4. **⚠️⚠️ 改 MAC 白名单——这是复现最容易卡死的一步 ⚠️⚠️**
   打开 `MSDK/ble/app/app_conn_mgr.c` **第 85-86 行**：
   ```c
   const uint8_t MAC_LEFT_FOOT[6]  = {0x75, 0x26, 0xCD, 0x88, 0x19, 0x70};
   const uint8_t MAC_RIGHT_FOOT[6] = {0x72, 0x26, 0xCD, 0x88, 0x19, 0x70};
   ```
   这是作者自己那双鞋垫上 CH583 的出厂 MAC，写死在代码里，**没有任何运行时配置手段**（不走 AT 命令、不读 Flash、不看广播名）。
   - 字节序是 **BLE 小端**：数组从低到高是 MAC 的最后一字节到第一字节。上面两行对应的人类可读 MAC 是 **左脚 70:19:88:CD:26:75 / 右脚 70:19:88:CD:26:72**。
   - 换成你自己鞋垫的 MAC：先用 nRF Connect / LightBlue 扫出两只 CH583 的 MAC，然后**倒着**填进数组。填正序会导致「上电后指示灯一直快速呼吸、日志刷不出 `🎯 捕获到智能鞋垫`」——这就是没匹配上。
   - 不改这两行，换了任何一只鞋垫都是永久扫描、零数据。
5. 编译，烧进「接收」板。
6. 自检：串口日志（USART0）应依次出现
   `=== BLE Adapter enable complete ===` → `🎯 捕获到智能鞋垫 [L]！` → `✅ [SYSTEM] 左脚连接成功！` → `🔓 [SYSTEM] 开阀指令发射成功` → `🎯 捕获到智能鞋垫 [R]！` → `🚀 左右双脚全部开阀！双核引擎正式并网！`。
   若停在「开阀失败！未找到 0xFFE4 的 CCCD」，说明 CH583 侧的服务/特征 UUID 和 0xFFE0/0xFFE4 不一致。

### B. 「发送」板（BLE Peripheral 网关）

1. 再解压一份干净 SDK，命名成 `MSDK-sender`。
2. 只覆盖一个文件：`gd32vw553-sender/app/main.c` → `MSDK/app/main.c`。
3. **不要**套上面那两个 patch，也**不要**改 MAC（它不做扫描，MAC 白名单跟它无关）。
4. 编译，烧进「发送」板。
5. 自检：手机 nRF Connect 应能扫到 **`VW553_Gateway`**，连上后订阅 `00000103-0000-1000-8000-00805f9b34fb` 就能看到字节流；串口日志有 `USART0 DMA OK!`、`>>> BLE Connected! <<<`、`MTU ready: 509`、`1st TX! nnB [0]=AA`。

## 拓扑：为什么「发送」和「接收」不是一对（务必写进 README 首屏）

这是全项目最容易被误解的一点。**两块 VW553 之间没有任何无线或有线连接，它们不通信、不配对、不知道对方存在。**

```
左鞋垫 CH583 ─┐
              ├─ BLE(Central) ─→ 「接收」VW553 ─ UART1 1.5Mbps ─→ GD32F470 ─→ GD32H737 推理
右鞋垫 CH583 ─┘                                                        │
                                                                       ↓ (推理结果覆写包尾)
上位机 PC ←─ BLE(Peripheral, VW553_Gateway) ─ 「发送」VW553 ←─ USART0 1.5Mbps ─ GD32F470
```

- 「**接收**」= **BLE 双 Central**，一拖二连两只鞋垫，把无线收来的数据**从 UART 吐出去**给 F470。名字叫「接收」是因为它接收鞋垫数据，不是接收「发送」板的数据。
- 「**发送**」= **BLE Peripheral**，从 UART 吃 F470 回来的字节流，**用 BLE 广播/Notify 发给上位机 PC**。它是纯字节管道，不解析 94 字节包结构，所以 H737 写进包尾的推理类别对它完全透明。
- 两块板之间的「连接」是**经由 GD32F470 的 UART 走线**，且是单向串联：接收板 TX → F470 → (H737 往返) → F470 TX → 发送板 RX。
- 用两颗芯片而不是一颗的原因：GD32VW553 的 BLE 在同一时刻做双 Central 已经吃满射频调度，再叠一路 Peripheral 广播不现实，所以物理上拆成两颗。

## 日志串口的坑

- 「接收」板：数据走 **UART1**（PB15），SDK 的 `LOG_UART`（定义在 SDK 的 `log_uart.h`，未随本仓库分发，默认是 USART0）保持独立，可以正常接 USB-TTL 看日志。
- 「发送」板：作者把 **USART0 抢来当数据口**了（`发送/app/main.c:78-86` 先 `usart_disable(USART0)` + `ECLIC_DisableIRQ(USART0_IRQn)`，注释写明「关闭 SDK 的 USART0 中断」，然后把波特率拉到 1,500,000）。这意味着**它的调试日志和数据入口共用一个外设**，看日志需要一台能跑 **1.5 Mbaud** 的 USB-TTL（CH340 之类的低速片子会满屏乱码），而且日志输出会从 PB15 喷出去——如果你把 PB15 也接到 F470 的 RX 上，F470 会收到日志文本混进数据流。建议：调试期只连 PA8（数据入）和 PB15（看日志），量产/演示时把 PB15 从 F470 断开。

## 关于 `接收/image-all.bin`（1.0 MB 已烧录固件镜像）

**建议：不放进 git 仓库，改成挂在 GitHub Release 的附件里，并在 `.gitignore` 里加 `*.bin`。**理由：

1. **它比源码新，而且方向还相反。**镜像内嵌构建时间 `2026/06/13 19:32:57`，但 `接收/ble/app/app_conn_mgr.c` 的 mtime 是 `2026-06-16 01:28:22` —— 源码在镜像烧完之后又被改过。同时镜像里存在一条源码里**根本不存在**的字符串 `⚠️ 垃圾包拦截！期望 %d 字节，实际收到 %d 字节`（`grep -rn 垃圾包 接收 发送` 零命中），说明烧进去的那一版有一段长度校验逻辑，后来从源码里被删掉了。两边都不是彼此的镜像，任何人「烧 bin 对照读源码」都会被误导。
2. **烧了也没用。**左右脚 MAC 已经写死编进二进制（`70:19:88:CD:26:75` / `...:72`），别人的鞋垫 MAC 不同，烧上去只会永久扫描、零数据输出。它对第三方的验证价值接近零。
3. **它是链接后的整镜像**，里面绝大部分是 GigaDevice 的 Wi-Fi/BLE 协议栈目标码与 bootloader（`strings` 里全是 `/MSDK/macsw/...` 路径），再分发二进制的许可边界比再分发带 BSD 头的源码模糊得多。
4. 1.0 MB 二进制进 git 历史后删不掉，会永久拖慢 clone。

如果作者仍想保留它（毕竟是唯一的「当时确实跑通了」的物证），折中做法：打一个 tag，作为 Release asset 上传，附件说明里写清「2026-06-13 快照，含硬编码 MAC，且与当前源码不一致（源码后于它修改，且镜像含源码已删除的垃圾包校验逻辑），仅作历史归档，不要用于复现」。

## 本目录未收录哪些文件，为什么

两个工程合计 **765 个文件 / 13.6 MB**（发送 382 files / 6.3 MB，接收 383 files / 7.3 MB）。收录 **4 个文件 / 约 1,610 行 / 约 62 KB**，其余 **761 个文件 / 约 13.5 MB 全部排除**。逐类说明（`diff -rq 发送 接收` 只报 4 处不同，已确认两工程除 4 个文件外逐字节相同，所以除了这 4 个，两边都是同一批 SDK 原件）：

| 排除对象 | 文件数 | 体积 | 理由 |
|---|---|---|---|
| `ble/mesh/`（两份） | 466 | 约 8.4 MB | GigaDevice/Zephyr 派生的 BLE Mesh 协议栈全套（api/models/port/src/example_cfg）。mtime 全部是 2026-04-13 11:12:18 或 2025-03-28 15:44:24 两个批量时间戳，零改动。本项目既不用 Mesh 也不用 WiFi，纯属 SDK 自带包袱，是最大的一坨。 |
| `app/`（两份，扣掉 2 个 main.c） | 78 | 约 1.6 MB | SDK 的 atcmd 全家桶（atcmd.c 65 KB、atcmd_tcpip.c 124 KB、atcmd_httpc/mqtt/azure/wifi）、mqtt_app/、iperf.c、ping.c、ota_demo.c、cmd_shell.c(71 KB)、802_1x_EAP_TLS_certs.c、app_cfg.h、rftest_cfg.h、CMakeLists.txt。全部零改动，本项目一个都没调用（作者甚至在 接收/app/main.c:94-98 把 atcmd_init() 注掉了）。 |
| `ble/app/`（两份，扣掉 1 个 app_conn_mgr.c + 1 个 app_adapter_mgr.c） | 118 | 约 1.5 MB | SDK BLE 应用管理层：app_adv_mgr、app_scan_mgr、app_sec_mgr、app_dev_mgr、app_dfu_*、app_bass/cscss/diss/hogp/prox、atcmd_ble.c(75 KB)、app_virtual_hci、ble_init.c、ble_app_config.h 等。链路运行必需但**一行未改**，由 SDK 提供。 |
| `ble/profile/`（两份） | 96 | 约 1.0 MB | SDK profile 实现。其中 `datatrans/`（ble_datatrans_common.h、ble_datatrans_srv.c/.h、ble_datatrans_cli.c/.h）是「发送」板对外链路的实际承载者，但**零改动**（mtime 2026-04-13 / 2025-03-28 批量戳），所以不收录，只把它定义的 UUID 抄进 hardware_config，供上位机订阅用。 |
| `接收/image-all.bin` | 1 | 1.0 MB | 已烧录固件镜像，**建议不进 git**，理由见 known_issues 第 3、11 条与 build_instructions 结尾。 |
| `app/_build_date.h`（两份） | 2 | 214 B | SDK 构建脚本自动生成（文件里自己写了 "Do not change the content here, it's auto generated"），两工程唯一差异只是时间戳字符串，属编译产物。 |

**编译产物 / IDE 缓存 / __pycache__ / 备份文件：这两个目录里本来就没有。**实测 `find . -type f \( -name "*.o" -o -name "*.dep" -o -name "*.map" -o -name "*.axf" -o -name "*.bin" -o -name "*.hex" -o -name "*.uvoptx" -o -name "*备份*" -o -name "*副本*" -o -name "*.bak" \)` 只命中 `接收/image-all.bin` 一个。原因是这两个目录不是完整 Keil/IAR 工程，只是准备覆盖进 SDK 的 `app/` + `ble/` 两个源码目录，工程文件与 build 目录都在 SDK 那边（SDK 不在本仓库内）。

---

已知问题见 [`docs/KNOWN-ISSUES.md`](../../docs/KNOWN-ISSUES.md)，
需打进 SDK 的改动见 [`docs/BUILD-PATCHES.md`](../../docs/BUILD-PATCHES.md)。
