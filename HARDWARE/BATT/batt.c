/**
 * @file    batt.c
 * @brief   电池电压检测与电量估算实现
 * @details 电量估算采用"放电曲线查表 + 区间线性插值"。
 *
 *          锂电池放电曲线呈平台特性：3.7V ~ 3.5V 区间电压变化极小，
 *          但对应的电量变化显著，因此线性换算会产生较大误差。
 *          查表法可显著改善显示准确性。
 */

#include "batt.h"
#include "adc1.h"

/** 全局电池数据 */
batt_data_t g_batt;

/**
 * 锂电池放电曲线对照表（电压 mV -> 剩余电量 %）
 *
 * 数据取自常见 18650 / 软包电芯的经验值，取值刻意偏保守：
 * 电量显示偏乐观比偏悲观更危险（显示 20% 却在一分钟内关机）。
 */
static const u16 s_curve_mv[]  = {4200, 4100, 4000, 3900, 3800, 3750, 3700, 3650, 3600, 3550, 3500, 3400, 3300};
static const u8  s_curve_pct[] = { 100,   95,   88,   80,   70,   60,   50,   40,   30,   20,   10,    5,    0};

#define CURVE_LEN (sizeof(s_curve_mv) / sizeof(s_curve_mv[0]))

/**
 * @brief  将电池电压换算为剩余电量百分比
 * @param  mv  电池电压 (mV)
 * @retval 电量百分比 (0 ~ 100)
 */
static u8 mv_to_percent(u16 mv)
{
    u8 i;

    if (mv >= s_curve_mv[0])             return 100;
    if (mv <= s_curve_mv[CURVE_LEN - 1]) return 0;

    /* 在相邻两个标定点之间线性插值 */
    for (i = 0; i < CURVE_LEN - 1; i++)
    {
        if (mv <= s_curve_mv[i] && mv >= s_curve_mv[i + 1])
        {
            u16 v_hi = s_curve_mv[i];
            u16 v_lo = s_curve_mv[i + 1];
            u8  p_hi = s_curve_pct[i];
            u8  p_lo = s_curve_pct[i + 1];
            u32 num  = (u32)(mv - v_lo) * (p_hi - p_lo);

            return (u8)(p_lo + num / (v_hi - v_lo));
        }
    }

    return 0;
}

void BATT_Init(void)
{
    adc1_init();
    BATT_Task();
}

void BATT_Task(void)
{
    static u16 tick = 0;
    u16 v_div;
    u16 mv;

    /* 采样限流：电池电压变化缓慢，无需每个任务周期都读取 ADC */
    tick++;
    if (tick < 10) return;
    tick = 0;

    /* 分压电阻阻值较高（100k），单次采样抖动明显，取 8 次平均 */
    v_div = adc1_read_mv(BATT_ADC_CHANNEL, 8);

    mv = v_div * BATT_DIVIDER_NUM;

    g_batt.mv       = mv;
    g_batt.percent  = mv_to_percent(mv);
    g_batt.low      = (mv < BATT_LOW_MV)  ? 1 : 0;
    g_batt.critical = (mv < BATT_CRIT_MV) ? 1 : 0;
}
