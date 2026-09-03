# 需手动打进厂商 SDK 的改动

本仓库**不包含**兆易 GigaDevice、沁恒 WCH 的 SDK 源码——它们的许可不允许我们随仓库
再分发。因此作者对 SDK 的少量改动无法以文件形式提供，只能以补丁说明的形式列在这里。

请先从官网获取对应 SDK，再按下面逐条施加。**跳过任何一条都可能导致编译不过或行为异常。**

共 20 条。

---

## 1. `EVT/EXAM/BLE/<工程目录>/APP/include/peripheral.h`

**位置**：第 33 行 `#define SBP_PHY_UPDATE_EVT 0x0010` 之后（左脚原文件第 35 行，右脚第 36 行）

新增一行 `#define SBP_LED_BREATH_EVT      0x0020`。这是作者对 WCH 例程头文件的**唯一**改动，用于注册 PB0 呼吸灯的 TMOS 事件（peripheral.c 的 SBP_LED_BREATH_EVT 分支和 tmos_start_task(...,32) 都依赖它）。不打这一行，peripheral.c 编译不过。因这份头文件带 WCH/南京沁恒版权块且改动仅 1 行，本仓库不收录整份文件。

---

## 2. `EVT/EXAM/BLE/HAL/include/CONFIG.h`

**位置**：`BLE_MAC` 宏定义处（SDK 默认 `#define BLE_MAC FALSE`）

改成 TRUE，才能让 peripheral_main.c:33 的 `const uint8_t MacAddr[6]` 生效（左 84:C2:E4:03:02:02 / 右 84:C2:E4:03:02:03）。**当前作者的构建里它是 FALSE**：`readelf -sW obj/APP/peripheral_main.o` 的 394 条符号里没有 MacAddr，`obj/Peripheral.map` 里也搜不到，即整个 `#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)` 块被预处理掉了。如果上游 GD32VW553 双 Central 想按 MAC 白名单区分左右脚，必须先打这个改动重新编译；否则左右脚只能靠扫描响应包里的名字末字节 'L'/'R' 区分。

---

## 3. `EVT/EXAM/BLE/<工程目录>/Profile/（gattprofile.c/.h、devinfoservice.c/.h）`

**位置**：无需改行，仅需确认版本

**不用改**，原样使用 SDK 里的 WCH 版本即可（本仓库未收录，见 exclude_summary 第 2 条）。只需确认 `Profile/include/gattprofile.h` 里 `SIMPLEPROFILE_SERV_UUID 0xFFE0`(第 36 行)、`SIMPLEPROFILE_CHAR4_UUID 0xFFE4`(第 42 行) 与你手上 SDK 一致——上位机（VW553 Central）就是按这两个 UUID 订阅通知的。顺便说明：`SIMPLEPROFILE_CHAR4_LEN` 声明为 1（第 52 行）不影响 44 字节 Notify，因为 simpleProfile_Notify 走的是 attHandleValueNoti_t 直传，不受属性声明长度约束。

---

## 4. `MSDK/ble/app/app_adapter_mgr.c`

**位置**：在第 282 行 `dbg_print(NOTICE, "=== BLE Adapter enable complete ===\r\n");` 之后插入一行（插入后即成为第 283 行）

【仅「接收」工程需要，「发送」工程不要动】

```c
             dbg_print(NOTICE, "=== BLE Adapter enable complete ===\r\n");
+            ble_scan_enable();
         }
```

作用：BLE 适配器使能完成的那一刻立即开始扫描，这是双 Central 状态机的**唯一冷启动入口**。不加这一行，`user_ble_scan_evt_handler`（app_conn_mgr.c:100）永远收不到广播报告，两只鞋垫一只都连不上，整条链路死在第一步。

注意事项：
1. 全文件 477 行相对 SDK 原件（476 行）**只多这 1 行**，实测 `diff -u 发送/ble/app/app_adapter_mgr.c 接收/ble/app/app_adapter_mgr.c` 只有这一个 hunk，所以本仓库**不收录整份 477 行**，请自行修改 SDK 文件。
2. **不需要加 #include**：`ble_scan.h` 已经在 SDK 原件第 50 行就 include 了。
3. 「发送」板是纯 Peripheral，加了这一行反而会白开扫描抢射频时间。

---

## 5. `MSDK/app/app_cfg.h`

