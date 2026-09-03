# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 The gd32-gait-insole authors

import sys
import asyncio
import time
import struct
import os
import numpy as np
import collections
import aiohttp
from datetime import datetime
from qasync import QEventLoop, asyncSlot
from bleak import BleakScanner, BleakClient
from PyQt5.QtWidgets import (QApplication, QWidget, QLabel, QMainWindow,
                             QVBoxLayout, QHBoxLayout, QGridLayout, QSizePolicy, QPushButton,
                             QFileDialog, QLineEdit, QGroupBox)
from PyQt5.QtChart import (QChart, QChartView, QValueAxis,
                           QBarSeries, QBarSet, QBarCategoryAxis)
from PyQt5.QtCore import Qt, QMargins, QPointF, QTimer, pyqtSignal
from PyQt5.QtGui import QPainter, QPainterPath, QBrush, QColor, QPen
from PyQt5.QtGui import QImage, QBitmap, QColor, QPainter, QPainterPath, QBrush, QPen
from PyQt5.QtCore import Qt, QPointF, QRect, QRectF

# ================== 1. 二进制协议解析 ==================
# 与 F470 的 CombinedDataPacket 完全对应
# #pragma pack(1)
# typedef struct {
#     uint8_t  header;          // 0xAA (1 byte)
#     uint32_t timestamp;       // (4 bytes)
#     uint16_t left_adc[16];    // (32 bytes)
#     int16_t  left_imu[6];     // (12 bytes)
#     uint16_t right_adc[16];   // (32 bytes)
#     int16_t  right_imu[6];    // (12 bytes)
#     uint8_t  tail;            // 0x55 (1 byte)
# } CombinedDataPacket;         // Total: 94 bytes

PACKET_SIZE = 94
PACKET_HEADER = 0xAA
PACKET_TAIL = 0x55
# 小端: B=uint8, I=uint32, 16H=16个uint16, 6h=6个int16, 16H, 6h, B=uint8
PACKET_FMT = '<B I 16H 6h 16H 6h B'

def parse_combined_packet(data):
    """解析 94 字节二进制帧"""
    if len(data) != PACKET_SIZE:
        return None
    if data[0] != PACKET_HEADER or data[-1] != PACKET_TAIL:
        return None
    try:
        unpacked = struct.unpack(PACKET_FMT, data)
        return {
            "type": "FRAME",
            "timestamp": unpacked[1],
            "l_adc": list(unpacked[2:18]),
            "l_imu": list(unpacked[18:24]),
            "r_adc": list(unpacked[24:40]),
            "r_imu": list(unpacked[40:46]),
        }
    except struct.error:
        return None


