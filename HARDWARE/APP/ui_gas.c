/**
 * @file    ui_gas.c
 * @brief   LVGL 人机界面实现（深色仪表盘风格）
 * @details 视觉设计要素：
 *            - 底色采用近黑色 (#0A0F14)，较纯黑更柔和，适合长时间观察；
 *            - 主色为青色霓虹 (#00E5FF)，用于强调读数、曲线与边框光晕；
 *            - 状态色统一由 state_color() 映射：
 *              正常 青 / 预热 蓝 / 预警 橙 / 报警 红 / 故障 灰。
 *
 *          320 x 240 版面布局：
 *
 *              y=0    ┌──────────────────────────────────────────┐
 *                     │ [状态胶囊]     CO MONITOR    [电池] 100% │  状态栏 28px
 *              y=28   ├──────────────────────────────────────────┤
 *                     │  ┌──── 读数卡：霓虹边框 + 光晕 ──────┐   │
 *                     │  │ CO                    ┌ TREND ──┐ │   │
 *                     │  │ 12.3  ppm             └─────────┘ │   │  94px
 *                     │  │ air looks clean                   │   │
 *                     │  └───────────────────────────────────┘   │
 *                     │  ───────────── 分隔线 ─────────────       │
 *                     │  TEMP 28 C     HUMI 44 %RH               │
 *                     │  Vout 0.67V    UP 00:09    LOG 11        │
 *              y=200  ├──────────────────────────────────────────┤
 *                     │    [  MAIN  ]   [  LOG  ]   [  CAL  ]    │  按钮栏 40px
 *              y=240  └──────────────────────────────────────────┘
 *
 * @note    界面文字当前为英文。因 lv_conf.h 中 LV_FONT_SIMSUN_16_CJK = 0，
 *          默认字体不含中文字形；启用 CJK 字库需占用较多 Flash 空间，
 *          而本项目 Flash 余量有限。切换方法见 docs/DEVELOPER_GUIDE.md。
 */

#include "ui_gas.h"
#include "gas_app.h"
#include "gas_mq7.h"
#include "batt.h"
#include "eeprom.h"
#include "lvgl.h"
#include <stdio.h>

/* ==========================================================================
 * 配色定义
 * ========================================================================== */

#define C_BG         lv_color_hex(0x0A0F14)     /**< 屏幕底色（近黑） */
#define C_PANEL      lv_color_hex(0x121A22)     /**< 面板底色 */
#define C_NEON       lv_color_hex(0x00E5FF)     /**< 主色（青色霓虹） */
#define C_TEXT       lv_color_hex(0xD8E4EE)     /**< 正文颜色 */
#define C_DIM        lv_color_hex(0x5A6B7A)     /**< 次要信息颜色 */

/* ==========================================================================
 * 界面对象句柄
 * ========================================================================== */

static lv_obj_t *s_pill;            /**< 状态胶囊容器 */
static lv_obj_t *s_pill_label;      /**< 状态文字 */
static lv_obj_t *s_co_value;        /**< CO 浓度大字号显示 */
static lv_obj_t *s_co_unit;         /**< 浓度单位 / 预热剩余秒数 */
static lv_obj_t *s_advice;          /**< 操作建议提示 */
static lv_obj_t *s_card;            /**< 读数卡片，报警时呈现呼吸光效 */
static lv_obj_t *s_chart;           /**< 浓度趋势曲线 */
static lv_chart_series_t *s_series; /**< 曲线数据序列 */
static lv_obj_t *s_th;              /**< 温湿度显示 */
static lv_obj_t *s_info;            /**< 电压、运行时长、日志条数 */
static lv_obj_t *s_batt_pct;        /**< 电量百分比文字 */
static lv_obj_t *s_batt_canvas;     /**< 电池图标画布 */
static lv_obj_t *s_page_main;       /**< 主页面容器 */
static lv_obj_t *s_page_log;        /**< 日志页面容器 */
static lv_obj_t *s_log_label;       /**< 日志正文 */

/* ==========================================================================
 * 趋势曲线数据
 * ========================================================================== */

#define TREND_POINTS   40                   /**< 曲线采样点数 */
#define TREND_MAX_PPM  200                  /**< 纵轴满量程，超出部分削平 */

