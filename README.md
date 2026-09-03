<div align="center">

# 柔感智行 · gd32-gait-insole

**一双能在跌倒发生之前读出前兆的智能鞋垫**

基于 GD32 的面向独居老人穿戴式防跌倒预警系统 · 四芯异构 · 端侧推理 · 不上云

[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![Dataset](https://img.shields.io/badge/dataset-CC%20BY%204.0-lightgrey.svg)](dataset/)
[![MCU](https://img.shields.io/badge/MCU-CH583M%20%C2%B7%20GD32VW553%20%C2%B7%20GD32F470%20%C2%B7%20GD32H737-orange.svg)](#硬件构成)
[![Status](https://img.shields.io/badge/status-reference%20implementation-yellow.svg)](#这个仓库是什么不是什么)

第二十一届中国研究生电子设计竞赛 · 全国总决赛团队二等奖 · 华中分赛区团队一等奖

**[▸ 项目展示页](https://zx-ye.github.io/gd32-gait-insole/)** · 含系统架构图、实机演示视频与硬件实拍

</div>

<sub>
关键词：GD32 步态识别 · 柔性压阻传感鞋垫 · 端侧 AI 推理 · TinyML · 防跌倒预警 · 智能可穿戴 ·
双流 1D-CNN · BLE 双 Central 一拖二 · 兆易 GD_LIB · GD32H737 · GD32VW553 · GD32F470 · CH583M · BMI270 ·
gait recognition, flexible piezoresistive insole, on-device inference, edge AI, fall prevention, wearable sensing
</sub>

---

## 这个仓库是什么，不是什么

**是**：一套已经跑通的四芯异构可穿戴系统的**完整参考实现**。四种 MCU 的固件、上位机、
模型结构与训练数据全在这里，接线参数、寄存器配置、协议字段都标了出处行号。

**不是**：一个开箱即可复现的硬件项目。**没有原理图、没有 PCB 文件、没有含单价的 BOM**；
鞋垫的柔性压阻阵列是自制材料。你没法照着这个仓库做出一双一样的鞋垫。

**为什么还值得开源**：里面有三块东西在中文互联网上几乎搜不到能跑的参考实现——

| 你在找什么 | 看哪里 |
|---|---|
| **GD32VW553 做 BLE 双 Central 一拖二** | [`firmware/gd32vw553-receiver/`](firmware/gd32vw553-receiver/) — 硬编码 MAC 认亲、MTU 247 交换、手工遍历 GATT 表找 CCCD、连接间隔 20 ms 并拒绝从机的参数更新请求 |
| **GD32H737 用兆易 GD_LIB 跑端侧推理** | [`firmware/gd32h737-inference/`](firmware/gd32h737-inference/) — `nn_model_init` / `nn_model_invoke` 这条路线，不是 TFLite Micro 也不是 Edge Impulse SDK |
| **CH583 高阻抗多路压力采集** | [`firmware/ch583-insole/`](firmware/ch583-insole/) — 模拟开关扩通道、丢弃假读冲残荷、4 倍过采样、阵列供电门控 |

---

## 系统怎么工作

```
 左/右鞋垫 CH583M ×2                    ┌─ 每足 16 路柔性压阻 + BMI270 六轴
 RISC-V 60 MHz                          └─ 44 B/包 @ 80 Hz
        │
        │  BLE 5.0 · 硬编码 MAC 认亲 · MTU 247 · 连接间隔 20 ms
        ▼
 GD32VW553 #1「接收」                   BLE 双 Central 一拖二
 RISC-V + BLE 5.2                       拼成 94 B CombinedDataPacket @ 40 Hz
        │
        │  UART 1.5 Mbps
        ▼
 GD32F470 汇聚枢纽                      FreeRTOS · DMA + IDLE 中断 + 乒乓双缓冲
 Cortex-M4 200 MHz                      30 Hz 节拍重采样 · 临界区内一分二双发
        │                               800×480 LVGL 仪表盘（TLI + IPA 加速）
        ├──────────────────┐
        │ USART1 94 B      │ UART3 → GD32VW553 #2「发送」→ BLE → PyQt 上位机
        ▼                  
 GD32H737 端侧推理                      45 帧窗口（1.5 s @ 30 Hz）· 步长 15 帧
 Cortex-M7 600 MHz                      双流 1D-CNN → 8 类步态
        │
        └─→ 分类结果覆写 94 B 包的**包尾字节**，沿原路回传给 F470
            不另开通道 · 天然帧同步 · 驱动屏显与一分钟滚动风险窗
```

三级降速 **80 Hz → 40 Hz → 30 Hz** 各有原因：鞋垫端过采样换信噪比，VW553 按 BLE 连接间隔
打快照，F470 用固定节拍把两条异步链路对齐成均匀时间轴。

### 8 类步态

索引即模型输出顺序（Edge Impulse 按字母序排类）：

| # | 类别 | 英文标签 | 风险分级 |
|---|---|---|---|
| 0 | 痛性跛行 | `ANTALGIC` | 高危 |
| 1 | 盲态探步 | `BLIND PROBE` | 高危 |
| 2 | 正在下楼 | `DOWNSTAIRS` | 预警 |
| 3 | 偏瘫步态 | `HEMIPLEGIC` | 高危 |
| 4 | 静止坐立 | `SITTING` | 安全 |
| 5 | 静止站立 | `STANDING` | 安全 |
| 6 | 正在上楼 | `UPSTAIRS` | 预警 |
| 7 | 正常走路 | `WALKING` | 安全 |

高危不是单帧触发。汇聚端维护一个借鉴 ICU 监护仪思路的**一分钟滚动风险窗**：
每 1800 帧（30 Hz × 60 s）结算一次高危占比，超 30% 红、超 10% 黄。用时间维度上的
持续性压掉单帧误报。

---

## 性能数字，以及它们的边界

| 指标 | 数值 | 来源与限定 |
|---|---|---|
| 步态识别准确率 | **95.5%** | ⚠️ Edge Impulse **验证集**，答辩材料自述 |
| 三类病理步态召回率 | 95.2% – 96.4% | 同上（痛性跛行 96.3 / 偏瘫 96.4 / 盲态探步 95.2） |
| 召回率最低的类别 | 正常走路 87.0% | 主要被误判为偏瘫 6.5%、盲态探步 4.3% |
| Flash 占用 | 173.4 KB | H737 链接产物 `.map` 实测（Total ROM 177 508 B） |
| 静态 RAM | 110.2 KB | 同上（RW + ZI = 112 808 B） |
| 模型参数量 | 18 216 | 按网络结构手算 |
| 部署权重 | 74 920 B | `nn_model_configure.c` 两块权重数组 |
| 推理延迟 / 功耗 / 续航 / 成本 | **无数据** | 计时函数是空桩、半主机被禁用，从未实测 |

> **关于 95.5% 请务必读这段。** 全部训练数据来自**单一被试、单日单场次**采集
> （57 段 / 29 798 帧 / 约 991 秒）。Edge Impulse 按滑窗随机切分训练与验证集，
> 相邻窗口高度重叠；数据中另有 15.2% 的零阶保持重复帧。因此这个数字应视为
> **乐观上界，不代表跨被试或跨设备的泛化性能**。仓库内没有独立留出测试集。
>
> 屏幕右下角显示的 `FPS / CPU` 是 LVGL 自带的性能监视器叠层，反映图形刷新率，
> **与推理速率无关**。

---

## 仓库结构

```
firmware/
├── ch583-insole/          鞋垫采集端（左/右），MounRiver Studio + RISC-V GCC
│   ├── left/ right/       作者业务代码（两份实质差异仅 5 处，见子目录 README）
│   ├── mrs-project/       工程描述、下载器与调试配置
│   └── third-party/       Bosch BMI270 官方驱动（BSD-3-Clause，未修改）
├── gd32vw553-receiver/    BLE 双 Central 网关，需覆盖进兆易 MSDK
├── gd32vw553-sender/      BLE Peripheral 透传网关，同上
├── gd32f470-hub/          汇聚枢纽，Keil MDK + FreeRTOS + LVGL 8.4
└── gd32h737-inference/    端侧推理，Keil MDK + 兆易 GD_LIB
host-app/                  PyQt5 上位机（BLE 接收、足底热力图、数据录制）
ml/                        双流 1D-CNN 结构与 Edge Impulse 配置说明
dataset/                   步态数据集（CC BY 4.0，见目录内 README）
docs/
├── KNOWN-ISSUES.md        76 条已知问题，作者自己整理
├── BUILD-PATCHES.md       需手动打进厂商 SDK 的改动
└── leak_scan.py           推送前的个人信息泄漏自检脚本
licenses/                  各第三方组件许可全文
```

## 上手

各子系统的编译烧录步骤在自己目录的 `README.md` 里。开始之前有三件事必须知道：

1. **厂商 SDK 不在本仓库。** 兆易 GD32VW55x MSDK、GD32F4xx/H7xx 固件库、沁恒 CH583 EVT
   都需要你自行从官网获取——它们的再分发许可不允许我们随仓库分发。各子系统 README
   写明了需要哪个版本、从哪里下、作者代码放到哪个位置。
2. **兆易的 `GD_LIB_CM7_v212.lib` 也不在本仓库。** 那是 664 KB 的闭源二进制库，
   全文没有任何许可授权声明，我们不能转发。端侧推理工程需要你自行获取它才能链接。
3. **有几处必须先改才能跑。** 最容易卡住的是鞋垫 MAC 白名单和上位机的扫描名——
   详见 [`docs/BUILD-PATCHES.md`](docs/BUILD-PATCHES.md)。

最快看到东西的路径是**只跑上位机**（不需要任何 MCU 工具链）：

```bash
cd host-app
python -m venv .venv && source .venv/bin/activate     # Windows: .venv\Scripts\activate
pip install -r requirements.txt
python smart_insole_display10.py
```

## 已知问题

[`docs/KNOWN-ISSUES.md`](docs/KNOWN-ISSUES.md) 里有 **76 条**，是我们自己逐文件核查
代码后整理的，不是别人挑的刺。其中几条会直接影响开箱体验：

- 上位机扫描的设备名 `VW553_Gateway` **在固件里根本不存在**，发送端实际广播 `GD-BLE-<MAC>`，所以开箱一定连不上
- 固件的英文标签是 `BLIND PROBE`（空格），上位机写的是 `BLIND_PROBE`（下划线），导致部分高危类别的告警着色不触发
- 上位机那条 `127.0.0.1:8080` 推理链的特征展平顺序与模型期望不符（按帧交错 vs 按通道分块）
- 训练脚本是 21 帧 / 924 维，板上固件是 45 帧 / 1980 维，45 帧那一版的训练脚本不存在
- 数据集有 2 路完全死掉的压力通道（`L_ADC_2`、`L_ADC_14`），左右足基线相差 2.13 倍

把它们摆出来是因为：一个还没解决的问题被明确指出，比被含糊带过更有价值。
欢迎提 PR，也欢迎直接开新 issue。

## 许可

代码以 [Apache License 2.0](LICENSE) 发布；`dataset/` 下的数据以
[CC BY 4.0](licenses/CC-BY-4.0.txt) 发布。

本仓库含派生自第三方的代码，各自的版权声明保留在源文件头部并汇总于
[`THIRD-PARTY-NOTICES.md`](THIRD-PARTY-NOTICES.md)：
Bosch Sensortec BMI270 驱动（BSD-3-Clause）、沁恒 WCH CH58x BLE 例程（厂商限定用途许可）、
兆易 GigaDevice MSDK / 固件库（BSD-3-Clause）、FreeRTOS（MIT）、LVGL（MIT）。

> 上位机使用 PyQt5（GPLv3 / 商业双许可）。以**源码形式**分发不受影响；
> 若你用 PyInstaller 打包成可执行文件再分发，那个可执行文件会落入 GPLv3。

## 致谢

感谢竞赛期间给过建议的老师与同学。BMI270 驱动来自 Bosch Sensortec，
图形界面基于 LVGL，实时内核为 FreeRTOS，模型训练在 Edge Impulse 上完成。
