# 双流 1D-CNN

## 网络结构

44 个输入通道里，32 路是 2 – 526 counts 的准静态压力分布，12 轴是 ±32768 LSB 的高频
加速度与角速度。量纲差两个数量级，频带也完全不同，共享第一层卷积核会让网络先学着
忽略其中一路。所以模型是**双流**的，到 `Concatenate` 才汇合：

```
输入 (924,)                      ← Edge Impulse 把两个 Raw Data 块首尾拼接
  ├─ Lambda t[:, 0:672]   → Reshape(21, 32) → Conv1D(32,k3) → MaxPool(2)
  │                                         → Conv1D(16,k3) → MaxPool(2) → Flatten(80)
  └─ Lambda t[:, 672:924] → Reshape(21, 12) → Conv1D(32,k3) → MaxPool(2)
                                            → Conv1D(16,k3) → MaxPool(2) → Flatten(80)
                          Concatenate(160) → Dense(64, relu) → Dropout(0.25)
                                           → Dense(8, softmax)
```

参数量 **18 216**，两分支时间感受野各 10 帧 ≈ 333 ms。
超参：50 epochs、batch 32、Adam lr 1e-3、`categorical_crossentropy`。

## 那个 Lambda 切片为什么是必须的

**`21 × 44 = 924`，恰好等于输入总长。** 如果不切片、直接 `Reshape((21, 44))`，
形状检查会**完全通过**，但数据被彻底打乱：重构出的「第 0 帧」由 ADC 第 0 帧的 32 个值
加上 ADC 第 1 帧的前 12 个值拼成，IMU 数据一个都没进来。这个尺寸巧合让错误无法被
异常暴露，只会表现为准确率莫名偏低。源码注释里我们管这叫「防 Edge Impulse 拼接刺客」。

## 怎么用

`edge-impulse-keras-expert.py` **不是独立可跑的脚本**，是粘贴进 Edge Impulse
「Neural Network settings → Switch to Keras (expert) mode」的代码片段，依赖 EI 注入的
`train_dataset` / `validation_dataset` / `input_length` / `classes` / `callbacks`。

EI 侧需要配**两个 Raw Data 处理块**，顺序必须是 ADC 在前：

| 块 | 轴 | Scale axes |
|---|---|---|
| 1 | 32 路 ADC | `0.002` |
| 2 | 12 轴 IMU | `0.000066` |

## 已知问题

- **窗口长度与部署固件不一致。** 本脚本是 21 帧 / 924 维，板上固件是 45 帧 / 1980 维
  （`WINDOW_FRAMES 45`），**45 帧那一版的训练脚本不存在**。脚本里 `672` / `924` / `21`
  全是硬编码，换窗口长度会静默错切而不报错。
- **IMU 缩放系数两处不一致**：EI 配置说明写 `0.000066`，H737 固件写 `0.00024414f`，差 3.7 倍。
- **左右脚通道顺序颠倒**：训练数据 CSV 是「左脚在前」，固件装箱是「右脚在前」。
- 上游模型文件（`.tflite` / `.h5` / `.eim`）不在仓库，从 EI 导出到兆易 GD_LIB 权重的
  转换流程无法复现。

详见 [`../docs/KNOWN-ISSUES.md`](../docs/KNOWN-ISSUES.md)。
