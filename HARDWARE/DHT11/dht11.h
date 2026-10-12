/**
 * @file    dht11.h
 * @brief   DHT11 温湿度传感器驱动（单总线协议）
 * @details 传感器通过单根数据线完成通信，时序由软件精确控制。
 *
 *          接线要求：
 *            - VCC  : 3.3V
 *            - DATA : PA3，并在 DATA 与 VCC 之间接入 4.7k ~ 10k 上拉电阻
 *            - GND  : GND
 *
 * @warning 多数 DHT11 模块出厂未焊接上拉电阻，若缺失将导致读取持续失败。
 * @note    引脚选择说明：2.8 寸屏占用 PB0~PB15 与 PC0~PC13，
 *          PA 端口中 PA0/PA1/PA2/PA8/PA9/PA10/PA15 已被占用，
 *          因此选用空闲的 PA3。
 */

#ifndef __DHT11_H
#define __DHT11_H

#include "sys.h"

/* ==========================================================================
 * 硬件配置
 * ========================================================================== */

#define DHT11_GPIO_PORT     GPIOA           /**< 数据线所属端口 */
#define DHT11_GPIO_PIN      GPIO_Pin_3      /**< 数据线引脚 */
#define DHT11_GPIO_CLK      RCC_APB2Periph_GPIOA
#define DHT11_PIN_NUM       3               /**< 引脚序号，供寄存器位操作使用 */

/** 数据线方向切换与电平操作 */
#define DHT11_SDA_OUT()  { GPIOA->CRL &= ~(0x0F << (4 * DHT11_PIN_NUM)); \
                           GPIOA->CRL |=  (0x03 << (4 * DHT11_PIN_NUM)); }
#define DHT11_SDA_IN()   { GPIOA->CRL &= ~(0x0F << (4 * DHT11_PIN_NUM)); \
                           GPIOA->CRL |=  (0x08 << (4 * DHT11_PIN_NUM)); }
#define DHT11_SDA_H()    (GPIOA->BSRR = (u32)1 << DHT11_PIN_NUM)
#define DHT11_SDA_L()    (GPIOA->BRR  = (u32)1 << DHT11_PIN_NUM)
#define DHT11_SDA_READ() ((GPIOA->IDR  >> DHT11_PIN_NUM) & 0x01)

/* ==========================================================================
 * 数据结构
 * ========================================================================== */

/** DHT11 测量结果与状态 */
typedef struct
{
    u8 temperature;     /**< 温度整数部分 (℃)，DHT11 小数位恒为 0 */
    u8 humidity;        /**< 相对湿度整数部分 (%RH) */
    u8 ok;              /**< 本次读取结果：1 成功，0 失败 */
    u8 err_count;       /**< 连续失败次数，用于界面提示传感器异常 */
} dht11_data_t;

/** 全局传感器数据 */
extern dht11_data_t g_dht11;

/* ==========================================================================
 * 对外接口
 * ========================================================================== */

/**
 * @brief  初始化 DHT11 驱动
 * @note   内部包含约 1.2 秒的上电稳定延时
 */
void DHT11_Init(void);

/**
 * @brief  读取一次温湿度数据
 * @retval 1 表示读取成功且校验通过，0 表示失败
 * @note   本函数为阻塞式实现，单次耗时约 22ms；
 *         两次调用间隔不得小于 1 秒，建议 2 秒
 */
u8 DHT11_Read(void);

#endif
