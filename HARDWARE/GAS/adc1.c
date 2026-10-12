/**
 * @file    adc1.c
 * @brief   ADC1 公共驱动层实现
 */

#include "adc1.h"

/** 初始化标志，避免重复初始化外设 */
static u8 s_initialized = 0;

void adc1_init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    ADC_InitTypeDef  ADC_InitStructure;

    if (s_initialized) return;
    s_initialized = 1;

    /* 使能 GPIOA 与 ADC1 时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);

    /* ADC 时钟 = PCLK2 / 6 = 72MHz / 6 = 12MHz
     * 数据手册要求 ADC 时钟不得超过 14MHz */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    /* PA1 (MQ-7)、PA2 (电池分压) 配置为模拟输入
     * 模拟输入模式会关闭引脚的数字输入通道，避免读数受到干扰 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_1 | GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* ADC1 工作于独立模式、单通道、单次转换、结果右对齐 */
    ADC_DeInit(ADC1);
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode       = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    /* 使能 ADC 并执行自校准
     * 数据手册要求：校准前 ADC 需上电至少 2 个 ADC 时钟周期 */
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

u16 adc1_read(u8 channel)
{
    if (!s_initialized) adc1_init();

    /* 采样时间设为 239.5 周期：MQ-7 模块输出阻抗较高（数 kΩ），
     * 采样时间不足会导致采样保持电容充电不完全，读数偏低 */
    ADC_RegularChannelConfig(ADC1, channel, 1, ADC_SampleTime_239Cycles5);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
    return ADC_GetConversionValue(ADC1);
}

u16 adc1_read_avg(u8 channel, u8 n)
{
    u32 sum = 0;
    u8  i;

    if (n == 0) n = 1;
    for (i = 0; i < n; i++) sum += adc1_read(channel);

    return (u16)(sum / n);
}

u16 adc1_read_mv(u8 channel, u8 n)
{
    return (u16)(((u32)adc1_read_avg(channel, n) * ADC1_VREF_MV) / 4095);
}
