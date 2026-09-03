# THIRD-PARTY NOTICES / 第三方组件声明

本仓库（「柔感智行 — 基于 GD32 的面向独居老人穿戴式防跌倒预警系统」）整体以
**Apache License 2.0** 发布（见根目录 `LICENSE`），`dataset/` 目录下的数据以
**CC BY 4.0** 发布（见 `dataset/LICENSE`）。

除此之外，仓库中包含或派生自下列第三方组件。各组件的原始版权声明与许可条款
均保留在对应源文件头部，并汇总于本文件。许可全文见 `licenses/` 目录。

本文件的信息由逐文件核查文件头版权声明得出，而非依据文件名推断。

---

## 目录

1. [Bosch Sensortec BMI270 传感器驱动](#1-bosch-sensortec-bmi270-传感器驱动) — BSD-3-Clause
2. [GigaDevice GD32VW55x MSDK](#2-gigadevice-gd32vw55x-msdk) — BSD-3-Clause
3. [GigaDevice GD32F4xx 固件库](#3-gigadevice-gd32f4xx-固件库) — BSD-3-Clause
4. [GigaDevice GD32 AI 模型转换工具生成代码](#4-gigadevice-gd32-ai-模型转换工具生成代码) — 见条目说明
5. [FreeRTOS Kernel](#5-freertos-kernel) — MIT
6. [LVGL](#6-lvgl) — MIT
7. [沁恒 WCH CH58x BLE 例程](#7-沁恒-wch-ch58x-ble-例程) — 厂商限定用途许可
8. [未随仓库分发、需自行获取的组件](#8-未随仓库分发需自行获取的组件)
9. [许可全文](#9-许可全文)

---

## 1. Bosch Sensortec BMI270 传感器驱动

**许可**：BSD-3-Clause（完整许可正文保留在每份源文件头部第 1–37 行）

**仓库内路径**：`firmware/ch583-insole/third-party/bosch-bmi270/`

| 文件 | 版本 | 日期 | 版权 |
|---|---|---|---|
| `bmi2.c` | v2.113.0 | 2025-04-22 | Copyright (c) 2025 Bosch Sensortec GmbH |
| `bmi2.h` | v2.113.0 | 2025-04-22 | Copyright (c) 2025 Bosch Sensortec GmbH |
| `bmi2_defs.h` | v2.113.0 | 2025-04-22 | Copyright (c) 2025 Bosch Sensortec GmbH |
| `bmi270.c` | v2.86.1 | 2023-05-03 | Copyright (c) 2023 Bosch Sensortec GmbH |
| `bmi270.h` | v2.86.1 | 2023-05-03 | Copyright (c) 2023 Bosch Sensortec GmbH |

**上游**：https://github.com/boschsensortec/BMI270_SensorAPI

**修改情况**：未修改。左右足两份副本 md5 一致
（`bmi2.c` ea0917186c058a5f61199329cc0129bf、`bmi2_defs.h` 8f6741fb30f7633b12a7a107506b9fcf、
`bmi2.h` 5fdeadd133035aa96a5b99a1fdf143fc、`bmi270.c` 54521bab54fe1447c387015f995af5c1、
`bmi270.h` 39e1e61ea754aae73cd0a83e843ccc9e），仓库内只保留一份。

**为何随仓库分发而非让使用者自行下载**：本项目使用的是 `bmi2.*` v2.113.0 与
`bmi270.*` v2.86.1 的组合，上游并不以这个版本组合成对发布。自行拉取会得到
另一套版本，接口未必兼容。BSD-3-Clause 明确允许源码再分发，条件是保留版权
声明、条件列表与免责声明——本仓库中这些文件的头部原样保留，条件已满足。

许可全文：`licenses/BSD-3-Clause-Bosch.txt`

---

## 2. GigaDevice GD32VW55x MSDK

**许可**：BSD-3-Clause（完整许可正文保留在每个源文件头部）
**版权**：Copyright (c) 2023, GigaDevice Semiconductor Inc.
**上游 SDK 版本**：`GD32VW55x_RELEASE_V1.0.3g`（MSDK 目录）

本仓库中下列文件**派生自**该 SDK，并由本项目作者做了实质修改：

| 仓库内路径 | 派生自 SDK 中的 | 作者改动 |
|---|---|---|
| `gd32vw553-receiver/ble/app/app_conn_mgr.c` | `MSDK/ble/app/app_conn_mgr.c` | 9 个 hunk，+213 / −29 行（约 23%）：新增双 Central 扫描白名单与连接锁、Notify 拦截转发、MTU 交换与手动写 CCCD、重连逻辑重写 |
| `gd32vw553-receiver/app/main.c` | `MSDK/app/main.c` | 新增约 250 / 425 行（约 59%）：UART 汇聚出口、94 字节 CombinedDataPacket 定义、双任务流水线、呼吸灯 |

作者贡献的精确边界另见 `patches/receiver-app_conn_mgr.patch`
（由 `diff -u <SDK原件> app_conn_mgr.c` 生成）。

`gd32vw553-sender/app/main.c` **不**派生自该 SDK：作者将 SDK 的 `main.c` 整份
丢弃后重写，文件内不含任何 GigaDevice 版权声明或代码，以 Apache-2.0 发布。

许可全文：`licenses/BSD-3-Clause-GigaDevice.txt`

---

## 3. GigaDevice GD32F4xx 固件库

**许可**：BSD-3-Clause（完整许可正文保留在每个源文件头部第 11–36 行）
**版权**：Copyright (c) 2022, GigaDevice Semiconductor Inc.
**版本**：firmware for GD32F4xx, V3.0.0 (2022-03-09)

| 仓库内路径 | 作者改动 |
|---|---|
| `firmware/gd32f470-hub/user/gd32f4xx_it.c` | 为运行 FreeRTOS 注释掉 SVC/PendSV/SysTick 三个 handler；新增 `g_gpu_state` / `g_disp_drv` 全局；新增 `IPA_IRQHandler` |
| `firmware/gd32f470-hub/user/main.h` | 新增 `UART_RX_BUFFER_SIZE` 与若干全局声明 |

许可全文：`licenses/BSD-3-Clause-GigaDevice.txt`

---

## 4. GigaDevice GD32 AI 模型转换工具生成代码

**版权**：Copyright (c) 2023, GigaDevice Semiconductor Inc.
**仓库内路径**：`gd32h737-inference/model/nn_model_configure.c`、`nn_model_configure.h`

**⚠️ 请注意本条目与上面两条 GigaDevice 条目的差别。**

这两个文件是兆易 GD32 AI 转换工具的**生成输出**。它们头部的兆易声明**只包含
免责条款，不包含再分发授权段**——即没有同 SDK 其它文件那样的
"Redistribution and use in source and binary forms ... are permitted provided that ..."
正文。（作为对照：同一套 AI 库的 `inc/gd_nn_layer.h`、`nn_forward.h`、
`gd_nn_interface.h`、`gd_nn_support.h`、`gd_nn_tensor.h`、`gd_nn_basic_types.h`
六份头文件均带完整 BSD-3-Clause 授权段。）

**内容权属是混合的**：
- `nn_model_configure.c` 共 3835 行，其中 3746 行（97.7%）是
  `model_paras_arr[33960]` 与 `model_paras_data[40960]` 两个数组
  （合计 74 920 字节），**这些权重由本项目作者自行采集数据训练得出**。
- 其余约 89 行为工具生成的样板（算子回调数组、算子名表、各类缓冲区声明、
  优化开关），版权归 GigaDevice。
- `nn_model_configure.h` 中的 `INPUT_SIZE 1980`、`OUTPUT_SIZE 8`、
  `STATIC_BUFFER_PEAK_SIZE 13344`、`AI_LAYER_MEM_SIZE 1180` 由作者的模型尺寸决定，
  模板本身由工具生成。

**本仓库的处理**：完整保留 GigaDevice 的原始声明（一字未改），并在此明确标注
文件的生成来源与混合权属。若 GigaDevice Semiconductor Inc. 认为此处的再分发
超出其许可范围，请通过仓库 Issue 联系，我们将立即移除这两个文件，改为提供
原始权重数据与重新生成的操作步骤。

---

## 5. FreeRTOS Kernel

**许可**：MIT（完整许可正文保留在源文件头部第 5–19 行）
**版权**：Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
**版本**：FreeRTOS V202212.00
**上游**：https://www.FreeRTOS.org / https://github.com/FreeRTOS

**仓库内路径**：`firmware/gd32f470-hub/freertos/FreeRTOSConfig.h`

**修改情况**：相对例程原件，仅第 110 行将 `configTOTAL_HEAP_SIZE`
由 32 KB 改为 70 KB（71680 字节）。

注：本仓库只收录配置文件，不收录 FreeRTOS 内核源码本身；内核请自行从上游获取。

许可全文：`licenses/MIT-FreeRTOS.txt`

---

## 6. LVGL

**许可**：MIT
**版权**：Copyright (c) 2021 LVGL Kft
**版本**：v8.4.1（依 `LVGL/lvgl.h` 中 `LVGL_VERSION_MAJOR/MINOR/PATCH` 实测；
注意 `lv_conf.h` 头部自称 "v8.4.0"，属上游模板未同步，非本仓库改动）
**上游**：https://github.com/lvgl/lvgl

**仓库内路径**：
| 文件 | 派生自 | 作者改动 |
|---|---|---|
| `firmware/gd32f470-hub/lvgl-port/lv_port_disp_template.c` | LVGL 官方 `lv_port_disp_template.c` | 实质重写：两块 800×40 双绘制缓冲、`full_refresh=0`、`disp_flush` 直接操作 GD32 IPA 寄存器将 SRAM 块搬入 SDRAM 显存并忙等完成 |
| `firmware/gd32f470-hub/lvgl-port/lv_conf.h` | LVGL 官方 `lv_conf_template.h` | 全面配置：`LV_COLOR_DEPTH 16`、`LV_MEM_SIZE 48K`、刷新周期 15 ms、`LV_USE_PERF_MONITOR 1`、按需启用 montserrat 16/20/24/30/32/34/36/38、`LV_USE_GPU_GD32_IPA 1` |

**重要说明**：LVGL 的 porting 模板与 `lv_conf_template.h` 在上游即**不含逐文件
版权头**，其 MIT 许可正文只存放于 LVGL 仓库根目录的 `LICENCE.txt`。因此本仓库
在 `licenses/MIT-LVGL.txt` 中提供该许可全文，以履行 MIT 关于「版权声明与许可
声明须包含于软件的所有副本或实质部分中」的要求。

注：本仓库只收录移植层与配置文件，不收录 LVGL 库源码本身；库请自行从上游获取
对应版本。

许可全文：`licenses/MIT-LVGL.txt`

---

## 7. 沁恒 WCH CH58x BLE 例程

**版权**：Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
**原始声明全文**（见 WCH CH583 EVT 包中 `EXAM/BLE/Peripheral/APP/` 下各文件头部）：

```
/********************************** (C) COPYRIGHT *******************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/
```

**这不是开源许可**。它是厂商的限定用途声明：允许使用与修改，但限定于配合
南京沁恒生产的微控制器，且未给出明确的再分发条款。

**仓库内路径**：
| 仓库内路径 | 派生自 WCH EVT 包中的 |
|---|---|
| `firmware/ch583-insole/{left,right}/app/peripheral.c` | `EXAM/BLE/Peripheral/APP/peripheral.c` |
| `firmware/ch583-insole/{left,right}/app/peripheral_main.c` | `EXAM/BLE/Peripheral/APP/peripheral_main.c` |

`peripheral.c` 共 763 行，其中第 402–763 行的 13 个函数
（`Peripheral_Init`、`peripheralInitConnItem`、`Peripheral_ProcessEvent`、
`Peripheral_ProcessGAPMsg`、`Peripheral_ProcessTMOSMsg`、`Peripheral_LinkEstablished`、
`Peripheral_LinkTerminated`、`peripheralRssiCB`、`peripheralParamUpdateCB`、
`peripheralStateNotificationCB`、`performPeriodicTask`、`peripheralChar4Notify`、
`simpleProfileChangeCB`）派生自 WCH 例程，约占全文 47%。
第 1–401 行（FSR 采样、BMI270 适配、软 I2C ping 引擎、44 字节数据包、
通道映射表）为本项目作者原创。

`peripheral_main.c` 共 92 行，其中 `MEM_BUF` / `MacAddr` 定义、
`Main_Circulation()` + `TMOS_SystemProcess()`、以及 `main()` 中
`PWR_DCDCCfg` → `SetSysClock` → `CH58X_BLEInit` → `HAL_Init` →
`GAPRole_PeripheralInit` → `Peripheral_Init` 的调用序列派生自 WCH 例程；
`UART0_Printf`、PRINT 宏劫持、UART0 引脚初始化、`LL_SetTxPowerLevel` 为作者新增。

本仓库中这些文件的头部已恢复上述 WCH 版权声明，并在其下标注了本项目的修改。
若沁恒微电子认为此处的再分发超出其许可范围，请通过仓库 Issue 联系，
我们将立即改为仅提供针对官方 EVT 包的补丁文件。

**使用者须知**：WCH CH583 SDK（`StdPeriphDriver`、`LIB/libCH58xBLE.a`、`RVMSIS`、
`Startup`、`Ld/Link.ld` 等）**不随本仓库分发**，请自行从沁恒官网获取 CH583 EVT
评估开发包，编译方法见 `firmware/ch583-insole/README.md`。

---

## 8. 未随仓库分发、需自行获取的组件

下列组件为编译本项目所必需，但因许可限制或体积原因**不包含在本仓库中**，
请自行从官方渠道获取：

| 组件 | 用于 | 获取方式 | 不随仓库分发的原因 |
|---|---|---|---|
| `GD_LIB_CM7_v212.lib`（664 KB） | GD32H737 端侧推理 | 兆易 GigaDevice GD32 AI 开发套件 | **闭源二进制，未附任何版权声明或许可授权**。经核查该库为 ar 归档（196 个目标文件，armclang 6.24 编译），内部无任何 copyright/license 字符串；且从其配套头文件可知它静态链接了 Arm CMSIS-NN（Apache-2.0）与 Google gemmlowp（Apache-2.0），而 Apache-2.0 §4 要求二进制再分发时随附许可副本与 NOTICE，该库无法满足。放置路径见 `gd32h737-inference/README.md`。 |
| GD32H7xx AI 库头文件（`gd_nn_*.h`、`nn_forward.h`） | GD32H737 端侧推理 | 同上，随 AI 开发套件提供 | 随套件一并获得，无需单独入库（这些头文件本身带完整 BSD-3-Clause 授权）。 |
| `GD32VW55x_RELEASE_V1.0.3g` MSDK | GD32VW553 收发对 | 兆易官网 | 完整 SDK 体积过大；且其 `ble/mesh/` 子树混有 Intel / Nordic Semiconductor 的 Apache-2.0 蓝牙 Mesh 协议栈（数百文件），随附其 LICENSE/NOTICE 义务对本项目无意义。覆盖方法见 `gd32vw553-gateway/docs/overlay-note.txt`。 |
| WCH CH583 EVT 评估开发包 | CH583M 鞋垫采集端 | 沁恒官网 | 厂商限定用途许可，见第 7 条。 |
| GigaDevice GD32F4xx 固件库、GD32H7xx DFP Pack (1.4.0) | F470 / H737 | 兆易官网 / Keil Pack Installer | 体积与版本管理原因。 |
| FreeRTOS Kernel V202212.00 源码 | GD32F470 汇聚枢纽 | https://github.com/FreeRTOS/FreeRTOS-Kernel | 本仓库只收录 `FreeRTOSConfig.h`。 |
| LVGL v8.4.1 源码 | GD32F470 UI | https://github.com/lvgl/lvgl | 本仓库只收录移植层与 `lv_conf.h`。 |

另：仓库历史中曾包含 Edge Impulse C++ SDK 的多份副本（BSD-3-Clause-Clear，
内嵌 CMSIS / TensorFlow Lite Micro / gemmlowp / ruy / flatbuffers 等 Apache-2.0
组件及 `libmli.a` 二进制）。这些是早期练手残留，**与最终上板模型无关**
（最终推理使用兆易 AI 库），已全部排除。

---

## 9. 许可全文

### 9.1 BSD-3-Clause（Bosch Sensortec 版，摘自 `bmi2.c` 文件头，原文照录）

```
Copyright (c) 2025 Bosch Sensortec GmbH. All rights reserved.

BSD-3-Clause

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

（`bmi270.c` / `bmi270.h` 同文，版权行为 `Copyright (c) 2023 Bosch Sensortec GmbH.`）

### 9.2 BSD-3-Clause（GigaDevice 版，摘自 `app_conn_mgr.c` 文件头，原文照录）

```
Copyright (c) 2023, GigaDevice Semiconductor Inc.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software without
   specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
```

（GD32F4xx 固件库文件同文，版权行为 `Copyright (c) 2022, GigaDevice Semiconductor Inc.`）

### 9.3 MIT（FreeRTOS 版，摘自 `FreeRTOSConfig.h` 文件头，原文照录）

```
FreeRTOS V202212.00
Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
the Software, and to permit persons to whom the Software is furnished to do so,
subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

https://www.FreeRTOS.org
https://github.com/FreeRTOS
```

### 9.4 MIT（LVGL）

> **⚠️ 落盘时请替换本节。** LVGL 的模板文件本身不带版权头，本仓库中没有
> LVGL 的 `LICENCE.txt`。请从你实际使用的 LVGL v8.4.1 发行包根目录
> 复制 `LICENCE.txt` 全文到 `licenses/MIT-LVGL.txt`，并把该文件内容
> 原样粘贴到此处，替换下面这段占位文本。版权行应为
> `Copyright (c) 2021 LVGL Kft`。

```
MIT License

Copyright (c) 2021 LVGL Kft

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

---

## 上位机 Python 依赖

`host-app/smart_insole_display10.py` 为本项目作者原创，以 Apache-2.0 发布。
它通过 `import` 使用下列第三方库，这些库**不随本仓库分发**，请按
`host-app/requirements.txt` 自行安装：

| 库 | 许可 | 备注 |
|---|---|---|
| PyQt5 / PyQt5.QtChart | **GPLv3 或商业双许可**（Riverbank Computing） | ⚠️ 见下方说明 |
| bleak | MIT | |
| qasync | BSD-2-Clause | |
| numpy | BSD-3-Clause | |
| aiohttp | Apache-2.0 | |

**⚠️ 关于 PyQt5**：以源码形式分发本上位机脚本不产生 GPL 义务，因此本文件
可以 Apache-2.0 发布。但若将本脚本用 PyInstaller / cx_Freeze 等工具打包为
可执行文件并对外分发，该可执行文件将落入 GPLv3，届时须整体以 GPLv3 开源，
或向 Riverbank Computing 购买商业授权。

---

## 数据集

`dataset/` 下的全部 CSV 由本项目作者使用自建硬件采集，不含第三方版权内容，
以 **CC BY 4.0** 发布（见 `dataset/LICENSE`）。

数据为单被试、单日采集，被试为健康成年人；其中 `band`（HEMIPLEGIC 偏瘫步态）、
`eye`（BLIND_PROBE 盲态探步）、`unnormal_singol`（ANTALGIC 痛性跛行）三类为
**健康被试模拟**，**非真实患者数据**，不得作为临床数据引用。
数据已去标识化，不含姓名、编号或其它可识别个人信息。

---

## 声明变更与联系方式

本文件基于对每个收录文件文件头版权声明的逐一核查编写。若你认为其中任何一项
标注有误、或本仓库对你的作品的再分发超出了许可范围，请通过仓库 Issue 联系，
我们将尽快更正或移除。
