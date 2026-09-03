/**
 * @file lv_port_disp_templ.c
 *
 */

/*Copy this file as "lv_port_disp.c" and set this value to "1" to enable content*/
#if 1

/*********************
 * INCLUDES
 *********************/
#include "lv_port_disp_template.h"
#include <stdbool.h>
#include "lcd.h"
#include "lcd_ui.h"
#include "gd32f4xx.h" // ?????????

/*********************
 * DEFINES
 *********************/
#ifndef MY_DISP_HOR_RES
    #define MY_DISP_HOR_RES   800
#endif
#ifndef MY_DISP_VER_RES
    #define MY_DISP_VER_RES   480
#endif
	
#define LV_HOR_RES_MAX (800)
	
extern uint16_t ltdc_lcd_framebuf0[800][480];
// extern volatile uint8_t g_gpu_state; // ???????,???????
extern lv_disp_drv_t *g_disp_drv;

/**********************
 * TYPEDEFS
 **********************/

/**********************
 * STATIC PROTOTYPES
 **********************/
static void disp_init(void);
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);

/**********************
 * STATIC VARIABLES
 **********************/

// ???????:? * 40? (? 1/12 ??)
// ???? SRAM ????????
#define SHOW_BUF_SIZE (MY_DISP_HOR_RES * 40)

static lv_disp_draw_buf_t draw_buf_dsc_2;
static lv_color_t buf_1[SHOW_BUF_SIZE]; 
static lv_color_t buf_2[SHOW_BUF_SIZE]; 

/**********************
 * GLOBAL FUNCTIONS
 **********************/

void lv_port_disp_init(void)
{
    /*-------------------------
     * Initialize your display
     * -----------------------*/
    disp_init();

    /*-----------------------------
     * Create a buffer for drawing
     *----------------------------*/
    // ????? (buf_1 ? buf_2)
    // ?? LVGL ??? buf_2 ?,DMA/IPA ???? buf_1,??????
    lv_disp_draw_buf_init(&draw_buf_dsc_2, buf_1, buf_2, SHOW_BUF_SIZE);

    /*-----------------------------------
     * Register the display in LVGL
     *----------------------------------*/
    static lv_disp_drv_t disp_drv;                 
    lv_disp_drv_init(&disp_drv);                    

    /*Set the resolution of the display*/
    disp_drv.hor_res = MY_DISP_HOR_RES;
    disp_drv.ver_res = MY_DISP_VER_RES;

    /*Used to copy the buffer's content to the display*/
    disp_drv.flush_cb = disp_flush;

    /*Set a display buffer*/
    disp_drv.draw_buf = &draw_buf_dsc_2;

    /* ????????????,??????,???? FPS */
    disp_drv.full_refresh = 0;

    /*Finally register the driver*/
    lv_disp_drv_register(&disp_drv);
}

/**********************
 * STATIC FUNCTIONS
 **********************/

static void disp_init(void)
{
	lcd_disp_config();
}

/*Flush the content of the internal buffer the specific area on the display*/
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    g_disp_drv = disp_drv;

    // ????? (FrameBuffer width - Area width)
    uint16_t offline;
    offline = LV_HOR_RES_MAX - (area->x2 - area->x1 + 1);
    
    // -----------------------------------------------------------
    // ???? 1?: ???? IPA_CTL_FTFIE (??),?????!
    // -----------------------------------------------------------
    IPA_CTL = 0x00000000UL; // ???,????? FTFIE
    
    // ??? ?????????
    // ??:??? cache ??,?????? SCB_CleanDCache,?? M4 ??????
    
    /* Set up pixel format */
    IPA_FPCTL = IPA_DPF_RGB565;             // ?????RGB565
    
    /* Set up pointers */
    IPA_FMADDR = (uint32_t)color_p;         // ??? (SRAM)
    
    // ?????? (SDRAM Framebuffer)
    IPA_DMADDR = (uint32_t)ltdc_lcd_framebuf0 + 2 * (LV_HOR_RES_MAX * area->y1 + area->x1); 
    
    IPA_FLOFF = 0;                          // ???
    IPA_DLOFF = offline;                    // ????
    
    /* Set up size */
    IPA_IMS = (uint32_t)((area->x2 - area->x1 + 1) << 16) | (uint16_t)(area->y2 - area->y1 + 1);
    
    // ???? (TEN)
    IPA_CTL |= IPA_CTL_TEN; 

    // -----------------------------------------------------------
    // ????????: ??????,???????????
    // -----------------------------------------------------------
    while(IPA_CTL & IPA_CTL_TEN);

    // -----------------------------------------------------------
    // ??????: ?? LVGL ???????
    // -----------------------------------------------------------
    lv_disp_flush_ready(disp_drv);
}

#else /*Enable this file at the top*/

typedef int keep_pedantic_happy;
#endif