# ================== 2. 脚掌绘图类 ==================
class InsoleWidget(QWidget):
    def __init__(self, title="鞋垫", flip_x=False, parent=None):
        super().__init__(parent)
        self.title = title
        self.flip_x = flip_x

        self.sensor_data = [0] * 16
        self.sensor_positions = []
        self.grid_w = 60
        self.grid_h = 100

        self.cached_path = QPainterPath()
        self.cached_W = None

        

        self.raw_sole_points = [
            (263, 50), (280, 51), (293, 53), (304, 56), (313, 60), (330, 75),
            (345, 92), (360, 111), (373, 132), (377, 152), (379, 172), (378, 192),
            (368, 220), (361, 248), (351, 275), (348, 315), (345, 355), (343, 380),
            (339, 400), (335, 406), (329, 411), (321, 415), (310, 419), (298, 422),
            (285, 423), (272, 422), (259, 419), (246, 414), (239, 409), (232, 402),
            (227, 393), (224, 353), (222, 313), (220, 273), (212, 238), (205, 205),
            (199, 185), (194, 170), (193, 157), (192, 143), (193, 130), (197, 110),
            (204, 90), (213, 71), (226, 62), (241, 55), (263, 50)
        ]

        self.raw_sensor_points = [
            (242.5, 98.5),   (303.5, 98.5),   (232.5, 144.5),  (282.5, 144.5),
            (338.5, 144.5),  (232.5, 193.5),  (289.5, 195.5),  (345.5, 195.5),
            (241.5, 248.5),  (288.5, 249.5),  (337.5, 249.5),  (251.5, 303.5),
            (289.5, 305.5),  (326.5, 306.5),  (264.5, 369.5),  (318.5, 370.5)
        ]

    def map_pt(self, x, y, w, h):
        ref_x_min, ref_x_max = 180, 400
        ref_y_min, ref_y_max = 40, 440
        ref_w, ref_h = ref_x_max - ref_x_min, ref_y_max - ref_y_min
        scale = min((w - 60) / ref_w, (h - 40) / ref_h)
        draw_w, draw_h = ref_w * scale, ref_h * scale
        offset_x = (w - draw_w) * 0.5
        offset_y = (h - draw_h) * 0.5
        px = offset_x + (x - ref_x_min) * scale
        if self.flip_x: px = w - px
        py = offset_y + (y - ref_y_min) * scale
        return px, py, scale

    def calculate_sensor_positions(self, w, h):
        positions = []
        for pt in self.raw_sensor_points:
            px, py, scale = self.map_pt(pt[0], pt[1], w, h)
            positions.append((px, py, scale))
        return positions

    def build_foot_path(self, w, h):
        path = QPainterPath()
        if not self.raw_sole_points: return path
        start_x, start_y, _ = self.map_pt(self.raw_sole_points[0][0], self.raw_sole_points[0][1], w, h)
        path.moveTo(start_x, start_y)
        for pt in self.raw_sole_points[1:]:
            px, py, _ = self.map_pt(pt[0], pt[1], w, h)
            path.lineTo(px, py)
        path.closeSubpath()
        return path

    def resizeEvent(self, event):
        super().resizeEvent(event)
        w, h = self.width(), self.height()
        if w <= 0 or h <= 0: return
        self.cached_path = self.build_foot_path(w, h)
        self.sensor_positions = self.calculate_sensor_positions(w, h)
        xs = np.linspace(0, w, self.grid_w)
        ys = np.linspace(0, h, self.grid_h)
        grid_x, grid_y = np.meshgrid(xs, ys)
        sensor_x = np.array([p[0] for p in self.sensor_positions])
        sensor_y = np.array([p[1] for p in self.sensor_positions])
        dist_sq = (grid_x[..., np.newaxis] - sensor_x)**2 + (grid_y[..., np.newaxis] - sensor_y)**2
        weights = 1.0 / (dist_sq + 1e-6)
        self.cached_W = weights / np.sum(weights, axis=2, keepdims=True)

    def generate_heatmap(self):
        if self.cached_W is None: return None
        sensor_v = np.array(self.sensor_data, dtype=np.float64)
        heatmap_vals = np.sum(self.cached_W * sensor_v, axis=2)
        norm_vals = np.clip(heatmap_vals / 300.0, 0, 1)
        img_data = np.zeros((self.grid_h, self.grid_w, 4), dtype=np.uint8)
        img_data[:, :, 3] = 180
        vals_x4 = norm_vals * 4
        r = np.clip(np.minimum(vals_x4 - 1.5, -vals_x4 + 4.5), 0, 1)
        g = np.clip(np.minimum(vals_x4 - 0.5, -vals_x4 + 3.5), 0, 1)
        b = np.clip(np.minimum(vals_x4 + 0.5, -vals_x4 + 2.5), 0, 1)
        img_data[:, :, 0] = (r * 255).astype(np.uint8)
        img_data[:, :, 1] = (g * 255).astype(np.uint8)
        img_data[:, :, 2] = (b * 255).astype(np.uint8)
        image = QImage(img_data.data, self.grid_w, self.grid_h, self.grid_w * 4, QImage.Format_RGBA8888)
        return image.copy()

    def draw_color_legend(self, painter, w, h):
        legend_x, legend_y = w * 0.85, h * 0.25
        legend_width, legend_height = w * 0.05, h * 0.50
        num_segments = 50
        segment_height = legend_height / num_segments
        for i in range(num_segments):
            norm_val = 1.0 - (i / num_segments)
            val_x4 = norm_val * 4
            r = np.clip(min(val_x4 - 1.5, -val_x4 + 4.5), 0, 1)
            g = np.clip(min(val_x4 - 0.5, -val_x4 + 3.5), 0, 1)
            b = np.clip(min(val_x4 + 0.5, -val_x4 + 2.5), 0, 1)
            painter.setBrush(QBrush(QColor(int(r * 255), int(g * 255), int(b * 255))))
            painter.setPen(Qt.NoPen)
            painter.drawRect(int(legend_x), int(legend_y + i * segment_height), int(legend_width), int(segment_height) + 1)
        painter.setBrush(Qt.NoBrush)
        painter.setPen(QPen(QColor(80, 80, 80), 1))
        painter.drawRect(int(legend_x), int(legend_y), int(legend_width), int(legend_height))
        painter.setPen(QPen(Qt.black, 1))
        font = painter.font()
        font.setPointSize(9)
        painter.setFont(font)
        painter.drawText(int(legend_x + legend_width + 5), int(legend_y + 8), "300")
        painter.drawText(int(legend_x + legend_width + 5), int(legend_y + legend_height / 2 + 5), "750")
        painter.drawText(int(legend_x + legend_width + 5), int(legend_y + legend_height), "0")

    def paintEvent(self, event):
        if self.cached_W is None: return
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setBrush(QBrush(QColor(245, 245, 220)))
        painter.setPen(QPen(QColor(100, 100, 100), 3, Qt.SolidLine, Qt.RoundCap, Qt.RoundJoin))
        painter.drawPath(self.cached_path)
        painter.save()
        painter.setClipPath(self.cached_path)
        try:
            heatmap_img = self.generate_heatmap()
            if heatmap_img:
                painter.drawImage(QRect(0, 0, self.width(), self.height()), heatmap_img)
        except Exception:
            pass
        painter.restore()
        painter.setBrush(Qt.NoBrush)
        painter.setPen(QPen(QColor(80, 80, 80), 3))
        painter.drawPath(self.cached_path)
        for i, (px, py, scale) in enumerate(self.sensor_positions):
            value = self.sensor_data[i] if i < len(self.sensor_data) else 0
            painter.setBrush(QBrush(self.get_color_for_value(value)))
            painter.setPen(QPen(Qt.white, 2))
            ew = 15 * scale
            eh = 27 * scale
            rect = QRectF(px - ew / 2, py - eh / 2, ew, eh)
            painter.drawRoundedRect(rect, ew / 2, ew / 2)
            painter.setPen(QPen(Qt.black, 1))
            painter.drawText(int(px - 6), int(py + 4), f"{i}")
        self.draw_color_legend(painter, self.width(), self.height())
        painter.setPen(QPen(Qt.black, 1))
        font = painter.font()
        font.setPointSize(12)
        font.setBold(True)
        painter.setFont(font)
        painter.drawText(20, 30, self.title)

    def get_color_for_value(self, value):
        ratio = min(1.0, max(0.0, value / 300.0))
        return QColor(int(255 * ratio), int(255 * (1 - ratio)), 0)

    def update_sensor_data(self, data):
        self.sensor_data = data
        self.update()


