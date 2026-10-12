/**
 * @file    gas_app.c
 * @brief   业务逻辑层实现
 * @details 状态机流转关系：
 *
 *                        预热完成
 *              ┌────────┐ ────────> ┌────────┐  ≥25ppm   ┌─────────┐
 *              │ WARMUP │           │ NORMAL │ ────────> │ WARNING │
 *              └────────┘           └────────┘  <18ppm    └─────────┘
 *                                       ^                      │ ≥50ppm
 *                                       │ <18ppm               │ 持续 3s
 *                                       │                      v
 *                                       │                ┌─────────┐
 *                                       └─────────────── │  ALARM  │
 *                                            <40ppm      └─────────┘
 *
 *              读数饱和 ──> SENSOR_FAULT
 *
 *          日志写入策略：
 *            - 状态跳变时立即写入一条；
 *            - 报警持续期间每 10 分钟补记一条，用于回顾浓度趋势；
 *            - 低电量仅在首次出现时写入，避免频繁擦写 EEPROM；
 *            - 温湿度偶发读取失败不记录（属正常现象，不值得占用存储寿命）。
 */

#include "gas_app.h"
#include "gas_mq7.h"
#include "dht11.h"
#include "batt.h"
#include "eeprom.h"
#include "led.h"
#include "delay.h"

/** 全局设备状态 */
gas_status_t g_status;

/** 累计运行时长 (ms) */
static u32 s_run_ms = 0;

/** 超阈值持续时间累计 (ms)，用于报警确认 */
static u32 s_confirm_ms = 0;

/** 上次报警日志的写入时刻 (ms) */
static u32 s_last_log_ms = 0;

/** 上一次的设备状态，用于检测状态跳变 */
static gas_state_t s_last_state = ST_WARMUP;

/** 上一次的低电量标志，用于检测状态跳变 */
static u8 s_batt_low_last = 0;

/** DHT11 采样分频计数 */
static u8 s_tick_100ms = 0;

/** LED 闪烁计时与当前亮度状态 */
static u16 s_blink_ms = 0;
static u8  s_blink_on = 0;

/**
 * @brief  将设备状态映射为日志事件编码
 * @param  st  设备状态
 * @retval 事件编码，无需记录时返回 0
 */
static u8 state_to_src(gas_state_t st)
{
    switch (st)
    {
        case ST_WARN:  return EE_SRC_CO_WARN;
        case ST_ALARM: return EE_SRC_CO_ALARM;
        default:       return 0;
    }
}

/**
 * @brief  写入一条日志记录
 * @param  src  事件来源编码
 */
static void log_write(u8 src)
{
    ee_log_t rec;

    rec.time_min = s_run_ms / 60000UL;
    rec.co_ppm   = (u16)(g_status.co_ppm + 0.5f);
    rec.temp     = g_status.temp;
    rec.src      = src;

    EEPROM_LogAdd(&rec);
    g_status.log_total = EEPROM_LogCount();
}

const char *GAS_APP_StateName(gas_state_t st)
{
    switch (st)
    {
        case ST_WARMUP:       return "WARM UP";
        case ST_NORMAL:       return "NORMAL";
        case ST_WARN:         return "WARNING";
        case ST_ALARM:        return "ALARM";
        case ST_SENSOR_FAULT: return "SENSOR ERR";
        default:              return "?";
    }
}

const char *GAS_APP_LogSrcName(u8 src)
{
    switch (src)
    {
        case EE_SRC_CO_WARN:     return "CO warn ";
        case EE_SRC_CO_ALARM:    return "CO ALARM";
        case EE_SRC_BATT_LOW:    return "LOW BATT";
        case EE_SRC_SENSOR_FAIL: return "SENS ERR";
        case EE_SRC_BOOT:        return "power on";
        default:                 return "unknown ";
    }
}

void GAS_APP_Init(void)
{
    ee_log_t boot;

    MQ7_Init();
    DHT11_Init();
    BATT_Init();
    EEPROM_Init();

    g_status.co_ppm        = 0;
    g_status.temp          = 0;
    g_status.humi          = 0;
    g_status.dht_ok        = 0;
    g_status.state         = ST_WARMUP;
    g_status.run_min       = 0;
    g_status.log_total     = EEPROM_LogCount();
    g_status.r0_calibrated = (g_mq7.r0_ohm != MQ7_RL_OHM) ? 1 : 0;

    s_run_ms      = 0;
    s_confirm_ms  = 0;
    s_last_log_ms = 0;
    s_last_state  = ST_WARMUP;

    /* 写入开机记录，便于后续追溯设备使用情况 */
    boot.time_min = 0;
    boot.co_ppm   = 0;
    boot.temp     = 0;
    boot.src      = EE_SRC_BOOT;

    if (EEPROM_IsPresent()) EEPROM_LogAdd(&boot);
}

