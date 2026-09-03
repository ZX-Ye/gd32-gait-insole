/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#include "lcd_mytest.h"
#include "stdio.h"
#include "main.h" 

// --- 引用 main.c 中真实的全局变量 ---
extern volatile uint32_t g_h7_frame_count;        
extern volatile uint8_t  g_ai_result;           
// --- 在文件开头添加外部变量引用和新的 UI 指针 ---
extern volatile uint8_t  g_abnormal_ratio;
extern volatile uint16_t g_1min_frame_counter;

// --- UI 对象指针 ---
lv_obj_t * bottom_progress_label;   // 左下角：低调的评估进度
lv_obj_t * bottom_risk_label;       // 中间靠下：醒目的高危指数

// --- UI 对象指针 ---
lv_obj_t * ui_border;           // 科幻边框
lv_obj_t * top_status_label;    // 顶部连接状态 (带图标)
lv_obj_t * ai_result_cn;        // 核心：中文大字报
lv_obj_t * ai_result_en;        // 辅助：英文小字报 (增加科技感)
lv_obj_t * bottom_fm_label;     // 底部帧数监视器

// ==========================================
// 🚨 声明自定义的 LVGL 中文字体 (见下文第二步教程)
// ==========================================
LV_FONT_DECLARE(ui_font_cn_48); // 48号中文字体

// ==========================================
// 🚀 核心升级 1：中英双语 8 分类字典！
// ==========================================
static const char* action_names_cn[8] = {
    "痛性跛行", "盲态探步", "正在下楼", "偏瘫步态", 
    "静止坐立", "静止站立", "正在上楼", "正常走路"
};

static const char* action_names_en[8] = {
    "ANTALGIC", "BLIND PROBE", "DOWNSTAIRS", "HEMIPLEGIC", 
    "SITTING", "STANDING", "UPSTAIRS", "WALKING"
};

// ==========================================
// 🚀 核心升级 2：医疗级 RGB565 警示色卡
// ==========================================
static const uint16_t action_colors_rgb565[8] = {
    0xF800, // 0: 红色 (危)
    0xF800, // 1: 红色 (危)
    0xFD20, // 2: 橙色 (警)
    0xF800, // 3: 红色 (危)
    0x8410, // 4: 灰蓝 (安)
    0x07FF, // 5: 亮青 (安)
    0xFD20, // 6: 橙色 (警)
    0x07E0  // 7: 亮绿 (安)
};

