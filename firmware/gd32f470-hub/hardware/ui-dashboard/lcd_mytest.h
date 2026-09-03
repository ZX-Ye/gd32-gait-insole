/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2026 The gd32-gait-insole authors
 */

#ifndef __LCD_MYTEST_H
#define __LCD_MYTEST_H

#include "gd32f4xx.h"
#include "bsp_led.h"
#include "touch.h"
#include "lcd.h"
#include "lcd_ui.h"

#include "lv_conf.h"
#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"


/* ¶¨Òå³£ÓÃÑÕÉ«ºê */
#define LV_COLOR_WHITE      lv_color_hex(0xFFFFFF)
#define LV_COLOR_BLACK      lv_color_hex(0x000000)
#define LV_COLOR_RED        lv_color_hex(0xFF0000)
#define LV_COLOR_GREEN      lv_color_hex(0x00FF00)
#define LV_COLOR_BLUE       lv_color_hex(0x0000FF)
#define LV_COLOR_YELLOW     lv_color_hex(0xFFFF00)
#define LV_COLOR_CYAN       lv_color_hex(0x00FFFF)
#define LV_COLOR_MAGENTA    lv_color_hex(0xFF00FF)
#define LV_COLOR_SILVER     lv_color_hex(0xC0C0C0)
#define LV_COLOR_GRAY       lv_color_hex(0x808080)
#define LV_COLOR_MAROON     lv_color_hex(0x800000)
#define LV_COLOR_OLIVE      lv_color_hex(0x808000)
#define LV_COLOR_LIME       lv_color_hex(0x00FF00)
#define LV_COLOR_TEAL       lv_color_hex(0x008080)
#define LV_COLOR_NAVY       lv_color_hex(0x000080)
#define LV_COLOR_PURPLE     lv_color_hex(0x800080)

void create_dashboard_ui(void);

void  create_led_btn(void);
void lcd_rgb_config(void);
void  create_adc_btn(void);
void update_adc_display(void);
void create_uart_adc_display(void);
void outside_dashbar_display(void);
void update_uart_adc_display(void);
void update_imu_display(void);

// å£°æå¨æ°ç UI å½æ°
void create_dashboard_ui(void);
void update_dashboard_ui(void);
#endif

