# PyQt5 上位机与机器学习

这是整条链路的「地面端」：GD32VW553 发送节点以 BLE Peripheral 身份广播 94 字节 CombinedDataPacket，PC 端用 bleak 订阅 datatrans TX 特征（0x0103）拿到原始帧，解析成 32 路足底压阻 ADC + 12 轴双足 IMU，然后做三件事——(1) 实时可视化：两只脚掌轮廓内用 16 点反距离加权插值出压力热力图，配 32 柱状图和 12 个 IMU 数值卡；(2) 数据采集：以 QTimer 33 ms 的固定节拍对内存快照做零阶保持重采样，导出 45 列 CSV，这就是模型训练集的唯一来源；(3) 可选的 PC 侧推理：把 45 帧滑窗 POST 给本机 127.0.0.1:8080 的 Edge Impulse WASM 服务（该服务未随仓库开源）。模型侧只有一份 77 行 Keras 代码，不是独立脚本，而是粘贴进 Edge Impulse「Switch to Keras (expert) mode」的片段：把 EI 拼接好的 924 维输入切成 ADC(21×32) 与 IMU(21×12) 两路，各走两层 Conv1D+MaxPooling1D，Flatten 后 Concatenate → Dense64 → Dropout0.25 → softmax 8 类，最终产物就是烧进 GD32H737 的双流 1D-CNN。这两个目录是纯作者原创（仅依赖 PyQt5/PyQtChart/bleak/qasync/numpy/aiohttp），许可上最干净，适合作为开源仓库的第一入口。

## 硬件配置

## BLE 链路参数（上位机侧）

| 项 | 值 | 出处 |
|---|---|---|
| 扫描设备名（精确匹配） | `"VW553_Gateway"` | smart_insole_display10.py:244 |
| 固件实际广播名 | `GD-BLE-<6字节MAC>`，与上一行**不一致** | 3.GD32VW553程序/发送/ble/app/app_adapter_mgr.c:79 + :115 |
| 设备名是否持久化 | 否，`FEAT_SUPPORT_SAVE_DEV_NAME = 0` | 3.GD32VW553程序/发送/app/app_cfg.h:150 |
| Notify 特征 UUID（128 位） | `00000103-0000-1000-8000-00805f9b34fb` | smart_insole_display10.py:245 |
| 对应固件短 UUID | `BLE_GATT_SVC_DATATRANS_TX_CHAR = 0x0103` | 3.GD32VW553程序/{接收,发送}/ble/profile/datatrans/ble_datatrans_common.h:43 |
| 扫描超时 | 5.0 s | smart_insole_display10.py:463 |
| 未找到设备重试间隔 | 5000 ms | smart_insole_display10.py:468 |
| 连接异常重试间隔 | 3000 ms | smart_insole_display10.py:485 |
| 掉线自动重连间隔 | 2000 ms | smart_insole_display10.py:492 |

## 94 字节数据包（`CombinedDataPacket`，`#pragma pack(1)`）

| 字段 | 类型 | 字节 | 偏移 |
|---|---|---|---|
| header | uint8 = `0xAA` | 1 | 0 |
| timestamp | uint32（小端，**上位机解析后丢弃**） | 4 | 1 |
| left_adc[16] | uint16 × 16 | 32 | 5 |
| left_imu[6] | int16 × 6（AX AY AZ GX GY GZ） | 12 | 37 |
| right_adc[16] | uint16 × 16 | 32 | 49 |
| right_imu[6] | int16 × 6 | 12 | 81 |
| tail | uint8 = `0x55` | 1 | 93 |

- 常量定义：`PACKET_SIZE=94` / `PACKET_HEADER=0xAA` / `PACKET_TAIL=0x55` / `PACKET_FMT='<B I 16H 6h 16H 6h B'`，smart_insole_display10.py:35-39；结构体注释 :23-33。
- 拆帧状态机：字节流缓冲 → 线性搜 `0xAA` → 校验第 94 字节是否 `0x55` → `struct.unpack`，smart_insole_display10.py:495-548。
- **左右交叉赋值**：`self.left_adc = parsed["r_adc"]`、`self.right_adc = parsed["l_adc"]`，IMU 同样交叉，smart_insole_display10.py:532-535。这是硬件接线导致的有意为之，但直接后果是**导出 CSV 里 `L_*` 列装的其实是报文 `right_*` 半区的数据**。
- 包尾字节（偏移 93）在整条链路里被 H737 覆写成推理出的类别索引后沿原路回传（见 H737/F470 子系统），上位机只把它当固定 `0x55` 校验（:525），不读取分类结果。

## 定时器与采样

