/**
 * @file    main.c
 * @brief   便携式气体检测仪主程序
 * @details 目标平台：正点原子 MiniSTM32 V3（STM32F103RCT6）+ 2.8 寸电阻触摸屏
 *          外设配置：MQ-7（CO）、DHT11（温湿度）、24C02（日志）、电池分压检测
 *
 *          软件分层：
 *            HARDWARE/        传感器与底层驱动
 *            HARDWARE/APP/    业务逻辑（gas_app.c）与界面（ui_gas.c）
 *            main.c           初始化、任务注册与主循环
 *
 *          调度方式：不使用 RTOS。传感器采样与界面刷新以 LVGL 定时器
 *          （lv_timer）的形式注册，由 lv_task_handler() 统一调度，
 *          时基由 TIM3 中断调用 lv_tick_inc() 提供。
 *
 *          相关文档：docs/USER_GUIDE.md、docs/DEVELOPER_GUIDE.md、
 *                    docs/FLASH_GUIDE.md
 */

#include "delay.h"
#include "sys.h"
#include "usart.h"
#include "lcd.h"
#include "led.h"
#include "key.h"
#include "touch.h"
#include "timer.h"
#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"
#include "gas_app.h"
#include "ui_gas.h"

/**
 * @brief  应用任务回调
 * @details 执行传感器采样、报警判定与日志写入，不涉及界面操作
 * @note   由 LVGL 定时器以 100ms 周期调用
 */
static void app_task_cb(lv_timer_t *t)
{
    GAS_APP_Task();
}

/**
 * @brief  界面刷新任务回调
 * @details 周期设为 200ms：人眼对 5Hz 的刷新率已无明显感知差异，
 *          提高频率只会增加 CPU 占用
 * @note   由 LVGL 定时器以 200ms 周期调用
 */
static void ui_task_cb(lv_timer_t *t)
{
    UI_Gas_Refresh();
}

int main(void)
{
    /* ---------------- 1. 系统层初始化 ---------------- */
    delay_init();                                   /* SysTick 延时，提供 us/ms 级延时 */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); /* 2 位抢占优先级，2 位响应优先级 */
    uart_init(9600);                                /* USART1：PA9/PA10，用于调试输出 */
    TIM3_Int_Init(1, 7199);                         /* TIM3 每 1ms 中断，为 LVGL 提供时基 */

    /* ---------------- 2. GPIO 初始化 ----------------
     * LED_Init / KEY_Init 必须在屏幕初始化之前调用。
     *
     * KEY_Init() 内部执行 GPIO_Remap_SWJ_JTAGDisable，将 PA15/PB3/PB4
     * 从 JTAG 功能释放为普通 GPIO。2.8 寸屏使用 PB0~PB15 作为 16 位数据总线，
     * 其中 PB3/PB4 默认功能为 JTDO/NJTRST，若未释放会导致数据线缺失两位，
     * 表现为屏幕花屏。 */
    LED_Init();
    KEY_Init();

    printf("\r\n===== Gas Detector boot =====\r\n");

    /* ---------------- 3. LVGL 初始化 ---------------- */
    lv_init();
    lv_port_disp_init();    /* 显示移植层，内部调用 LCD_Init() 并配置为 320x240 横屏 */
    lv_port_indev_init();   /* 触摸移植层，首次上电将进入触摸校准流程 */

    /* ---------------- 4. 应用初始化 ---------------- */
    GAS_APP_Init();         /* EEPROM / MQ-7 / DHT11 / 电池检测 */
    UI_Gas_Init();          /* 构建界面控件 */

    /* ---------------- 5. 注册周期任务 ---------------- */
    lv_timer_create(app_task_cb, 100, NULL);
    lv_timer_create(ui_task_cb,  200, NULL);

    ui_task_cb(NULL);       /* 立即刷新一次，避免开机后短暂空屏 */

    printf("LCD ID=0x%04X  %dx%d  Touch %s\r\n",
           lcddev.id, lcddev.width, lcddev.height,
           (tp_dev.touchtype & 0X80) ? "capacitive" : "resistive");

    /* ---------------- 6. 主循环 ---------------- */
    while (1)
    {
        lv_task_handler();
        delay_ms(5);
    }
}