static lv_coord_t s_trend[TREND_POINTS];    /**< 采样值环形缓冲 */
static u8 s_trend_head = 0;                 /**< 环形缓冲写指针 */

/* ==========================================================================
 * 静态资源
 * ========================================================================== */

#define BATT_W   32
#define BATT_H   15

/** 电池图标画布缓冲区（16 位色，尺寸由 LVGL 宏计算） */
static lv_color_t s_batt_buf[LV_CANVAS_BUF_SIZE_TRUE_COLOR(BATT_W, BATT_H)];

/** 日志正文缓冲区，静态分配以避免占用 LVGL 堆内存 */
static char s_log_text[6 * 44 + 8];

/* ==========================================================================
 * 内部函数
 * ========================================================================== */

/**
 * @brief  获取设备状态对应的主题色
 * @param  st  设备状态
 * @retval 主题色
 */
static lv_color_t state_color(gas_state_t st)
{
    switch (st)
    {
        case ST_NORMAL:   return C_NEON;
        case ST_WARMUP:   return lv_color_hex(0x448AFF);
        case ST_WARN:     return lv_color_hex(0xFFB300);
        case ST_ALARM:    return lv_color_hex(0xFF1744);
        default:          return lv_color_hex(0x78909C);
    }
}

/**
 * @brief  获取设备状态对应的操作建议文本
 * @param  st  设备状态
 * @retval 建议文本
 */
static const char *state_advice(gas_state_t st)
{
    switch (st)
    {
        case ST_WARMUP:       return "sensor heating, please wait";
        case ST_NORMAL:       return "air looks clean";
        case ST_WARN:         return "ventilate the room";
        case ST_ALARM:        return "OPEN WINDOW / LEAVE NOW";
        case ST_SENSOR_FAULT: return "check MQ-7 AO wiring";
        default:              return "";
    }
}

/**
 * @brief  报警呼吸动效回调
 * @param  obj  动画作用对象（读数卡片）
 * @param  v    当前动画值，范围 90 ~ 255
 *
 * @details 通过周期性改变边框透明度与光晕宽度，
 *          使读数卡片呈现呼吸效果，强化报警时的视觉警示。
 */
static void glow_anim_cb(void *obj, int32_t v)
{
    lv_obj_t *o = (lv_obj_t *)obj;

    lv_obj_set_style_border_opa(o, (lv_opa_t)v, 0);
    lv_obj_set_style_shadow_width(o, v / 18, 0);
    lv_obj_set_style_shadow_opa(o, (lv_opa_t)(v / 2), 0);
}

/**
 * @brief  开启动或关闭读数卡片的呼吸动效
 * @param  breathing  非 0 表示开启呼吸效果，0 表示关闭
 * @note   内部维护运行状态，重复调用不会创建多个动画实例
 */
static void set_glow(u8 breathing)
{
    static lv_anim_t a;
    static u8 running = 0;

    if (breathing)
    {
        if (running) return;
        running = 1;

        lv_anim_init(&a);
        lv_anim_set_var(&a, s_card);
        lv_anim_set_exec_cb(&a, glow_anim_cb);
        lv_anim_set_values(&a, 90, 255);
        lv_anim_set_time(&a, 500);
        lv_anim_set_playback_time(&a, 500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&a);
    }
    else
    {
        if (!running) return;
        running = 0;

        lv_anim_del(s_card, glow_anim_cb);

        /* 恢复静态微光外观 */
        lv_obj_set_style_border_opa(s_card, LV_OPA_COVER, 0);
        lv_obj_set_style_shadow_width(s_card, 8, 0);
        lv_obj_set_style_shadow_opa(s_card, LV_OPA_30, 0);
    }
}

/**
 * @brief  更新读数卡片的边框与光晕颜色
 * @param  col  主题色
 */
static void apply_card_color(lv_color_t col)
{
    lv_obj_set_style_border_color(s_card, col, 0);
    lv_obj_set_style_shadow_color(s_card, col, 0);
}

/**
 * @brief  绘制电池图标
 * @param  percent  剩余电量百分比
 * @param  low      低电量标志，置位时强制显示为红色
 *
 * @details 使用 canvas 动态绘制而非位图资源，可节省 Flash 空间，
 *          并支持根据电量实时改变填充颜色。
 */