| 定时器 | 周期 | 实际频率 | 用途 | 出处 |
|---|---|---|---|---|
| `ui_timer` | 16 ms | ≈62.5 Hz | 重绘热力图、柱状图、IMU 卡片 | :427-429 |
| `fps_timer` | 1000 ms | 1 Hz | 结算并显示真实收帧数，随后清零 | :432-434, :551-554 |
| `sampling_timer` | 33 ms | ≈30.3 Hz | 零阶保持重采样 → 录制 + AI 滑窗 | :438-440, :589 |
| CSV 合成时间戳步长 | `int(n × 33.333)` ms | 标称 30.0 Hz | 实际相邻差交替为 33/34 ms | :601 |

## AI 滑窗（PC 侧推理链）

| 项 | 值 | 出处 |
|---|---|---|
| `window_size` | 45 帧（≈1485 ms @33 ms） | :335 |
| `inference_step` | 15 帧（≈495 ms 触发一次） | :336 |
| 单帧特征维度 | 44 = 16 L_ADC + 16 R_ADC + 6 L_IMU + 6 R_IMU | :596 |
| 送出的向量长度 | 45 × 44 = **1980** | :618 |
| 模型期望长度 | **924**（21 帧 × 44） | Neural_network_architecture.py:21/29/30 |
| 平滑 | 最近 3 次结果多数投票 | :339, :676-679 |
| 推理服务端点 | `POST http://127.0.0.1:8080/predict` | :668 |

## 显示归一化

| 项 | 值 | 出处 |
|---|---|---|
| 热力图插值网格 | 60 × 100（宽×高） | :70-71 |
| 插值算法 | 反距离加权 `1/(d²+1e-6)`，权重在 resizeEvent 预计算并缓存 | :138-140 |
| 热力图归一化上限 | ADC = 300（`clip(v/300.0, 0, 1)`） | :146 |
| 传感器点着色归一化上限 | ADC = 300 | :222 |
| 柱状图 Y 轴 | 0 – 300 | :391 |
| 色标刻度标注 | 上「300」/ 中「750」/ 下「0」← 中间值标错 | :180-182 |
| 热力图透明度 | Alpha = 180/255 | :148 |
| 窗口初始尺寸 | 1400 × 760 | :235 |
| 足底轮廓 | 47 个手录坐标点，参考框 x∈[180,400] y∈[40,440] | :78-87, :97-98 |
| 16 个传感器坐标 | 手录，同一参考框 | :89-94 |

## Edge Impulse 侧配置

| 项 | 值 | 出处 |
|---|---|---|
| Raw Data 块 #1（ADC）Scale axes | **0.002**（≈1/500） | 使用说明.txt 第 2 行 |
| Raw Data 块 #2（IMU）Scale axes | **0.000066**（≈1/15151） | 使用说明.txt 第 2 行 |
| 块拼接顺序 | ADC 在前（0:672），IMU 在后（672:924） | Neural_network_architecture.py:24-30 |
| 反推的窗口长度 | 700 ms @ 30 Hz = 21 帧 | 672 = 21×32，252 = 21×12 |
| EPOCHS / BATCH_SIZE / LR | 50 / 32 / 0.001（代码内硬编码，覆盖界面设置） | Neural_network_architecture.py:10-12 |
| 网络结构 | 双分支各 Conv1D(32,k3,same)→MaxPool(2)→Conv1D(16,k3,same)→MaxPool(2)→Flatten；Concatenate→Dense(64,relu)→Dropout(0.25)→Dense(classes,softmax) | Neural_network_architecture.py:39-62 |
| 损失 / 优化器 | categorical_crossentropy / Adam | Neural_network_architecture.py:67-68 |

## 传感器量纲（从数据实测反推，仓库里无显式配置代码）

| 项 | 实测/推断值 | 依据 |
|---|---|---|
| ADC 取值范围 | 全集 min 2 / max 526（L_ADC_5），无物理单位、未做力标定 | 57 个 CSV 全量统计 |
| 加速度计满量程 | **±8 g（4096 LSB/g）** | stand.csv / sit1.csv 静止段 \|acc\| 中位数 4103–4118 LSB ≈ 4096，即 BMI270 上电默认量程 |
| 陀螺仪满量程 | 推断 ±2000 dps（16.4 LSB/dps），未经证实 | 全集 \|gyro\| 峰值 15 575 LSB ≈ 950 dps；`grep -rn "BMI2_ACC_RANGE\|BMI2_GYR_RANGE"` 在 CH583 的 peripheral.c / bmi270 封装层里**没有任何赋值**，说明沿用 BMI270 默认配置 |
| IMU 坐标系 | 未记录，两只鞋垫的安装朝向也未记录 | 仓库无相关文档 |