**位置**：第 134 行 `#define CONFIG_BLE_LIB                       BLE_LIB_MIN`

【「接收」工程必须改，「发送」工程保持 BLE_LIB_MIN 即可】

```c
-#define CONFIG_BLE_LIB                       BLE_LIB_MIN
+#define CONFIG_BLE_LIB                       BLE_LIB_MAX
```

SDK 自己在第 131-132 行注明：`BLE_LIB_MIN` = only peripheral and server，`BLE_LIB_MAX` = add central and client usage。「接收」板要做双 Central + GATT Client，必须选 MAX，否则链接的是 peripheral-only 的 BLE 库、`BLE_GATT_CLIENT_SUPPORT` 不会被定义 → `BLE_APP_GATT_CLIENT_SUPPORT` 变 0（ble_app_config.h:90-94）→ app_conn_mgr.c:487-492 里作者调 `ble_gattc_start_discovery()` 的整段被 `#if` 编译掉 → 连上鞋垫但永远不发现服务、永远不开 Notify，一个字节数据都收不到。

**为什么要单独列成 patch**：作者随工程拷出来的这份 app_cfg.h 里写的是 BLE_LIB_MIN，且「发送」「接收」两份 app_cfg.h **逐字节相同**，说明作者是在 SDK 那侧改的、没把改动同步回拷贝目录。所以 app_cfg.h 本身不是作者改过的文件（不收录），但复现者一定会在这里翻车。

---

## 6. `Hardware/RGB/lcd.c（立创·梁山派 GD32F470 RGB 屏例程原件）`

**位置**：42（另见 41 行被注释掉的 216 版本）

本工程按 rcu_pllsai_config(192, 2, 3) 跑：PLLSAI VCO=192 MHz、R 分频 3 → 64 MHz，再经 rcu_tli_clock_div_config(RCU_PLLSAIR_DIV2)(47 行) → 32 MHz 像素时钟。如果你手里的例程是 rcu_pllsai_config(216, 2, 3)（36 MHz 像素时钟），把它改成 192，否则屏幕时序和本工程不一致。

---

## 7. `Hardware/RGB/lcd.c / Hardware/RGB/lcd.h（同上例程原件）`

**位置**：lcd.c:8 与 lcd.h:41

帧缓存声明是 `uint16_t ltdc_lcd_framebuf0[800][480] __attribute__((at(0xC0000000)))`，维度写反了（应为 [480][800]）。总字节数 768000 正好，所以现在能跑；本工程的 disp_flush 也是手工算偏移(lv_port_disp_template.c:130)绕过去的。如果你要按 framebuf[y][x] 直接画点，先把这两行的维度换过来，否则会写到别的行上去。

---

## 8. `Keil 工程 .uvprojx（若你不用我们收录的那份，而是复用自己的工程）`

**位置**：<umfTarg> 与 <ScatterFile> 两个节点

必须 umfTarg=0（取消 'Use Memory Layout from Target Dialog'）并把 Scatter File 指向本仓库的 keil/GD32F450.sct。Target 对话框里填的是 IROM 512 KB + IRAM 192 KB + IRAM2(TCM) 64 KB，而 128 000 B 绘制缓冲 + 71 680 B FreeRTOS 堆 + 48 KB LVGL 池必须落在 0x20000000 起的一段连续 256 KB 里，用对话框布局会链接失败或运行崩。

---

## 9. `freeRTOS/FreeRTOSConfig.h（根目录那份，例程原件）`

**位置**：110

把 32*1024 改成 70*1024，或者直接删掉这份文件。它和 freeRTOS/include/FreeRTOSConfig.h 同名，实际参与编译的是 include/ 那份（quoted include 先搜 FreeRTOS.h 所在目录），改根目录这份不会有任何效果——这是本工程最容易踩的坑。

---

## 10. `LVGL/lv_conf.h（若你从 LVGL 官方 lv_conf_template.h 起步，而不是用我们收录的整份）`

**位置**：285-287 / 776 / 780 / 368-388 / 27 / 52 / 81

需要打开：LV_USE_PERF_MONITOR 1 且 LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_RIGHT；LV_USE_GPU_GD32_IPA 1；LV_GPU_IPA_CMSIS_INCLUDE "gd32f4xx.h"；LV_FONT_MONTSERRAT_16/20/24/30/32/34/36/38 置 1；LV_COLOR_DEPTH 16；LV_MEM_SIZE 48*1024；LV_DISP_DEF_REFR_PERIOD 15。

