/**
 * @file    gas_mq7.c
 * @brief   MQ-7 一氧化碳传感器驱动实现
 * @details 传感器工作原理：加热丝将 SnO2 敏感层加热至工作温度，
 *          环境中 CO 浓度升高时敏感层表面电阻 Rs 减小。
 *          由于模块采用 Rs 与负载电阻 RL 串联分压，
 *          因此输出电压越低，表示 CO 浓度越高。
 *
 *          换算流程分为四步，分别对应本文件中的四个处理环节：
 *            ① ADC      -> 电压    adc_to_mv()
 *            ② 电压     -> Rs      mv_to_rs()
 *            ③ Rs       -> Rs/R0   在 MQ7_Task() 中计算
 *            ④ Rs/R0    -> ppm     ratio_to_ppm()
 */

#include "gas_mq7.h"
#include "adc1.h"
#include "delay.h"
#include "eeprom.h"
#include <math.h>

/** 全局传感器数据 */
mq7_data_t g_mq7;

/** 滤波后的 ADC 值 */
static float s_filtered = 0;

/** 累计预热时长 (ms) */
static u32 s_warmup_ms = 0;

/* ==========================================================================
 * 内部函数
 * ========================================================================== */

/**
 * @brief  ADC 原始值转换为电压值
 * @param  adc  12 位 ADC 结果
 * @retval 电压值 (mV)
 */
static u16 adc_to_mv(u16 adc)
{
    return (u16)(((u32)adc * ADC1_VREF_MV) / 4095);
}

/**
 * @brief  输出电压换算为传感器电阻 Rs
 * @param  v_out_mv  传感器输出电压 (mV)
 * @retval 传感器电阻 Rs (Ω)
 *
 * @details 模块内部为串联分压结构：
 *              VCC ──[ Rs ]──┬──[ RL ]── GND
 *                            └── AO (Vout)
 *          由此可得  Vout = VCC × RL / (Rs + RL)
 *          反解得    Rs   = RL × (VCC − Vout) / Vout
 *
 * @note   输出接近 VCC 时 Rs 趋于无穷大，此处返回极大值以避免除零
 */
static u32 mv_to_rs(u16 v_out_mv)
{
    u32 v = v_out_mv;

    if (v < 1) v = 1;
    if (v >= MQ7_VC_MV) return 99999999UL;

    return (u32)(((u32)MQ7_RL_OHM * (u32)(MQ7_VC_MV - v)) / v);
}

/**
 * @brief  Rs/R0 比值换算为 CO 浓度
 * @param  ratio  Rs / R0
 * @retval CO 浓度 (ppm)
 *
 * @details 依据数据手册双对数曲线拟合的幂函数：
 *              ppm = A × (Rs/R0)^B，其中 B 为负值
 */
static float ratio_to_ppm(float ratio)
{
    float ppm;

    if (ratio <= 0.0001f) ratio = 0.0001f;

    ppm = MQ7_CURVE_A * (float)pow((double)ratio, (double)MQ7_CURVE_B);

    if (ppm < MQ7_PPM_MIN) ppm = MQ7_PPM_MIN;
    if (ppm > MQ7_PPM_MAX) ppm = MQ7_PPM_MAX;

    return ppm;
}

/* ==========================================================================
 * 对外接口
 * ========================================================================== */

void MQ7_Init(void)
{
    adc1_init();

    /* 从 EEPROM 载入已保存的 R0 标定值 */
    g_mq7.r0_ohm = 0;
    EEPROM_ReadR0(&g_mq7.r0_ohm);

    if (g_mq7.r0_ohm == 0 || g_mq7.r0_ohm == 0xFFFFFFFFUL)
    {
        /* 尚未标定：暂以负载电阻值作为占位，界面侧可通过比较该值判断标定状态 */
        g_mq7.r0_ohm = MQ7_RL_OHM;
    }

    s_filtered  = 0;
    s_warmup_ms = 0;
    g_mq7.warmup_done = 0;
    g_mq7.warmup_sec  = 0;
}

u8 MQ7_Task(u16 ms_elapsed)
{
    u16 raw;

    /* ---- 预热计时 ---- */
    if (s_warmup_ms < MQ7_WARMUP_MS)
    {
        s_warmup_ms += ms_elapsed;
        if (s_warmup_ms >= MQ7_WARMUP_MS)
        {
            s_warmup_ms = MQ7_WARMUP_MS;
            g_mq7.warmup_done = 1;
        }
    }
    g_mq7.warmup_sec = (u16)(s_warmup_ms / 1000);

    /* ---- 采样与一阶低通滤波 ---- */
    raw = adc1_read_avg(MQ7_ADC_CHANNEL, MQ7_ADC_SAMPLE_NUM);

    if (s_filtered < 1.0f)
    {
        /* 首次采样直接赋值，避免滤波器从 0 缓慢爬升 */
        s_filtered = (float)raw;
    }
    else
    {
        s_filtered = s_filtered + MQ7_FILTER_ALPHA * ((float)raw - s_filtered);
    }

    /* ---- 四级换算 ---- */
    g_mq7.adc_raw  = (u16)s_filtered;
    g_mq7.v_out_mv = adc_to_mv((u16)s_filtered);
    g_mq7.rs_ohm   = mv_to_rs(g_mq7.v_out_mv);
    g_mq7.ratio    = (float)g_mq7.rs_ohm / (float)g_mq7.r0_ohm;
    g_mq7.ppm      = ratio_to_ppm(g_mq7.ratio);

    return 1;
}

void MQ7_CalibrateCleanAir(void)
{
    u32 rs;

    /* 多次采样取平均，保证标定值稳定 */
    s_filtered = (float)adc1_read_avg(MQ7_ADC_CHANNEL, 64);
    rs = mv_to_rs(adc_to_mv((u16)s_filtered));

    /* 洁净空气中 Rs/R0 约为 3.8，据此反推 R0 */
    g_mq7.r0_ohm = (u32)((float)rs / MQ7_RATIO_CLEAN_AIR);
    if (g_mq7.r0_ohm == 0) g_mq7.r0_ohm = 1;

    EEPROM_SaveR0(g_mq7.r0_ohm);

    g_mq7.rs_ohm = rs;
    g_mq7.ratio  = (float)rs / (float)g_mq7.r0_ohm;
    g_mq7.ppm    = ratio_to_ppm(g_mq7.ratio);
}

u16 MQ7_GetRawAdc(void)
{
    return adc1_read(MQ7_ADC_CHANNEL);
}