# ================== 3. 主窗口类 ==================
class gd32_show(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("智能双足步态分析 Dashboard (GD32VW553 BLE)")
        self.setGeometry(100, 100, 1400, 760)
        self.setStyleSheet("QMainWindow { background-color: #e9ecef; }")

        # --- 变量初始化 ---
        self.ble_client = None
        self.is_connected = False
        self.is_connecting = False

        # ★ 改为 VW553 的设备名和 Notify UUID
        self.device_name = "VW553_Gateway"
        self.CHAR_UUID_TX = "00000103-0000-1000-8000-00805f9b34fb"

        # ★ 二进制缓冲区（不再按 \n 分割）
        self.data_buffer = b""
        self.error_count = 0
        self.frame_count = 0

        self.left_adc = [0] * 16
        self.right_adc = [0] * 16
        self.left_imu = [0] * 6
        self.right_imu = [0] * 6

        self.recorded_data = []
        self.is_recording = False
        # --- 新增：录制时间与帧数追踪 ---
        self.record_frame_count = 0
        self.record_start_time = 0.0
        # -----------------------------
        self.right_sensor_mapping = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]
        self.left_sensor_mapping = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15]

        # --- 主布局 ---
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        main_layout.setContentsMargins(10, 10, 10, 10)
        main_layout.setSpacing(10)

        # === 1. 顶部操作栏 ===
        header_panel = QWidget()
        header_panel.setStyleSheet("background-color: white; border-radius: 8px;")
        header_panel.setMaximumHeight(65)
        header_layout = QHBoxLayout(header_panel)
        header_layout.setContentsMargins(15, 5, 15, 5)

        title = QLabel("👟 双足分析台")
        title.setStyleSheet("font-size: 22px; font-weight: bold; color: #2c3e50;")

        self.predict_label = QLabel("🤖 预测: 等待数据...")
        self.predict_label.setStyleSheet("font-size: 18px; color: #8e44ad; font-weight: bold; background: #fdf2e9; padding: 5px; border-radius: 5px;")

        self.status_label = QLabel("🔴 等待连接...")
        self.status_label.setStyleSheet("font-size: 14px; color: #7f8c8d; font-weight: bold;")

        self.error_label = QLabel("⚠️ 丢包: 0")
        self.error_label.setStyleSheet("font-size: 14px; color: #c0392b;")

        self.frame_label = QLabel("⚡ 帧率: 0 Hz")
        self.frame_label.setStyleSheet("font-size: 14px; color: #d35400; font-weight: bold;")
        
        # --- 新增：用于统计1秒内收到帧数的变量 ---
        self.fps_frame_count = 0

        self.data_count_label = QLabel("💾 记录: 0")
        self.data_count_label.setStyleSheet("font-size: 14px; color: #2980b9;")

        self.is_ai_enabled = False
        self.ai_button = QPushButton("▶ 开启 AI 预测")
        self.ai_button.clicked.connect(self.toggle_ai)
        self.ai_button.setStyleSheet("QPushButton { background-color: #8e44ad; color: white; border-radius: 4px; padding: 6px 15px; font-weight: bold; }")

        self.record_button = QPushButton("▶ 开始录制")
        self.record_button.clicked.connect(self.toggle_recording)
        self.record_button.setStyleSheet("QPushButton { background-color: #27ae60; color: white; border-radius: 4px; padding: 6px 15px; font-weight: bold; }")

        self.save_button = QPushButton("⬇ 导出CSV")
        self.save_button.clicked.connect(self.save_data_prompt)
        self.save_button.setStyleSheet("QPushButton { background-color: #2980b9; color: white; border-radius: 4px; padding: 6px 15px; font-weight: bold; }")

        header_layout.addWidget(title)
        header_layout.addSpacing(20)
        header_layout.addWidget(self.predict_label)
        header_layout.addStretch()
        header_layout.addWidget(self.status_label)
        header_layout.addSpacing(15)
        header_layout.addWidget(self.error_label)
        header_layout.addSpacing(15)
        header_layout.addWidget(self.frame_label)
        header_layout.addSpacing(15)
        header_layout.addWidget(self.data_count_label)
        header_layout.addSpacing(15)
        header_layout.addWidget(self.ai_button)
        header_layout.addSpacing(10)
        header_layout.addWidget(self.record_button)
        header_layout.addSpacing(10)
        header_layout.addWidget(self.save_button)

        main_layout.addWidget(header_panel)

        # ====== 核心修改 1：同步 WASM 模型参数 ======
        self.window_size = 45
        self.inference_step = 15
        self.feature_window = collections.deque(maxlen=self.window_size)
        self.inference_counter = 0
        self.history_labels = collections.deque(maxlen=3)
        self.is_inferencing = False

        

        # === 2. 内容区 ===
        content_panel = QWidget()
        content_layout = QHBoxLayout(content_panel)
        content_layout.setContentsMargins(0, 0, 0, 0)
        content_layout.setSpacing(10)

        # 2a. 热力图
        insole_container = QWidget()
        insole_container.setStyleSheet("background: white; border-radius: 8px;")
        insole_layout = QHBoxLayout(insole_container)
        self.left_insole = InsoleWidget(title="左脚 [L_]", flip_x=True)
        self.right_insole = InsoleWidget(title="右脚 [R_]", flip_x=False)
        insole_layout.addWidget(self.left_insole)
        insole_layout.addWidget(self.right_insole)
        content_layout.addWidget(insole_container, 4)

        # 2b. 图表区
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(0, 0, 0, 0)
        right_layout.setSpacing(10)

        self.chart_view = QChartView()
        self.chart_view.setStyleSheet("background: white; border-radius: 8px;")
        self.chart_view.setRenderHint(QPainter.Antialiasing)
        self.chart = QChart()
        self.chart.setAnimationOptions(QChart.NoAnimation)
        self.chart.layout().setContentsMargins(5, 5, 5, 5)

        self.left_bar_set = QBarSet("左脚 ADC")
        self.left_bar_set.append([0] * 16)
        self.left_bar_set.setColor(QColor(46, 204, 113))

        self.right_bar_set = QBarSet("右脚 ADC")
        self.right_bar_set.append([0] * 16)
        self.right_bar_set.setColor(QColor(52, 152, 219))

        self.series = QBarSeries()
        self.series.append(self.left_bar_set)
        self.series.append(self.right_bar_set)
        self.chart.addSeries(self.series)

        self.axis_x = QBarCategoryAxis()
        self.axis_x.append([f"C{i}" for i in range(16)])
        self.chart.setAxisX(self.axis_x, self.series)

        self.axis_y = QValueAxis()
        self.axis_y.setRange(0, 300)
        self.chart.setAxisY(self.axis_y, self.series)
        self.chart.legend().setAlignment(Qt.AlignTop)

        self.chart_view.setChart(self.chart)
        right_layout.addWidget(self.chart_view, 6)

        # IMU 卡片
        imu_group = QGroupBox("实时姿态数据 (双足 6轴 IMU)")
        imu_group.setStyleSheet("QGroupBox { background: white; border-radius: 8px; font-weight: bold; padding-top: 20px;} QGroupBox::title { subcontrol-origin: margin; left: 10px; top: 5px; }")
        imu_layout = QGridLayout(imu_group)
        imu_layout.setContentsMargins(10, 15, 10, 10)

        self.left_imu_labels = []
        self.right_imu_labels = []
        imu_names = ["AX", "AY", "AZ", "GX", "GY", "GZ"]

        for i, name in enumerate(imu_names):
            lbl_l = QLabel(f"左 {name}\n0")
            lbl_l.setStyleSheet("background-color: #e8f8f5; border: 1px solid #e9ecef; border-radius: 4px; font-size: 14px; color: #27ae60;")
            lbl_l.setAlignment(Qt.AlignCenter)
            imu_layout.addWidget(lbl_l, 0, i)
            self.left_imu_labels.append(lbl_l)

            lbl_r = QLabel(f"右 {name}\n0")
            lbl_r.setStyleSheet("background-color: #ebf5fb; border: 1px solid #e9ecef; border-radius: 4px; font-size: 14px; color: #2980b9;")
            lbl_r.setAlignment(Qt.AlignCenter)
            imu_layout.addWidget(lbl_r, 1, i)
            self.right_imu_labels.append(lbl_r)

        right_layout.addWidget(imu_group, 3)

        content_layout.addWidget(right_panel, 6)
        main_layout.addWidget(content_panel)

        # 定时器
        self.ui_timer = QTimer()
        self.ui_timer.timeout.connect(self.update_ui_timer_tick)
        self.ui_timer.start(16)

        # --- 新增：1秒结算一次帧率的定时器 ---
        self.fps_timer = QTimer()
        self.fps_timer.timeout.connect(self.update_fps_tick)
        self.fps_timer.start(1000)
        # --------------------------------

        # ====== 核心修改 1：大一统全局 30Hz 均匀重采样引擎 ======
        self.sampling_timer = QTimer()
        self.sampling_timer.timeout.connect(self.process_uniform_frame)
        self.sampling_timer.start(33) # 33ms ≈ 30.3Hz，完美兼容 30Hz 步态需求

        asyncio.ensure_future(self.start_ble_task())

    def remap_sensor_data(self, raw_data, mapping):
        remapped_data = [0] * 16
        for i in range(16):
            if i < len(mapping) and mapping[i] < len(raw_data):
                remapped_data[i] = raw_data[mapping[i]]
            else:
                remapped_data[i] = raw_data[i]
        return remapped_data

    # ================== 蓝牙逻辑 ==================
    async def start_ble_task(self):
        if self.is_connecting: return
        self.is_connecting = True
        self.status_label.setText("🟡 扫描设备中...")

        try:
            if self.ble_client and self.ble_client.is_connected:
                await self.ble_client.disconnect()

            device = await BleakScanner.find_device_by_name(self.device_name, timeout=5.0)

            if device is None:
                self.status_label.setText("🔴 未找到设备重试中")
                self.is_connecting = False
                QTimer.singleShot(5000, lambda: asyncio.ensure_future(self.start_ble_task()))
                return

            self.status_label.setText(f"🟡 连接中: {device.address}")
            self.ble_client = BleakClient(device, disconnected_callback=self.on_disconnect)
            await self.ble_client.connect()

            self.is_connected = True
            self.data_buffer = b""
            self.status_label.setText(f"🟢 已连接: {self.device_name}")
            await self.ble_client.start_notify(self.CHAR_UUID_TX, self.handle_ble_notification)

        except Exception as e:
            print(f"BLE error: {e}")
            self.status_label.setText("🔴 连接失败")
            self.is_connected = False
            self.ble_client = None
            QTimer.singleShot(3000, lambda: asyncio.ensure_future(self.start_ble_task()))
        finally:
            self.is_connecting = False

    def on_disconnect(self, client):
        self.is_connected = False
        self.status_label.setText("🔴 断开重连中...")
        QTimer.singleShot(2000, lambda: asyncio.ensure_future(self.start_ble_task()))

    # ★ 核心改动：二进制协议解析
    def handle_ble_notification(self, sender, data):
        try:
            self.data_buffer += data

            # 从缓冲区中提取完整的 94 字节帧
            while len(self.data_buffer) >= PACKET_SIZE:
                # 查找帧头 0xAA
                header_idx = -1
                for i in range(len(self.data_buffer) - PACKET_SIZE + 1):
                    if self.data_buffer[i] == PACKET_HEADER:
                        header_idx = i
                        break

                if header_idx == -1:
                    # 没找到帧头，丢弃除最后1字节外的所有数据
                    # (最后1字节可能是帧头的一半)
                    if len(self.data_buffer) > 1:
                        self.data_buffer = self.data_buffer[-1:]
                    break

                # 丢弃帧头之前的垃圾数据
                if header_idx > 0:
                    self.data_buffer = self.data_buffer[header_idx:]
                    self.error_count += header_idx  # 丢弃的字节数

                # 检查是否有足够的数据
                if len(self.data_buffer) < PACKET_SIZE:
                    break

                # 检查帧尾 0x55
                if self.data_buffer[PACKET_SIZE - 1] == PACKET_TAIL:
                    packet_data = bytes(self.data_buffer[:PACKET_SIZE])
                    self.data_buffer = self.data_buffer[PACKET_SIZE:]

                    parsed = parse_combined_packet(packet_data)
                    if parsed and parsed["type"] == "FRAME":
                        # ====== 核心修改：直接在这里交叉赋值 ======
                        self.left_adc = parsed["r_adc"]   # 拿后半截的 ADC 给左脚
                        self.right_adc = parsed["l_adc"]  # 拿前半截的 ADC 给右脚
                        self.left_imu = parsed["r_imu"]   # 拿后半截的 IMU 给左脚
                        self.right_imu = parsed["l_imu"]  # 拿前半截的 IMU 给右脚
                        # ==========================================
                        self.frame_count += 1
                        self.fps_frame_count += 1  # <--- 新增：累加1秒内的瞬时帧数
                        
                        
                    else:
                        self.error_count += 1
                else:
                    # 帧尾不对，跳过这个假帧头，继续搜索
                    self.data_buffer = self.data_buffer[1:]
                    self.error_count += 1
        except Exception:
            self.data_buffer = b""

    # ====== 新增：帧率结算函数 ======
    def update_fps_tick(self):
        # 每秒更新一次界面，并把计数器清零
        self.frame_label.setText(f"⚡ 帧率: {self.fps_frame_count} Hz")
        self.fps_frame_count = 0
    # ==============================

    # ================== UI更新与AI逻辑 ==================
    def update_ui_timer_tick(self):
        self.error_label.setText(f"⚠️ 丢包: {self.error_count}")
        
        if self.is_recording:
            self.data_count_label.setText(f"💾 记录: {len(self.recorded_data)}")

        left_mapped = self.remap_sensor_data(self.left_adc, self.left_sensor_mapping)
        right_mapped = self.remap_sensor_data(self.right_adc, self.right_sensor_mapping)

        self.left_insole.update_sensor_data(left_mapped)
        self.right_insole.update_sensor_data(right_mapped)

        try:
            for i in range(16):
                self.left_bar_set.replace(i, left_mapped[i])
                self.right_bar_set.replace(i, right_mapped[i])
        except Exception: pass

        imu_names = ["AX", "AY", "AZ", "GX", "GY", "GZ"]
        try:
            for i in range(6):
                lv = self.left_imu[i]
                c_l = "#27ae60" if lv > 0 else "#c0392b" if lv < 0 else "#2c3e50"
                self.left_imu_labels[i].setText(f"<span style='font-size:11px;color:#7f8c8d;'>左 {imu_names[i]}</span><br><span style='color:{c_l}; font-size:18px; font-weight:bold;'>{lv}</span>")

                rv = self.right_imu[i]
                c_r = "#2980b9" if rv > 0 else "#c0392b" if rv < 0 else "#2c3e50"
                self.right_imu_labels[i].setText(f"<span style='font-size:11px;color:#7f8c8d;'>右 {imu_names[i]}</span><br><span style='color:{c_r}; font-size:18px; font-weight:bold;'>{rv}</span>")
        except Exception: pass

    # 🚀 核心修改 3：绝对均匀的 30Hz 零阶保持采样器
    def process_uniform_frame(self):
        if getattr(self, 'is_closing', False): return
        
        # 1. 抓取当前时刻的最新“内存快照”（不管底层蓝牙刚才卡没卡，我只管拿最新的）
        left_mapped = self.remap_sensor_data(self.left_adc, self.left_sensor_mapping)
        right_mapped = self.remap_sensor_data(self.right_adc, self.right_sensor_mapping)
        
        current_frame_features = list(left_mapped) + list(right_mapped) + list(self.left_imu) + list(self.right_imu)

        # 2. 如果正在录制，强行打上 30Hz 的完美均匀时间戳！
        if self.is_recording:
            # 33.333 毫秒一帧 = 绝对的 30.0 Hz
            elapsed_ms = int(self.record_frame_count * 33.333)
            self.record_frame_count += 1
            
            recorded_row = [elapsed_ms] + current_frame_features
            self.recorded_data.append(recorded_row)

        # 3. 将这帧均匀数据推入 AI 滑动窗口
        if self.is_connected and self.is_ai_enabled:
            self.feature_window.append(current_frame_features)
            
            # 当队列刚满 21 帧时 (1500ms 窗口成型)
            if len(self.feature_window) == self.window_size:
                self.inference_counter += 1
                
                # 每滑动 7 帧触发一次 WebAssembly 预测
                if self.inference_counter >= self.inference_step and not self.is_inferencing:
                    self.inference_counter = 0
                    flat_features = [val for frame in self.feature_window for val in frame]
                    asyncio.ensure_future(self.send_to_nodejs(flat_features))

    def toggle_recording(self):
        if not self.is_connected:
            self.status_label.setText("🔴 请先连接设备")
            return
        if self.is_recording:
            self.is_recording = False
            self.record_button.setText("▶ 开始录制")
            self.record_button.setStyleSheet("QPushButton { background-color: #27ae60; color: white; border-radius: 4px; padding: 6px 15px; font-weight: bold; }")
        else:
            self.recorded_data = []
            self.is_recording = True
            self.error_count = 0
            self.frame_count = 0
            # --- 新增：初始化记录帧数，并打下真实时间的起点 ---
            self.record_frame_count = 0
            self.record_start_time = time.time()
            # ---------------------------------------------
            # --- 新增：清空上一帧的特征记忆 ---
            
            self.record_button.setText("⏹ 停止录制")
            self.record_button.setStyleSheet("QPushButton { background-color: #e74c3c; color: white; border-radius: 4px; padding: 6px 15px; font-weight: bold; }")

    def toggle_ai(self):
        if not self.is_connected:
            self.status_label.setText("🔴 请先连接设备")
            return
        if self.is_ai_enabled:
            self.is_ai_enabled = False
            self.ai_button.setText("▶ 开启 AI 预测")
            self.ai_button.setStyleSheet("QPushButton { background-color: #8e44ad; color: white; border-radius: 4px; padding: 6px 15px; font-weight: bold; }")
            self.predict_label.setText("🤖 预测: 已暂停")
            self.predict_label.setStyleSheet("font-size: 18px; color: #7f8c8d; font-weight: bold; background: #eaeded; padding: 5px; border-radius: 5px;")
            self.feature_window.clear()
            self.inference_counter = 0
            self.history_labels.clear()
        else:
            self.is_ai_enabled = True
            self.ai_button.setText("⏹ 停止 AI 预测")
            self.ai_button.setStyleSheet("QPushButton { background-color: #e74c3c; color: white; border-radius: 4px; padding: 6px 15px; font-weight: bold; }")
            self.predict_label.setText("🤖 预测: 积攒数据中...")
            self.predict_label.setStyleSheet("font-size: 18px; color: #8e44ad; font-weight: bold; background: #fdf2e9; padding: 5px; border-radius: 5px;")

    @asyncSlot()
    async def send_to_nodejs(self, features):
        self.is_inferencing = True
        try:
            async with aiohttp.ClientSession() as session:
                async with session.post('http://127.0.0.1:8080/predict', json={'features': features}) as resp:
                    if resp.status == 200:
                        result = await resp.json()
                        if "classification" in result:
                            classifications = result["classification"]
                            best_class = max(classifications, key=classifications.get)
                            score = classifications[best_class]
                            self.history_labels.append(best_class)
                            if len(self.history_labels) >= 2:
                                smoothed_class = max(set(self.history_labels), key=self.history_labels.count)
                            else:
                                smoothed_class = best_class
                            label_str = smoothed_class.upper()
                            self.predict_label.setText(f"🤖 预测: {label_str} ({score:.2f})")
                            # --- 医疗级病态与动作标签精细分类 UI ---
                            high_risk_classes = ["HEMIPLEGIC", "ANTALGIC", "BLIND_PROBE"]
                            warning_classes = ["DOWNSTAIR", "UPSTAIR"]
                            safe_classes = ["WALKING", "STAND", "SIT"]

                            if label_str in high_risk_classes:
                                # 🚨 高危病理步态：刺眼的红底白字 + 警报图标
                                self.predict_label.setText(f"🚨 高危步态: {label_str} ({score:.2f})")
                                self.predict_label.setStyleSheet("font-size: 19px; color: #FFFFFF; font-weight: bold; background: #c0392b; padding: 5px; border-radius: 5px;")
                            
                            elif label_str in warning_classes:
                                # ⚠️ 复杂地形(上下楼)：橙色预警
                                self.predict_label.setText(f"⚠️ 地形切换: {label_str} ({score:.2f})")
                                self.predict_label.setStyleSheet("font-size: 18px; color: #d35400; font-weight: bold; background: #fdebd0; padding: 5px; border-radius: 5px;")
                                
                            elif label_str in safe_classes:
                                # 🟢 安全日常动作：舒适的护眼绿/蓝
                                icon = "🚶" if label_str == "WALKING" else "🧍" if label_str == "STAND" else "🪑"
                                self.predict_label.setText(f"{icon} 日常: {label_str} ({score:.2f})")
                                color = "#27ae60" if label_str == "WALKING" else "#2980b9"
                                bg = "#e8f8f5" if label_str == "WALKING" else "#ebf5fb"
                                self.predict_label.setStyleSheet(f"font-size: 18px; color: {color}; font-weight: bold; background: {bg}; padding: 5px; border-radius: 5px;")
                            else:
                                # 默认托底
                                self.predict_label.setText(f"🤖 预测: {label_str} ({score:.2f})")
                                self.predict_label.setStyleSheet("font-size: 18px; color: #2c3e50; font-weight: bold; background: #ecf0f1; padding: 5px; border-radius: 5px;")
        except Exception as e:
            print(f"❌ 通信出错: {e}")
            self.predict_label.setText("🤖 模型推断错误")
            self.predict_label.setStyleSheet("font-size: 18px; color: #e74c3c; font-weight: bold;")
        finally:
            self.is_inferencing = False

    @asyncSlot()
    async def save_data_to_file(self, file_path):
        data_to_save = list(self.recorded_data)
        if not data_to_save: return
        try:
            with open(file_path, 'w') as f:
                h_l_adc = [f"L_ADC_{i+1}" for i in range(16)]
                h_r_adc = [f"R_ADC_{i+1}" for i in range(16)]
                h_l_imu = ["L_IMU_AX", "L_IMU_AY", "L_IMU_AZ", "L_IMU_GX", "L_IMU_GY", "L_IMU_GZ"]
                h_r_imu = ["R_IMU_AX", "R_IMU_AY", "R_IMU_AZ", "R_IMU_GX", "R_IMU_GY", "R_IMU_GZ"]
                # --- 修改：在表头最前面加上 "timestamp" ---
                headers = ["timestamp"] + h_l_adc + h_r_adc + h_l_imu + h_r_imu
                # ---------------------------------------
                f.write(",".join(headers) + "\n")
                for row in data_to_save:
                    f.write(",".join(map(str, row)) + "\n")
            self.data_count_label.setText(f"✅ 已保存: {len(data_to_save)}")
        except Exception:
            self.error_label.setText("保存失败")

    def save_data_prompt(self):
        if self.is_recording: return
        if not self.recorded_data: return
        
        # 1. 定义你的目标文件夹路径 (使用 r"" 声明为原始字符串，避免 \t 等被转义)
        target_dir = os.path.join(os.path.expanduser("~"), "insole_data")
        
        # 2. 如果该文件夹不存在，自动创建它
        try:
            os.makedirs(target_dir, exist_ok=True)
        except OSError:
            target_dir = os.getcwd()
            
        # 3. 生成文件名，并和目录拼接成完整的默认绝对路径
        default_filename = f"Dual_Insole_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        default_path = os.path.join(target_dir, default_filename)
        
        # 4. 这里的第三个参数改成拼接好的 default_path
        file_path, _ = QFileDialog.getSaveFileName(self, "Save Data", default_path, "CSV Files (*.csv)")
        if file_path:
            asyncio.ensure_future(self.save_data_to_file(file_path))

    async def close_ble(self):
        if self.ble_client and self.is_connected:
            try:
                await self.ble_client.stop_notify(self.CHAR_UUID_TX)
                await self.ble_client.disconnect()
            except: pass

    def closeEvent(self, event):
        if self.is_connected: asyncio.ensure_future(self.close_ble())
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    loop = QEventLoop(app)
    asyncio.set_event_loop(loop)
    win = gd32_show()
    win.show()
    with loop:
        loop.run_forever()