## 编译与烧录

## 一、上位机（host-app/）

### requirements.txt 内容

从 `smart_insole_display10.py:1-20` 的 import 逐条反推。标准库（sys / asyncio / time / struct / os / collections / datetime）不列入。

```
# 柔感智行 上位机依赖
# 作者实测运行环境：CPython 3.12（依据 __pycache__/smart_insole_display10.cpython-312.pyc）
# 建议 Python 3.10 – 3.12

# GUI：QtWidgets / QtCore / QtGui
PyQt5>=5.15.9,<5.16

# QtChart 模块是独立发行包，不随 PyQt5 安装！
# 代码里 from PyQt5.QtChart import QChart, QChartView, QValueAxis, QBarSeries, QBarSet, QBarCategoryAxis
# 缺它会在启动瞬间 ImportError: No module named 'PyQt5.QtChart'
# 版本号必须与 PyQt5 同一个 5.15.x 大版本，否则 ABI 不匹配
PyQtChart>=5.15.6,<5.16

# BLE 客户端：BleakScanner.find_device_by_name / BleakClient / start_notify
bleak>=0.21.0,<1.0

# 把 asyncio 事件循环嫁接到 Qt 事件循环：QEventLoop / asyncSlot
qasync>=0.27.0,<0.28

# 热力图的反距离加权插值与 RGBA 缓冲
numpy>=1.24

# 向本机 127.0.0.1:8080 推理服务发 POST（仅「开启 AI 预测」按钮需要）
aiohttp>=3.9.0,<4.0
```

版本约束的理由：PyQt5 与 PyQtChart 必须同大版本，这是本项目最容易踩的坑（QtChart 单独发包）；bleak 的 `find_device_by_name` 从 0.15 起可用，但 0.21 以后 macOS/Windows 后端才稳定；qasync 0.27 起正式支持 Python 3.12；numpy 只用到 linspace/meshgrid/clip/sum 等基础 API，1.x 与 2.x 均可，保守起见可加 `,<2.0`；aiohttp 3.9 起有 cp312 wheel。

### 安装与运行

```bash
python -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\activate
pip install -r requirements.txt
python host-app/smart_insole_display10.py
```

运行前必做两处修改（否则一定连不上 / 会乱建目录），见 patches 字段的前两条：
1. `smart_insole_display10.py:244` 的 `device_name`
2. `smart_insole_display10.py:740` 的 `target_dir`

平台前置条件：
- Windows 10/11 — 系统蓝牙开启即可，bleak 走 WinRT。
- macOS — 首次运行会弹蓝牙权限请求，必须允许；从终端启动时授权对象是终端 App 本身。
- Linux — 需要 BlueZ ≥ 5.43 且 `bluetoothd` 在跑；某些发行版需要 `sudo setcap 'cap_net_raw,cap_net_admin+eip' $(readlink -f $(which python))` 或以 root 运行。

### 连接流程（代码内建，不需要点任何按钮）

`__init__` 末尾直接 `asyncio.ensure_future(self.start_ble_task())`（:442），启动即自动扫描：

1. `BleakScanner.find_device_by_name(self.device_name, timeout=5.0)`（:463）按**完整名字精确匹配**扫 5 秒。
2. 扫不到 → 状态栏「🔴 未找到设备重试中」，5 s 后重试（:466-468），无限循环。
3. 扫到 → `BleakClient(device, disconnected_callback=self.on_disconnect).connect()`（:472-473）。
4. 连上后 `start_notify("00000103-0000-1000-8000-00805f9b34fb", self.handle_ble_notification)`（:478）。这个 128 位 UUID 就是蓝牙 Base UUID 套上 16 位短 UUID 0x0103，对应固件里的 `BLE_GATT_SVC_DATATRANS_TX_CHAR`（3.GD32VW553程序/{接收,发送}/ble/profile/datatrans/ble_datatrans_common.h:43）。
5. 异常 → 3 s 后重连（:485）；连上后掉线 → 2 s 后重连（:492）。

所以正常使用只需：给发送端 VW553 上电 → 打开上位机 → 等标题栏变成「🟢 已连接」。

### 界面分区

顶栏（`header_panel`，:274-332，限高 65 px）从左到右：
- 「👟 双足分析台」标题
- 🤖 预测标签 —— 只有开了 AI 预测才有内容。高危步态（HEMIPLEGIC / ANTALGIC / BLIND_PROBE）红底白字，上下楼橙色预警，日常动作绿/蓝（:683-703）
- 🔴/🟡/🟢 连接状态
- ⚠️ 丢包计数 —— 口径混乱，见 known_issues
- ⚡ 帧率 —— 每 1 s 结算一次真实收到的帧数（:432-434, :551-554）
- 💾 记录条数
- 三个按钮：开启 AI 预测 / 开始录制 / 导出 CSV