---

## 11. `LVGL 源码树（v8.4.0）`

**位置**：编译清单

上游 LVGL 没有 GD32 的 GPU 后端。必须把梁山派移植例程里的 LVGL/lv_gpu_gd32_ipa.c 和 lv_gpu_gd32_ipa.h 一起加入编译（它们与 LV_USE_GPU_GD32_IPA=1 配套），否则链接报缺符号。这两个文件本仓库未收录（判为例程原件）。

---

## 12. `User/gd32f4xx_it.c（GD 官方中断模板；本仓库已整份收录，此条供你 diff 自己的工程）`

**位置**：135-137 / 155-157 / 165-168 与 170-182

跑 FreeRTOS 必须把 SVC_Handler、PendSV_Handler、SysTick_Handler 三个空实现注释掉（由 freeRTOS/port/port.c 用宏接过去）；另外要加 `volatile uint8_t g_gpu_state; lv_disp_drv_t *g_disp_drv;` 和 IPA_IRQHandler。

---

## 13. `4.工程代码/5.上位机代码/smart_insole_display10.py（上位机，属另一子系统，此处只给修法）`

**位置**：683-685

把标签表和固件对齐：high_risk_classes 里的 "BLIND_PROBE" 应为 "BLIND PROBE"，warning_classes 的 "DOWNSTAIR"/"UPSTAIR" 应为 "DOWNSTAIRS"/"UPSTAIRS"，safe_classes 的 "STAND"/"SIT" 应为 "STANDING"/"SITTING"（固件表见 Hardware/lcd_my_test/lcd_mytest.c:36-39）。不改则部分类别的上位机告警着色不触发。

---

## 14. `GD32H7_study/GD32H7xx_Demo_Suites_V2.1.0/GD32H7xx_Firmware_Library/CMSIS/GD/GD32H7xx/Source/system_gd32h7xx.c`

**位置**：第 43 行

把 SDK 默认注释掉的这一行**取消注释**：
`#define __SYSTEM_CLOCK_600M_PLL0_IRC64M         (uint32_t)(600000000)`
（SDK 原版是 `//#define __SYSTEM_CLOCK_600M_PLL0_IRC64M ...`，默认走第 53 行已放开的 __SYSTEM_CLOCK_600M_PLL0_HXTAL。）
效果：因为文件第 89 行起的 #if/#elif 链是按 IRC64M → LPIRC4M → HXTAL 的顺序判定的，放开第 43 行后系统时钟改为**用内部 IRC64M 倍频到 600 MHz**，不再依赖 25 MHz 外部晶振。作者的自制板没有可靠的 HXTAL，不打这一行会卡在 system_clock_config() 等 HXTAL 起振。改完后第 53 行留着不用管（被 #elif 短路）。
验证方法：与 GD32H7_study/alll/GD32H7xx_Firmware_Library 下那份未改动的同名文件 diff，只会看到这一处和下面那一处。

---

## 15. `GD32H7_study/GD32H7xx_Demo_Suites_V2.1.0/GD32H7xx_Firmware_Library/CMSIS/GD/GD32H7xx/Source/system_gd32h7xx.c`

**位置**：SystemInit() 内，第 214–215 行（紧接在 `#if defined (SEL_PMU_SMPS_MODE) ... #endif` 之后、`system_clock_config();` 之前）

插入两行：
```
		/* 在这里强制配置为内部 LDO 供电模式，这是最稳妥的保底供电方案 */
    pmu_smps_ldo_supply_config(PMU_LDO_SUPPLY);
```
效果：无条件把 H737 的内核供电切成 LDO 模式。SDK 原版只在定义了 SEL_PMU_SMPS_MODE 时才调 pmu_smps_ldo_supply_config()，而工程的 uvprojx 里 <Define> 是空的、没有定义这个宏，于是供电模式保持复位默认值。作者的板子用的是 LDO 供电电路，不打这一行在 600 MHz 下会跑不稳/上不去。
注意这行的缩进用的是 Tab+空格混排，原样如此。

---

## 16. `（可选，仅当想恢复板上推理计时）h737vmt6_AI1.0/main.c 第 74–76 行 + GD32H7_study/GD32_H7_AI/1.0/nn_model_configure.h 第 41 行`