void GAS_APP_Calibrate(void)
{
    MQ7_CalibrateCleanAir();
    g_status.r0_calibrated = 1;
}

void GAS_APP_ClearLog(void)
{
    EEPROM_LogClear();
    g_status.log_total = 0;
}

void GAS_APP_Task(void)
{
    gas_state_t new_state;
    u8 src;

    s_run_ms += 100;
    g_status.run_min = s_run_ms / 60000UL;

    /* ---------- 1. 传感器采样 ---------- */
    MQ7_Task(100);
    BATT_Task();            /* 内部含 1 秒采样限流 */

    /* DHT11 数据手册要求采样间隔不小于 1 秒，此处按 2 秒执行。
     * 单次读取会阻塞约 22ms，对界面流畅度的影响可忽略。 */
    s_tick_100ms++;
    if (s_tick_100ms >= 20)
    {
        s_tick_100ms = 0;
        DHT11_Read();
    }

    g_status.co_ppm     = g_mq7.ppm;
    g_status.co_warm    = (g_mq7.warmup_done == 0) ? 1 : 0;
    g_status.warmup_sec = g_mq7.warmup_sec;
    g_status.temp       = g_dht11.temperature;
    g_status.humi       = g_dht11.humidity;
    g_status.dht_ok     = g_dht11.ok;
    g_status.batt_mv    = g_batt.mv;
    g_status.batt_pct   = g_batt.percent;
    g_status.batt_low   = g_batt.low;

    /* ---------- 2. 状态判定 ---------- */
    if (!g_mq7.warmup_done)
    {
        /* 预热期间传感器输出不稳定，不参与判定 */
        new_state    = ST_WARMUP;
        s_confirm_ms = 0;
    }
    else if (g_mq7.v_out_mv >= MQ7_VC_MV - 20 || g_mq7.adc_raw >= 4090)
    {
        /* 输出接近供电电压或 ADC 饱和，判定为接线异常而非浓度超量程 */
        new_state = ST_SENSOR_FAULT;
    }
    else
    {
        /* 报警确认计时：浓度必须连续超阈值达到 GAS_CONFIRM_MS */
        if (g_mq7.ppm >= GAS_CO_ALARM_PPM)          s_confirm_ms += 100;
        else if (g_mq7.ppm < GAS_CO_ALARM_RELEASE)  s_confirm_ms = 0;

        if (s_confirm_ms >= GAS_CONFIRM_MS)
        {
            new_state = ST_ALARM;
        }
        else if (g_mq7.ppm >= GAS_CO_WARN_PPM)
        {
            new_state = ST_WARN;
        }
        else if (g_mq7.ppm >= GAS_CO_WARN_RELEASE)
        {
            /* 处于迟滞区间：维持原状态，避免阈值附近反复跳变 */
            new_state = (s_last_state == ST_ALARM || s_last_state == ST_WARN)
                        ? ST_WARN : ST_NORMAL;
        }
        else
        {
            new_state = ST_NORMAL;
        }
    }

    /* ---------- 3. 状态跳变时写入日志 ---------- */
    if (new_state != s_last_state)
    {
        if (new_state == ST_ALARM || new_state == ST_WARN)
        {
            src = state_to_src(new_state);
            if (src) log_write(src);
            s_last_log_ms = s_run_ms;
        }
        else if (new_state == ST_SENSOR_FAULT)
        {
            log_write(EE_SRC_SENSOR_FAIL);
        }

        s_last_state = new_state;
    }

    /* 报警持续期间定期补记，用于回顾浓度变化趋势 */
    if (new_state == ST_ALARM && (s_run_ms - s_last_log_ms) >= 600000UL)
    {
        log_write(EE_SRC_CO_ALARM);
        s_last_log_ms = s_run_ms;
    }

    /* ---------- 4. 低电量检测（仅在状态跳变时记录） ---------- */
    if (g_batt.low && !s_batt_low_last)
    {
        log_write(EE_SRC_BATT_LOW);
    }
    s_batt_low_last = g_batt.low;

    g_status.state = new_state;

    /* ---------- 5. LED 状态指示 ----------
     * 板载 LED 为低电平点亮。
     * 正常常亮 / 预热慢闪 / 预警中速闪 / 报警急促闪 / 故障慢闪 */
    if (new_state == ST_NORMAL)
    {
        LED0 = 0;
        s_blink_on = 0;
    }
    else
    {
        u16 period;

        if (new_state == ST_WARMUP)     period = 1000;
        else if (new_state == ST_WARN)  period = 400;
        else if (new_state == ST_ALARM) period = 200;
        else                            period = 600;

        s_blink_ms += 100;
        if (s_blink_ms >= period)
        {
            s_blink_ms = 0;
            s_blink_on = !s_blink_on;
        }

        LED0 = s_blink_on ? 0 : 1;
    }
}