// ==========================================
// 🎨 酷炫医疗 Dashboard 初始化
// ==========================================
void create_dashboard_ui(void) {
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x050508), 0); 

    // 1. 科幻边框
    ui_border = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_border, LV_PCT(94), LV_PCT(94));
    lv_obj_align(ui_border, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(ui_border, 0, 0); 
    lv_obj_set_style_border_color(ui_border, lv_color_hex(0x222233), 0);
    lv_obj_set_style_border_width(ui_border, 2, 0);
    lv_obj_set_style_radius(ui_border, 12, 0);

    // 2. 左上角状态
    top_status_label = lv_label_create(lv_scr_act());
    lv_obj_align(top_status_label, LV_ALIGN_TOP_LEFT, 15, 15);
    lv_label_set_text(top_status_label, LV_SYMBOL_WIFI " H737 EDGE-AI ONLINE");
    lv_obj_set_style_text_color(top_status_label, lv_color_hex(0x00FFCC), 0); 
    lv_obj_set_style_text_font(top_status_label, &lv_font_montserrat_16, 0);

    // 3. 居中超大中文警报 (你的48号中文字体)
    ai_result_cn = lv_label_create(lv_scr_act());
    lv_obj_align(ai_result_cn, LV_ALIGN_CENTER, 0, -35); // 稍微往上移一点，给下面腾位置
    lv_obj_set_style_text_font(ai_result_cn, &ui_font_cn_48, 0); 
    lv_label_set_text(ai_result_cn, "等待同步");
    lv_obj_set_style_text_color(ai_result_cn, lv_color_hex(0x555555), 0);

    // 4. 居中英文副标题
    ai_result_en = lv_label_create(lv_scr_act());
    lv_obj_align_to(ai_result_en, ai_result_cn, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lv_obj_set_style_text_font(ai_result_en, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_letter_space(ai_result_en, 6, 0); 
    lv_label_set_text(ai_result_en, "WAITING SYNC...");
    lv_obj_set_style_text_color(ai_result_en, lv_color_hex(0x666677), 0);

    // ==========================================
    // 🚀 新增 UI 布局：黄金三角区
    // ==========================================
    // 5. 右下角：心跳帧数
    bottom_fm_label = lv_label_create(lv_scr_act());
    lv_obj_align(bottom_fm_label, LV_ALIGN_BOTTOM_RIGHT, -15, -15);
    lv_label_set_text(bottom_fm_label, "FM: 00000");
    lv_obj_set_style_text_color(bottom_fm_label, lv_color_hex(0x444455), 0);
    lv_obj_set_style_text_font(bottom_fm_label, &lv_font_montserrat_16, 0);

    // 6. 左下角：永远滚动的进度条
    bottom_progress_label = lv_label_create(lv_scr_act());
    lv_obj_align(bottom_progress_label, LV_ALIGN_BOTTOM_LEFT, 15, -15);
    lv_label_set_text(bottom_progress_label, "EVAL: 0%");
    lv_obj_set_style_text_color(bottom_progress_label, lv_color_hex(0x7F8C8D), 0);
    lv_obj_set_style_text_font(bottom_progress_label, &lv_font_montserrat_16, 0);

    // 7. 中间靠下：醒目的异常指数看板 (采用 32 号字体！)
    bottom_risk_label = lv_label_create(lv_scr_act());
    lv_obj_align(bottom_risk_label, LV_ALIGN_BOTTOM_MID, 0, -15); 
    lv_obj_set_style_text_font(bottom_risk_label, &lv_font_montserrat_32, 0); // 🚨 醒目但不抢主标题
    lv_label_set_text(bottom_risk_label, "RISK: CALC...");
    lv_obj_set_style_text_color(bottom_risk_label, lv_color_hex(0x555555), 0);
}

// ==========================================
// 🔄 极速刷新函数
// ==========================================
void update_dashboard_ui(void) {
    char buf[32];
    char cn_buf[64];

    // 1. 刷新帧数
    sprintf(buf, "FM: %06u", g_h7_frame_count);
    lv_label_set_text(bottom_fm_label, buf);

    // 2. 刷新左下角进度条 (0~100% 滚动循环)
    if (g_ai_result <= 7) {
        int progress = (g_1min_frame_counter * 100) / 1800;
        sprintf(buf, "EVAL: %d%%", progress);
        lv_label_set_text(bottom_progress_label, buf);
    }

    // 3. 刷新正下方核心风险率！
    if (g_abnormal_ratio != 0xFF) {
        sprintf(buf, "RISK: %d%%", g_abnormal_ratio);
        lv_label_set_text(bottom_risk_label, buf);
        
        // 动态变色警告系统
        if (g_abnormal_ratio > 30) {
            lv_obj_set_style_text_color(bottom_risk_label, lv_color_hex(0xE74C3C), 0); // 危红
        } else if (g_abnormal_ratio > 10) {
            lv_obj_set_style_text_color(bottom_risk_label, lv_color_hex(0xF1C40F), 0); // 警黄
        } else {
            lv_obj_set_style_text_color(bottom_risk_label, lv_color_hex(0x2ECC71), 0); // 安绿
        }
    }

    // 4. 中间大字报核心刷新 (保持原有逻辑)
    if (g_ai_result <= 7) { 
        lv_color_t current_color;
        current_color.full = action_colors_rgb565[g_ai_result]; 
        
        if (g_ai_result == 0 || g_ai_result == 1 || g_ai_result == 3) {
            sprintf(cn_buf, LV_SYMBOL_WARNING " %s", action_names_cn[g_ai_result]);
            lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x330000), 0); 
            lv_obj_set_style_border_color(ui_border, lv_color_hex(0x880000), 0); 
        } else if (g_ai_result == 2 || g_ai_result == 6) {
            sprintf(cn_buf, LV_SYMBOL_UP " %s", action_names_cn[g_ai_result]);
            lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x050508), 0);
            lv_obj_set_style_border_color(ui_border, lv_color_hex(0x884400), 0);
        } else {
            sprintf(cn_buf, LV_SYMBOL_OK " %s", action_names_cn[g_ai_result]);
            lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x050508), 0); 
            lv_obj_set_style_border_color(ui_border, lv_color_hex(0x222233), 0); 
        }

        lv_label_set_text(ai_result_cn, cn_buf);
        lv_obj_set_style_text_color(ai_result_cn, current_color, 0);
        
        lv_label_set_text(ai_result_en, action_names_en[g_ai_result]);
        lv_obj_set_style_text_color(ai_result_en, current_color, 0);
        
    }
}

// 哑函数防报错
void create_adc_btn(void) { create_dashboard_ui(); }
void update_uart_adc_display(void) {}
void update_imu_display(void) {}
void create_uart_adc_display(void) {}
void update_adc_display(void) {}
void lcd_rgb_config(void) {}
	