**位置**：main.c:74-76 / nn_model_configure.h:41

这不是 SDK 改动，是给复现者的提示：nn_model_configure.h:41 定义了 `#define BENCHMARK`，GD_LIB 因此会引用 gd_nn_measure_time_start/get/stop 三个符号；main.c:74-76 把它们全实现成空函数、gd_nn_measure_time_get() 直接 `return 0.0f`。所以**这份固件里 GD_LIB 的耗时统计恒为 0，仓库里不存在任何推理延迟实测值**。若要真实计时，把这三个桩换成 DWT->CYCCNT 读数；若不想要 benchmark 通路，按 nn_model_configure.h:40 的注释 `#undef BENCHMARK` 并同时把 :106 的 debug_print_result 一起处理。

---

## 17. `4.工程代码/5.上位机代码/smart_insole_display10.py:244`

**位置**：self.device_name = "VW553_Gateway"

这不是 SDK 补丁，而是一条必须写进 README 的运行前修改："VW553_Gateway" 这个字符串在整个仓库里只出现在这一行（`grep -rIl VW553_Gateway` 全仓库仅命中本文件），VW553 发送端固件实际广播的名字是 `GD-BLE-<6 字节 MAC>`（见 3.GD32VW553程序/发送/ble/app/app_adapter_mgr.c:79 的 APP_DFLT_DEVICE_NAME 与 :115 的 snprintf 拼接）。因为 发送/app/app_cfg.h:150 里 FEAT_SUPPORT_SAVE_DEV_NAME = 0，设备名不落 flash、每次上电都回到默认值。所以别人拿到代码后必须二选一：(a) 把这一行改成 bleak 扫到的实际名字，或改用 BleakScanner.discover() 按 MAC/服务 UUID 匹配；(b) 每次开机通过 AT 口下发 AT+BLENAME=VW553_Gateway（atcmd_ble.c 支持，但不持久化）。

---

## 18. `4.工程代码/5.上位机代码/smart_insole_display10.py:740`

**位置**：target_dir = r"<作者本机的一个绝对路径>"

硬编码的作者本机 WSL 路径，且紧接着 :743-744 的 os.path.exists/os.makedirs 没有 try 保护。别人跑起来会在自己文件系统里凭空创建 /workspace/... 目录，或在无写权限时直接抛未捕获异常。README 里必须提示改成 os.path.expanduser("~/insole_data") 之类，或干脆传空串让 QFileDialog 用当前目录。

---

## 19. `4.工程代码/6.模型训练代码/Neural_network_architecture.py:29-34`

**位置**：adc_flat = Lambda(lambda t: t[:, 0:672])(inputs) / imu_flat = Lambda(lambda t: t[:, 672:924])(inputs) / Reshape((21,32)) / Reshape((21,12))

672 / 924 / 21 三个数字全是硬编码，只在「EI 窗口 = 700 ms、采样率 = 30 Hz、ADC 32 轴、IMU 12 轴」这一组配置下成立（21 帧 × 32 = 672，21 帧 × 12 = 252，合计 924 = input_length）。别人在 EI 里换窗口长度（例如按上位机滑窗改成 1500 ms → 45 帧 → input_length 1980）后，这段代码不会报错，只会静默切错数据。粘贴前应改成从 EI 注入的 input_length 反推：`FRAMES = input_length // 44; ADC_LEN = FRAMES * 32`，再用 ADC_LEN 做切片和 Reshape。

---

## 20. `4.工程代码/6.模型训练代码/Neural_network_architecture.py:17-18`

**位置**：train_dataset = train_dataset.batch(BATCH_SIZE, drop_remainder=False)

这两行是作者为绕过某个 EI 报错自己加的（注释写着「🚨 修复报错：将原始数据集按批次打包」）。Edge Impulse 不同版本的 expert mode 模板对 train_dataset 是否已 batch 的处理不一致：如果你的 EI 版本已经喂进来 batched dataset，再 .batch() 一次会得到 (batch, batch, features) 的三维输入，Input(shape=(input_length,)) 直接报维度错。粘贴后如果报 shape 错误，先删掉这两行再试。同时 EPOCHS/BATCH_SIZE/LEARNING_RATE（:10-12）是脚本内硬编码的，会覆盖 EI 界面上填的训练轮数和学习率——改参数要改代码，不是改界面。

