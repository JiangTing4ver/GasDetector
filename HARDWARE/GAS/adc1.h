/**
 * @file    adc1.h
 * @brief   ADC1 公共驱动层
 * @details MQ-7 气体传感器与电池电压检测共用 ADC1。由于标准外设库不提供
 *          外设互斥机制，多个模块各自调用 ADC_Init 会造成配置互相覆盖，
 *          因此 ADC1 的初始化与读取统一由本模块提供。
 *
 *          通道分配：
 *            - PA1 (ADC1_IN1) : MQ-7 模拟输出
 *            - PA2 (ADC1_IN2) : 电池分压中点
 *
 * @note    新增 ADC 通道时，需在 adc1_init() 中补充对应的 GPIO 配置。
 */

#ifndef __ADC1_H
#define __ADC1_H

#include "sys.h"

/** ADC 参考电压 (mV)。默认按 3.3V 标称值，建议用万用表实测 VDDA 后修正 */
#define ADC1_VREF_MV    3300

/**
 * @brief  初始化 ADC1 及其 GPIO
 * @note   幂等操作，重复调用无副作用
 */
void adc1_init(void);

/**
 * @brief  单次读取指定通道的 ADC 原始值
 * @param  channel  ADC 通道号（ADC_Channel_x）
 * @retval 12 位转换结果，范围 0 ~ 4095
 */
u16 adc1_read(u8 channel);

/**
 * @brief  多次采样并取平均值
 * @param  channel  ADC 通道号
 * @param  n        采样次数，为 0 时按 1 次处理
 * @retval 平均后的 12 位结果
 */
u16 adc1_read_avg(u8 channel, u8 n);

/**
 * @brief  多次采样并换算为电压值
 * @param  channel  ADC 通道号
 * @param  n        采样次数
 * @retval 电压值 (mV)
 */
u16 adc1_read_mv(u8 channel, u8 n);

#endif