内容区左侧（占 4 份）：两个 `InsoleWidget` 脚掌热力图，左脚 `flip_x=True` 做镜像（:354-355）。每只脚在手工描的 47 点足底轮廓内做 60×100 网格的反距离加权插值（权重矩阵在 `resizeEvent` 里一次性预计算并缓存，:127-140），16 个传感器位置画成圆角胶囊并标 0–15 编号（:203-212），右侧带一条 50 段色标。

内容区右侧（占 6 份）：
- 上方（占 6 份）QtChart 分组柱状图，C0–C15 共 16 组，每组左脚（绿 #2ecc71）/右脚（蓝 #3498db）两根柱，Y 轴固定 0–300（:390-392）
- 下方（占 3 份）IMU 卡片组，2 行 × 6 列 = 12 个数值格，第一行左脚 AX/AY/AZ/GX/GY/GZ，第二行右脚。正值绿/蓝、负值红、零灰（:576-586）

三个定时器：UI 刷新 16 ms ≈ 60 fps（:427-429）、帧率结算 1000 ms（:432-434）、数据重采样 33 ms ≈ 30.3 Hz（:438-440）。

### 录数据 / 导 CSV

1. 先确认已连接（未连接时点录制只会提示「🔴 请先连接设备」，:622-624）。
2. 点「▶ 开始录制」：清空缓冲、`error_count`/`frame_count`/`record_frame_count` 归零（:630-636），按钮变红「⏹ 停止录制」。
3. 录制期间 33 ms 的 `process_uniform_frame` 每次触发都抓一份内存快照写入 `recorded_data`（:589-605）。注意这是**零阶保持重采样**：如果这 33 ms 内蓝牙没来新帧，会把上一帧原封不动再写一遍——全数据集实测 15.19% 的相邻帧完全重复就是这么来的。
4. 点「⏹ 停止录制」。
5. 点「⬇ 导出CSV」弹保存框（录制中会直接 return，必须先停止，:736）。表头与数据在 `save_data_to_file` 里手写拼接（:721-730），45 列。

单段录多长自己控制：29 798 帧 / 993 s 的原始数据集里，长录段是 4000+ 帧（约 133 s），上下楼段是 127–188 帧（约 4–6 s，一层楼梯一段）。

### 「开启 AI 预测」需要一个仓库里没有的服务

`send_to_nodejs`（:663-713）向 `http://127.0.0.1:8080/predict` POST `{"features": [...]}`，期望回 `{"classification": {"类名": 分数, ...}}`。这个 HTTP 服务**不在仓库里**（全仓库 `find -name "*.js" -o -name "package.json" -o -name "*.wasm"` 零命中），是作者本机自己写的一层 Express 壳子。别人要复现得自己搭：

1. Edge Impulse 项目里 Deployment → 选 **WebAssembly (Node.js)** 导出，解压。
2. 用它的 `EdgeImpulseClassifier` 写一个 Express 路由：收到 `features` 数组 → `classifier.classify(features)` → 把 EI 原生返回的 `{results:[{label,value},...]}` 转成上位机期望的 `{classification:{label:value}}` 形状 → 监听 8080。
3. **同时必须修掉 known_issues 里第 1、2 条**（展平顺序与滑窗长度），否则即使服务通了，喂进去的也是错位数据。

不搭这个服务完全不影响可视化和数据采集，只是「开启 AI 预测」会一直显示「🤖 模型推断错误」。真正的端侧推理在 GD32H737 上，与这条 PC 链路无关。

## 二、模型训练（ml/）

### 工具链

