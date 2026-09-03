# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The gd32-gait-insole authors

import tensorflow as tf
from tensorflow.keras.models import Sequential, Model
from tensorflow.keras.layers import Dense, InputLayer, Dropout, Conv1D, Flatten, Reshape, MaxPooling1D, Concatenate, Input, Lambda
from tensorflow.keras.optimizers import Adam
import sys

# ==========================================
# ⚙️ 超参数配置 (Hyperparameters)
# ==========================================
EPOCHS = 50
BATCH_SIZE = 32
LEARNING_RATE = 0.001

sys.stderr.write("Building Dual-Stream 1D-CNN for ADC + IMU...\n")

# 🚨 修复报错：将原始数据集按批次打包 (Batching)
train_dataset = train_dataset.batch(BATCH_SIZE, drop_remainder=False)
validation_dataset = validation_dataset.batch(BATCH_SIZE, drop_remainder=False)

# 1. 接收展平的特征输入 (Batch_size, 924)
inputs = Input(shape=(input_length,), name='raw_input')

# ==========================================
# 🌟 核心修复：物理级解耦 (防 Edge Impulse 拼接刺客)
# Edge Impulse 是把两个 Raw Data 模块首尾拼接的！
# 模块1(ADC): 32轴 * 21帧 = 672
# 模块2(IMU): 12轴 * 21帧 = 252
# ==========================================
adc_flat = Lambda(lambda t: t[:, 0:672], name='adc_slice')(inputs)
imu_flat = Lambda(lambda t: t[:, 672:924], name='imu_slice')(inputs)

# 2. 独立时序重构 (还原为 2D 波形矩阵)
adc_stream = Reshape((21, 32), name='adc_reshape')(adc_flat)
imu_stream = Reshape((21, 12), name='imu_reshape')(imu_flat)

# ==========================================
# 🌊 分支 A: ADC 压力拓扑流 (专注 32 轴压力变化)
# ==========================================
x_adc = Conv1D(filters=32, kernel_size=3, activation='relu', padding='same')(adc_stream)
x_adc = MaxPooling1D(pool_size=2)(x_adc)
x_adc = Conv1D(filters=16, kernel_size=3, activation='relu', padding='same')(x_adc)
x_adc = MaxPooling1D(pool_size=2)(x_adc)
x_adc = Flatten(name='adc_features')(x_adc)

# ==========================================
# 🌪️ 分支 B: IMU 运动学流 (专注 12 轴姿态变化)
# ==========================================
x_imu = Conv1D(filters=32, kernel_size=3, activation='relu', padding='same')(imu_stream)
x_imu = MaxPooling1D(pool_size=2)(x_imu)
x_imu = Conv1D(filters=16, kernel_size=3, activation='relu', padding='same')(x_imu)
x_imu = MaxPooling1D(pool_size=2)(x_imu)
x_imu = Flatten(name='imu_features')(x_imu)

# ==========================================
# 🤝 多模态特征融合层 (Fusion)
# ==========================================
merged = Concatenate()([x_adc, x_imu])
y = Dense(64, activation='relu')(merged)
y = Dropout(0.25)(y)

# 最终输出层: Softmax 分类器
outputs = Dense(classes, activation='softmax', name='prediction')(y)

model = Model(inputs=inputs, outputs=outputs)

# 编译网络
opt = Adam(learning_rate=LEARNING_RATE)
model.compile(loss='categorical_crossentropy', optimizer=opt, metrics=['accuracy'])

# 开始训练
model.fit(
    train_dataset,
    epochs=EPOCHS,
    validation_data=validation_dataset,
    verbose=2,
    callbacks=callbacks
)