static void draw_battery(u8 percent, u8 low)
{
    lv_draw_rect_dsc_t r;
    lv_color_t col;
    lv_coord_t fill_w;

    /* 电量对应的填充色 */
    if (percent > 50)      col = C_NEON;
    else if (percent > 20) col = lv_color_hex(0xFFB300);
    else                   col = lv_color_hex(0xFF1744);
    if (low)               col = lv_color_hex(0xFF1744);

    /* 填充背景，避免透明区域残留上一帧内容 */
    lv_canvas_fill_bg(s_batt_canvas, C_BG, LV_OPA_COVER);

    /* 电池外壳（仅描边） */
    lv_draw_rect_dsc_init(&r);
    r.bg_opa       = LV_OPA_TRANSP;
    r.border_color = C_TEXT;
    r.border_width = 2;
    r.radius       = 3;
    lv_canvas_draw_rect(s_batt_canvas, 0, 0, BATT_W - 6, BATT_H, &r);

    /* 正极触点 */
    lv_draw_rect_dsc_init(&r);
    r.bg_opa   = LV_OPA_COVER;
    r.bg_color = C_TEXT;
    r.radius   = 1;
    lv_canvas_draw_rect(s_batt_canvas, BATT_W - 5, 4, 3, BATT_H - 8, &r);

    /* 电量填充条 */
    fill_w = (lv_coord_t)(((BATT_W - 10) * percent) / 100);
    if (fill_w > BATT_W - 10) fill_w = BATT_W - 10;

    if (fill_w > 1)
    {
        lv_draw_rect_dsc_init(&r);
        r.bg_opa   = LV_OPA_COVER;
        r.bg_color = col;
        r.radius   = 2;
        lv_canvas_draw_rect(s_batt_canvas, 3, 3, fill_w, BATT_H - 6, &r);
    }
}

/**
 * @brief  清空趋势曲线数据
 */
static void trend_reset(void)
{
    u8 i;

    for (i = 0; i < TREND_POINTS; i++) s_trend[i] = 0;

    lv_chart_set_all_value(s_chart, s_series, 0);
    s_trend_head = 0;
}

/**
 * @brief  向趋势曲线追加一个采样点
 * @param  ppm  当前 CO 浓度
 *
 * @details 采样值存于环形缓冲，每次刷新时按"由旧到新"的顺序
 *          整体重新写入图表。
 *
 * @note    未采用 lv_chart_set_next_value 的 FIFO 滚动模式，原因是
 *          环形缓冲回绕后元素顺序即被打乱。40 个点的重灌开销很小
 *          （每 200ms 一次），换取实现上的清晰可靠。
 */
static void trend_push(float ppm)
{
    lv_coord_t ordered[TREND_POINTS];
    lv_coord_t v = (lv_coord_t)(ppm + 0.5f);
    u8 i;

    if (v > TREND_MAX_PPM) v = TREND_MAX_PPM;

    s_trend[s_trend_head] = v;
    s_trend_head = (u8)((s_trend_head + 1) % TREND_POINTS);

    for (i = 0; i < TREND_POINTS; i++)
    {
        ordered[i] = s_trend[(s_trend_head + i) % TREND_POINTS];
    }

    lv_chart_set_all_value(s_chart, s_series, 0);
    for (i = 0; i < TREND_POINTS; i++)
    {
        lv_chart_set_next_value(s_chart, s_series, ordered[i]);
    }

    lv_chart_refresh(s_chart);
}

/* ==========================================================================
 * 事件回调
 * ========================================================================== */

/**
 * @brief  页面切换回调
 * @param  e  LVGL 事件对象，user_data 携带目标页面编号（0 = 主页面，1 = 日志页）
 */