不需要本地装任何东西 —— 没有 IDE、没有 CUDA、没有 conda 环境。整个训练在 [Edge Impulse Studio](https://studio.edgeimpulse.com/) 网页里完成，免费账号足够。`ml/edge-impulse-keras-expert.py` **不是可执行脚本**，直接 `python` 跑它必然 NameError：`train_dataset` / `validation_dataset` / `input_length` / `classes` / `callbacks` 这五个名字全都由 EI 在运行时注入，文件里没有任何定义。

### 复现步骤

**1. 建项目、传数据**

新建 project，Data acquisition → Upload data，把 `dataset/raw/` 下的 CSV 传上去。EI 会把 45 列里的 `timestamp` 认成时间轴，剩下 44 列认成 44 个轴。标签在上传时按文件名指定，8 个类必须命名为固件侧一致的英文名（否则 F470 屏和 H737 的类别索引会错位）：

```
ANTALGIC, BLIND_PROBE, DOWNSTAIRS, HEMIPLEGIC, SITTING, STANDING, UPSTAIRS, WALKING
```

这 8 个名字按字母序排列后的索引 0–7，正好对应 `2.GD32F470ZGT6程序/final_wireless_v16.0/Hardware/lcd_my_test/lcd_mytest.c:32-40` 里那两张 8 元素字典表（`action_names_cn` / `action_names_en`）。**类名的字母序就是类别索引，改名字等于改索引。**

CSV 前缀到这 8 个类的映射见 `dataset/README.md`——其中 band / eye / unnormal_singol 三条是**推断**，不是仓库里记录的事实。

**2. Impulse design：两个 Raw Data 块**

这一步是整个复现里最容易错的地方，也是那份 3 行 `使用说明.txt` 唯一记录的内容。

| 处理块 | 选中的轴 | 轴数 | Scale axes |
|---|---|---|---|
| Raw Data #1（ADC） | L_ADC_1..16 + R_ADC_1..16 | 32 | **0.002** |
| Raw Data #2（IMU） | L_IMU_AX/AY/AZ/GX/GY/GZ + R_IMU_同 | 12 | **0.000066** |

窗口参数按 Keras 片段里硬编码的 672/924 反推：**Window size = 700 ms，Frequency = 30 Hz**（→ 21 帧 → 21×32=672，21×12=252，input_length=924）。Window increase 决定样本数量，不影响这份代码。

顺序很重要：ADC 块必须在 IMU 块**前面**，因为 EI 把多个处理块的输出首尾拼接，代码里 `t[:, 0:672]` 取的就是前一块。两块顺序调了模型就全废。

**3. 粘代码**

Neural Network settings 页面右上角三点菜单 → **Switch to Keras (expert) mode** → 清空默认模板 → 整段粘贴 `edge-impulse-keras-expert.py` → Start training。

超参在代码里：EPOCHS=50、BATCH_SIZE=32、LEARNING_RATE=0.001（:10-12），界面上填的值会被这三行覆盖。如果一粘就报维度错误，先删掉 :17-18 那两行 `.batch()`（见 patches）。

**4. 导出到 H737**

Deployment → 选 C++ library 或 Arm CMSIS-NN 优化的 C++ library，下载后按 GD32H737 子系统的文档接进 Keil 工程。

### 精度口径

答辩 PPT 自述 **95.5%**，来源是 Edge Impulse 的验证集指标（train/validation split 由 EI 自动划分，比例未记录）。这个数字建立在**单被试单日采集**的数据上，且 3 个病理步态类是健康被试模拟的，不构成跨人群泛化证据。仓库里没有独立测试集、没有混淆矩阵原始文件、没有推理延迟/功耗/续航的任何实测记录——README 里不要出现这些数字。

## 本目录未收录哪些文件，为什么

这个子系统几乎没有可排除的东西——它是整个仓库里最干净的两个目录，没有厂商 SDK、没有 IDE 工程、没有编译产物。

唯一需要排除的：
- `4.工程代码/5.上位机代码/__pycache__/smart_insole_display10.cpython-312.pyc` —— 1 个文件 / 48 715 B（48 KB）。Python 字节码缓存，mtime 2026-09-02 15:49（远晚于源码的 2026-06-16 19:14），说明是近期运行时重新生成的。同时它也是唯一能证明作者运行环境为 CPython 3.12 的线索，这一点写进 README 即可，文件本身不收录。

统计：两个目录合计 60 个文件 / 4.4 MB，排除 1 个文件 / 48 KB，收录 59 个文件 / 约 4.35 MB（其中 4.3 MB 是 57 个 CSV，代码只有 40 KB）。

另外提醒（不属于本子系统但会影响仓库根目录 .gitignore）：建议在根 .gitignore 里写入 `__pycache__/`、`*.py[cod]`，避免作者以后再跑一次上位机就把 .pyc 提交进去。

**注意：数据集的 4.3 MB 是否收录，取决于下面「数据集要不要开源」的结论。如果作者决定不开源数据集，那么本子系统的收录体积从 4.35 MB 降到 40 KB（只剩 3 个代码/说明文件），dataset/ 目录整体排除。**

---

已知问题见 [`docs/KNOWN-ISSUES.md`](../docs/KNOWN-ISSUES.md)，
需打进 SDK 的改动见 [`docs/BUILD-PATCHES.md`](../docs/BUILD-PATCHES.md)。
