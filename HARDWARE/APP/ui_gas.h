/**
 * @file    ui_gas.h
 * @brief   LVGL 人机界面层
 * @details 本模块仅负责界面构建与状态渲染，不参与任何数据采集与业务判定。
 *          展示内容全部来源于 gas_app.h 中定义的 g_status 结构体。
 */

#ifndef __UI_GAS_H
#define __UI_GAS_H

/**
 * @brief  构建界面
 * @note   需在 lv_init() 与 lv_port_disp_init() 之后调用
 */
void UI_Gas_Init(void);

/**
 * @brief  刷新界面显示
 * @note   建议调用周期 200ms。函数内部对状态变化做了检测，
 *         仅在状态跳变时才更新配色与动画，避免无谓的重复渲染
 */
void UI_Gas_Refresh(void);

#endif