static void tab_event_cb(lv_event_t *e)
{
    uint32_t page = (uint32_t)(lv_obj_get_user_data(lv_event_get_target(e)));

    if (page == 0)
    {
        lv_obj_clear_flag(s_page_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_page_log, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(s_page_main, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_page_log, LV_OBJ_FLAG_HIDDEN);

        /* 切换至日志页时立即刷新一次内容 */
        UI_Gas_Refresh();
    }
}

/**
 * @brief  CAL 键回调：触发洁净空气标定
 */
static void cal_event_cb(lv_event_t *e)
{
    GAS_APP_Calibrate();
    lv_label_set_text(s_advice, "R0 calibrated & saved");
}

/**
 * @brief  创建统一样式的功能按键
 * @param  parent     父对象
 * @param  txt        按键文本
 * @param  x          水平位置
 * @param  w          按键宽度
 * @param  cb         点击事件回调
 * @param  user_data  随事件传递的用户数据
 * @retval 创建的按键对象
 */
static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, lv_coord_t x, lv_coord_t w,
                          lv_event_cb_t cb, uint32_t user_data)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_t *lbl;

    lv_obj_set_pos(btn, x, 6);
    lv_obj_set_size(btn, w, 28);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_bg_color(btn, C_PANEL, 0);
    lv_obj_set_style_bg_grad_color(btn, lv_color_hex(0x1B2733), 0);
    lv_obj_set_style_bg_grad_dir(btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x2E4256), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);

    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, C_TEXT, 0);
    lv_obj_set_style_text_letter_space(lbl, 2, 0);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_user_data(btn, (void *)user_data);

    return btn;
}

/* ==========================================================================
 * 界面构建
 * ========================================================================== */

void UI_Gas_Init(void)
{
    lv_obj_t *bar, *btnbar, *title, *sep, *lbl;

    /* ---------------- 屏幕底色 ---------------- */
    lv_obj_set_style_bg_color(lv_scr_act(), C_BG, 0);
    lv_obj_set_style_bg_grad_color(lv_scr_act(), lv_color_hex(0x0E1620), 0);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_VER, 0);

    /* ---------------- 顶部状态栏 ---------------- */
    bar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(bar, 320, 28);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(0x1E2A36), 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0D141B), 0);
    lv_obj_set_style_pad_top(bar, 0, 0);
    lv_obj_set_style_pad_bottom(bar, 0, 0);
    lv_obj_set_style_pad_left(bar, 0, 0);
    lv_obj_set_style_pad_right(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* 状态胶囊：边框与文字颜色随设备状态变化 */
    s_pill = lv_obj_create(bar);
    lv_obj_set_size(s_pill, 92, 18);
    lv_obj_align(s_pill, LV_ALIGN_LEFT_MID, 5, 0);
    lv_obj_set_style_radius(s_pill, 9, 0);
    lv_obj_set_style_border_width(s_pill, 1, 0);
    lv_obj_set_style_bg_color(s_pill, lv_color_hex(0x101820), 0);
    lv_obj_set_style_pad_top(s_pill, 0, 0);
    lv_obj_set_style_pad_bottom(s_pill, 0, 0);
    lv_obj_set_style_pad_left(s_pill, 0, 0);
    lv_obj_set_style_pad_right(s_pill, 0, 0);
    lv_obj_clear_flag(s_pill, LV_OBJ_FLAG_SCROLLABLE);

    s_pill_label = lv_label_create(s_pill);
    lv_label_set_text(s_pill_label, "WARM UP");
    lv_obj_set_style_text_letter_space(s_pill_label, 1, 0);
    lv_obj_center(s_pill_label);

    /* 标题 */
    title = lv_label_create(bar);
    lv_label_set_text(title, "CO MONITOR");
    lv_obj_set_style_text_color(title, C_DIM, 0);
    lv_obj_set_style_text_letter_space(title, 2, 0);
    lv_obj_align(title, LV_ALIGN_LEFT_MID, 104, 0);

    /* 电池图标与电量文字 */
    s_batt_canvas = lv_canvas_create(bar);
    lv_canvas_set_buffer(s_batt_canvas, s_batt_buf, BATT_W, BATT_H, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(s_batt_canvas, LV_ALIGN_RIGHT_MID, -46, 0);

    s_batt_pct = lv_label_create(bar);
    lv_label_set_text(s_batt_pct, "--%");
    lv_obj_set_style_text_color(s_batt_pct, C_TEXT, 0);
    lv_obj_align(s_batt_pct, LV_ALIGN_RIGHT_MID, -4, 0);

    /* ---------------- 主页面 ---------------- */
    s_page_main = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_page_main, 320, 172);
    lv_obj_set_pos(s_page_main, 0, 28);
    lv_obj_set_style_radius(s_page_main, 0, 0);
    lv_obj_set_style_border_width(s_page_main, 0, 0);
    lv_obj_set_style_bg_opa(s_page_main, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(s_page_main, 5, 0);
    lv_obj_set_style_pad_bottom(s_page_main, 5, 0);
    lv_obj_set_style_pad_left(s_page_main, 5, 0);
    lv_obj_set_style_pad_right(s_page_main, 5, 0);
    lv_obj_clear_flag(s_page_main, LV_OBJ_FLAG_SCROLLABLE);

    /* --- 读数卡片 --- */
    s_card = lv_obj_create(s_page_main);
    lv_obj_set_size(s_card, 310, 94);
    lv_obj_set_pos(s_card, 0, 0);
    lv_obj_set_style_radius(s_card, 10, 0);
    lv_obj_set_style_bg_color(s_card, C_PANEL, 0);
    lv_obj_set_style_bg_grad_color(s_card, lv_color_hex(0x0C1319), 0);
    lv_obj_set_style_bg_grad_dir(s_card, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_border_width(s_card, 2, 0);
    lv_obj_set_style_border_color(s_card, C_NEON, 0);
    lv_obj_set_style_shadow_width(s_card, 8, 0);
    lv_obj_set_style_shadow_color(s_card, C_NEON, 0);
    lv_obj_set_style_shadow_opa(s_card, LV_OPA_30, 0);
    lv_obj_set_style_pad_top(s_card, 4, 0);
    lv_obj_set_style_pad_bottom(s_card, 4, 0);
    lv_obj_set_style_pad_left(s_card, 4, 0);
    lv_obj_set_style_pad_right(s_card, 4, 0);
    lv_obj_clear_flag(s_card, LV_OBJ_FLAG_SCROLLABLE);

    /* 气体名称标签 */
    lbl = lv_label_create(s_card);
    lv_label_set_text(lbl, "CO");
    lv_obj_set_style_text_color(lbl, C_DIM, 0);
    lv_obj_set_style_text_letter_space(lbl, 3, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 2, 0);

    /* 浓度数值 */
    s_co_value = lv_label_create(s_card);
    lv_label_set_text(s_co_value, "0.0");
    lv_obj_set_style_text_font(s_co_value, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_co_value, C_NEON, 0);
    lv_obj_align(s_co_value, LV_ALIGN_LEFT_MID, 2, 4);

    /* 浓度单位 */
    s_co_unit = lv_label_create(s_card);
    lv_label_set_text(s_co_unit, "ppm");
    lv_obj_set_style_text_color(s_co_unit, C_DIM, 0);
    lv_obj_align(s_co_unit, LV_ALIGN_LEFT_MID, 104, 12);

    /* 操作建议 */
    s_advice = lv_label_create(s_card);
    lv_label_set_text(s_advice, "");
    lv_obj_set_style_text_color(s_advice, C_TEXT, 0);
    lv_obj_align(s_advice, LV_ALIGN_BOTTOM_LEFT, 2, 0);

    /* --- 趋势曲线 --- */
    lbl = lv_label_create(s_card);
    lv_label_set_text(lbl, "TREND");
    lv_obj_set_style_text_color(lbl, C_DIM, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, -2, 0);

    s_chart = lv_chart_create(s_card);
    lv_obj_set_size(s_chart, 138, 62);
    lv_obj_align(s_chart, LV_ALIGN_RIGHT_MID, -2, 6);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, TREND_MAX_PPM);
    lv_chart_set_point_count(s_chart, TREND_POINTS);
    lv_chart_set_div_line_count(s_chart, 2, 4);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_top(s_chart, 0, 0);
    lv_obj_set_style_pad_bottom(s_chart, 0, 0);
    lv_obj_set_style_pad_left(s_chart, 0, 0);
    lv_obj_set_style_pad_right(s_chart, 0, 0);
    lv_obj_set_style_line_color(s_chart, lv_color_hex(0x1E2E3C), 0);

    s_series = lv_chart_add_series(s_chart, C_NEON, LV_CHART_AXIS_PRIMARY_Y);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, LV_PART_ITEMS);
    trend_reset();

    /* --- 分隔线 --- */
    sep = lv_obj_create(s_page_main);
    lv_obj_set_size(sep, 308, 1);
    lv_obj_set_pos(sep, 0, 98);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_bg_color(sep, lv_color_hex(0x1E2A36), 0);

    /* --- 温湿度 --- */
    s_th = lv_label_create(s_page_main);
    lv_label_set_text(s_th, "TEMP -- C     HUMI -- %RH");
    lv_obj_set_style_text_color(s_th, C_TEXT, 0);
    lv_obj_set_pos(s_th, 2, 104);

    /* --- 电压 / 运行时长 / 日志条数 --- */
    s_info = lv_label_create(s_page_main);
    lv_label_set_text(s_info, "Vout --V");
    lv_obj_set_style_text_color(s_info, C_DIM, 0);
    lv_obj_set_pos(s_info, 2, 126);

    /* ---------------- 日志页面 ---------------- */
    s_page_log = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_page_log, 320, 172);
    lv_obj_set_pos(s_page_log, 0, 28);
    lv_obj_set_style_radius(s_page_log, 0, 0);
    lv_obj_set_style_border_width(s_page_log, 0, 0);
    lv_obj_set_style_bg_opa(s_page_log, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_top(s_page_log, 6, 0);
    lv_obj_set_style_pad_bottom(s_page_log, 6, 0);
    lv_obj_set_style_pad_left(s_page_log, 6, 0);
    lv_obj_set_style_pad_right(s_page_log, 6, 0);
    lv_obj_clear_flag(s_page_log, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_page_log, LV_OBJ_FLAG_HIDDEN);

    s_log_label = lv_label_create(s_page_log);
    lv_label_set_text(s_log_label, "no record");
    lv_obj_set_style_text_color(s_log_label, C_TEXT, 0);
    lv_obj_set_style_text_font(s_log_label, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_line_space(s_log_label, 4, 0);
    lv_obj_set_pos(s_log_label, 0, 0);

    /* ---------------- 底部按键栏 ---------------- */
    btnbar = lv_obj_create(lv_scr_act());
    lv_obj_set_size(btnbar, 320, 40);
    lv_obj_set_pos(btnbar, 0, 200);
    lv_obj_set_style_radius(btnbar, 0, 0);
    lv_obj_set_style_border_width(btnbar, 1, 0);
    lv_obj_set_style_border_side(btnbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(btnbar, lv_color_hex(0x1E2A36), 0);
    lv_obj_set_style_bg_color(btnbar, lv_color_hex(0x0D141B), 0);
    lv_obj_set_style_pad_top(btnbar, 0, 0);
    lv_obj_set_style_pad_bottom(btnbar, 0, 0);
    lv_obj_set_style_pad_left(btnbar, 0, 0);
    lv_obj_set_style_pad_right(btnbar, 0, 0);
    lv_obj_clear_flag(btnbar, LV_OBJ_FLAG_SCROLLABLE);

    make_btn(btnbar, "MAIN", 10,  92, tab_event_cb, 0);
    make_btn(btnbar, "LOG",  114, 92, tab_event_cb, 1);
    make_btn(btnbar, "CAL",  218, 92, cal_event_cb, 0);
}

/* ==========================================================================
 * 界面刷新
 * ========================================================================== */

/**
 * @brief  刷新日志页面内容
 * @details 从 EEPROM 读取最近 6 条记录并格式化为等宽文本。
 *
 * @note    单条记录读取需通过 I2C 访问 EEPROM，耗时较长，
 *          因此仅在日志页面可见时执行刷新。
 */
static void refresh_log_page(void)
{
    ee_log_t rec;
    u16 n = EEPROM_LogCount();
    u16 i, shown;
    int len = 0;

    s_log_text[0] = '\0';

    if (n == 0)
    {
        lv_label_set_text(s_log_label,
                          "  no record yet\n\n"
                          "  CAL = calibrate R0\n"
                          "  (do it in clean air,\n"
                          "   after 3 min warm-up)");
        return;
    }

    len += snprintf(s_log_text + len, sizeof(s_log_text) - len,
                    "TIME    CO    T   EVENT\n");

    shown = (n > 6) ? 6 : n;
    for (i = 0; i < shown; i++)
    {
        if (!EEPROM_LogGet(i, &rec)) break;

        len += snprintf(s_log_text + len, sizeof(s_log_text) - len,
                        "%3d:%02d %5d  %3d  %s\n",
                        (int)(rec.time_min / 60), (int)(rec.time_min % 60),
                        (int)rec.co_ppm, (int)rec.temp,
                        GAS_APP_LogSrcName(rec.src));
    }

    if (n > 6)
    {
        len += snprintf(s_log_text + len, sizeof(s_log_text) - len,
                        "  ---- total %d records ----", (int)n);
    }

    lv_label_set_text(s_log_label, s_log_text);
}

void UI_Gas_Refresh(void)
{
    char buf[56];
    lv_color_t col = state_color(g_status.state);

    /** 上一次的设备状态，用于识别状态跳变 */
    static gas_state_t last_state = (gas_state_t)0xFF;

    /* ---------- 状态跳变时才执行的更新 ----------
     * 将配色与动画的更新限制在状态变化时，可减少重复渲染开销，
     * 同时避免动画被反复重启 */
    if (g_status.state != last_state)
    {
        last_state = g_status.state;

        apply_card_color(col);
        lv_obj_set_style_text_color(s_pill_label, col, 0);
        lv_obj_set_style_border_color(s_pill, col, 0);
        lv_chart_set_series_color(s_chart, s_series, col);

        set_glow((g_status.state == ST_ALARM) ? 1 : 0);
    }

    /* ---------- 状态胶囊 ---------- */
    lv_label_set_text(s_pill_label, GAS_APP_StateName(g_status.state));

    /* ---------- CO 浓度 ---------- */
    if (g_status.state == ST_WARMUP)
    {
        /* 预热期间显示倒计时，便于判断还需等待多久 */
        snprintf(buf, sizeof(buf), "%d", (int)g_status.warmup_sec);
        lv_label_set_text(s_co_value, buf);
        lv_label_set_text(s_co_unit, "s heat");
    }
    else
    {
        snprintf(buf, sizeof(buf), "%.1f", (double)g_status.co_ppm);
        lv_label_set_text(s_co_value, buf);
        lv_label_set_text(s_co_unit, "ppm");
    }

    lv_obj_set_style_text_color(s_co_value, col, 0);
    lv_label_set_text(s_advice, state_advice(g_status.state));

    /* ---------- 趋势曲线 ----------
     * 预热期间读数不稳定，不纳入曲线 */
    if (g_status.state != ST_WARMUP)
    {
        trend_push(g_status.co_ppm);
    }

    /* ---------- 温湿度 ---------- */
    if (g_status.dht_ok)
    {
        snprintf(buf, sizeof(buf), "TEMP %d C     HUMI %d %%RH",
                 (int)g_status.temp, (int)g_status.humi);
    }
    else
    {
        snprintf(buf, sizeof(buf), "TEMP -- C     HUMI -- %%RH");
    }
    lv_label_set_text(s_th, buf);

    /* ---------- 电压 / 运行时长 / 日志条数 ---------- */
    snprintf(buf, sizeof(buf), "Vout %u.%02uV     UP %02d:%02d    LOG %d",
             (unsigned)(g_mq7.v_out_mv / 1000),
             (unsigned)((g_mq7.v_out_mv % 1000) / 10),
             (int)(g_status.run_min / 60), (int)(g_status.run_min % 60),
             (int)g_status.log_total);
    lv_label_set_text(s_info, buf);

    /* ---------- 电池状态 ---------- */
    snprintf(buf, sizeof(buf), "%d%%", (int)g_status.batt_pct);
    lv_label_set_text(s_batt_pct, buf);
    lv_obj_set_style_text_color(s_batt_pct,
                                g_status.batt_low ? lv_color_hex(0xFF1744) : C_TEXT, 0);
    draw_battery(g_status.batt_pct, g_status.batt_low);

    /* ---------- 日志页面内容（仅可见时刷新） ---------- */
    if (!lv_obj_has_flag(s_page_log, LV_OBJ_FLAG_HIDDEN))
    {
        refresh_log_page();
    }
}
