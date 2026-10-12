# 开发文档

面向需要**阅读、修改或移植**本项目的开发者。使用说明请见 [使用手册](USER_GUIDE.md)。

---

## 目录

- [1. 技术概览](#1-技术概览)
- [2. 开发环境](#2-开发环境)
- [3. 构建系统](#3-构建系统)
- [4. 软件架构](#4-软件架构)
- [5. 模块设计](#5-模块设计)
- [6. 核心算法](#6-核心算法)
- [7. 存储设计](#7-存储设计)
- [8. 界面实现](#8-界面实现)
- [9. 资源占用](#9-资源占用)
- [10. 已解决的问题记录](#10-已解决的问题记录)
- [11. 调试方法](#11-调试方法)
- [12. 移植指南](#12-移植指南)
- [13. 编码规范](#13-编码规范)

---

## 1. 技术概览

| 项目 | 内容 |
|---|---|
| 目标平台 | STM32F103RCT6（Cortex-M3 @72MHz，256KB Flash / 48KB RAM） |
| 开发板 | 正点原子 MiniSTM32 V3 |
| 图形库 | LVGL 8.2.0 |
| 外设库 | STM32 标准外设库 V3.5 |
| 编译器 | Keil ARMCC v5.06 update 5 |
| 构建系统 | GNU Make 4.4.1 |
| 烧录调试 | pyOCD 0.45.1 + ST-Link，Cortex-Debug 1.12.1 |

### 分层结构

```
应用层     HARDWARE/APP/gas_app.c（业务逻辑）  ui_gas.c（人机界面）
              ↓ 通过 gas_status_t 结构体单向通信
驱动层     HARDWARE/GAS、DHT11、BATT、EEPROM
              ↓
硬件抽象   HARDWARE/LCD、TOUCH、IIC、24CXX、KEY、LED、TIMER
              ↓
库与内核   LVGL 8.2 / STM32 标准外设库 / CMSIS
```

**关键设计约束**：界面层不感知传感器如何采样，逻辑层不感知界面如何渲染。两者仅通过 `gas_app.h` 中的 `gas_status_t` 全局状态结构体交互。这样更换显示屏或调整界面布局时，业务逻辑无需改动。

---

## 2. 开发环境

### 工具链

| 工具 | 路径 | 用途 |
|---|---|---|
| ARMCC | `D:\apps\keil5MDK\ARM\ARMCC\bin` | 编译、链接、格式转换 |
| GNU Make | `<仓库上级>\.tools\make\make.exe` | 构建 |
| pyOCD | `%USERPROFILE%\.pyocd-venv\Scripts\pyocd.exe` | 烧录、调试服务 |
| Arm GNU Toolchain | `C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin` | GDB 调试、符号分析 |

> **为什么编译用 ARMCC 而调试用 GNU 工具链**
> Keil 的 ARMCC 与 ARMCLANG 均不提供 GDB；而 Cortex-Debug 扩展依赖 `arm-none-eabi-gdb` 及其配套的 `objdump` / `readelf` / `nm`。两者职责分离，互不干扰。
>
> 选择 ARMCC 而非 GCC 的原因：底层驱动（`delay.c` / `lcd.c`）使用了 ARMCC 方言的内联汇编与位带操作，改用 GCC 需要重写分散加载与 CMSIS 适配，成本远大于收益。

### 依赖安装

```powershell
# Arm GNU Toolchain（调试必需）
winget install --id Arm.ArmGnuToolchain --source winget

# pyOCD
python -m venv $env:USERPROFILE\.pyocd-venv
& $env:USERPROFILE\.pyocd-venv\Scripts\pip.exe install pyocd
```

### VS Code 扩展

| 扩展 | ID | 用途 |
|---|---|---|
| C/C++ | `ms-vscode.cpptools` | 代码补全与跳转 |
| Cortex-Debug | `marus25.cortex-debug` | 在线调试 |
| Serial Monitor | `ms-vscode.vscode-serial-monitor` | 查看串口输出 |

---

## 3. 构建系统

### 使用方式

```bat
build.bat              :: 增量编译，产物 build\GasDetector.hex / .axf
build.bat clean        :: 清理 build\
build.bat DEBUG=1      :: 带调试信息编译
```

### 三条硬性规则

| 规则 | 原因 |
|---|---|
| **必须通过 `build.bat` 调用，不要直接执行 `make`** | `build.bat` 会把 `TMP`/`TEMP` 指向工程内的 `.tmp\`。否则 ARMCC 编译含内联汇编的源码时会随机报 `#564: cannot open embedded assembler output file` |
| **不要给 make 添加 `-j`** | ARMCC 的内联汇编使用固定临时文件名，并行编译会互相覆盖，同样触发 `#564` |
| **必须使用 `OBJ\GasDetector.sct` 分散加载文件** | 其中三行规则缺一不可，详见下节 |

### 分散加载文件

`OBJ/GasDetector.sct` 中的三行关键规则：

```
*.o (RESET, +First)        ; 中断向量表必须位于镜像最开头
*(InRoot$$Sections)        ; C 库启动代码与数据段初始化
.ANY (+RW +ZI)             ; 读写数据与零初始化数据放入 RAM
```

**若缺少任一行**，链接仍会"成功"，但镜像首地址会变成复位处理函数的机器码、栈指针会落在 Flash 区间，烧录后**无法启动**，且链接器不会报错。

### 镜像有效性校验

正确的镜像前 16 个字节应为中断向量表（栈顶指针 + 复位向量地址）：

```powershell
$l = [IO.File]::ReadAllLines("build\GasDetector.hex")
$l | Where-Object { $_ -match '^:..000000' } | Select-Object -First 1
```

实测输出：

```
:10000000705C00203D020008C3630008772C0008E4
```

解析结果：`SP = 0x20005C70`（位于 RAM 区间）、`PC = 0x0800023D`（位于 Flash 区间），判定为有效。

### 关于头文件依赖

**本 Makefile 只有 `%.o: %.c` 规则，没有头文件依赖规则。**

这意味着修改任何 `.h` 文件后执行 `build.bat`，make 会认为"无事可做"，改动**不会生效**——这一行为极易被误判为"代码没起作用"。

**修改 `.h` 后的正确流程**：

```bat
build.bat clean
build.bat
```

**验证方法**：例如修改 `lv_conf.h` 中的 `LV_MEM_SIZE` 后，用以下命令确认实际生效：

```powershell
arm-none-eabi-nm -n -S --defined-only build\GasDetector.axf | findstr work_mem_int
# 期望输出中的大小字段与 LV_MEM_SIZE 一致（如 0x6000 = 24KB）
```

---

## 4. 软件架构

### 调度模型

系统不使用 RTOS。任务调度借助 **LVGL 内置的定时器机制**实现：

```
TIM3 中断（1ms）
   └─> lv_tick_inc(1)                   提供时基

主循环 while(1)
   └─> lv_task_handler()                驱动 LVGL 定时器列表
        ├─> app_task_cb()   每 100ms    传感器采样、报警判定、日志写入
        └─> ui_task_cb()    每 200ms    界面刷新
```

**任务周期选择依据**：

| 任务 | 周期 | 依据 |
|---|---|---|
| 传感器采样 | 100ms | MQ-7 响应时间在秒级，100ms 足够；同时兼顾低通滤波的平滑效果 |
| 界面刷新 | 200ms | 人眼对 5Hz 刷新已无感知差异，提高频率只会增加 CPU 占用 |
| DHT11 读取 | 2s | 传感器手册要求采样间隔 ≥1s，留出余量 |

这样设计的收益：主循环无需维护多个 `if (now - last > period)` 判断，调整周期只需修改 `lv_timer_create` 的参数。

### 数据流

```
MQ7_Task()      ──┐
DHT11_Read()    ──┼──> gas_status_t g_status ──> UI_Gas_Refresh() ──> 屏幕
BATT_Task()     ──┤         （全局状态）              （单向读取）
EEPROM 状态     ──┘
```

`g_status` 是逻辑层与界面层之间唯一的耦合点，定义为 `gas_app.h` 中的 `gas_status_t`。

---

## 5. 模块设计

| 模块 | 文件 | 职责 |
|---|---|---|
| ADC 底座 | `GAS/adc1.c` | ADC1 统一初始化与读取。MQ-7 与电池检测共用 ADC1，集中管理避免重复初始化导致配置互相覆盖 |
| CO 传感器 | `GAS/gas_mq7.c` | MQ-7 采样、滤波、浓度换算、R0 标定、预热计时 |
| 温湿度 | `DHT11/dht11.c` | DHT11 单总线时序实现，含超时保护 |
| 电池 | `BATT/batt.c` | 分压电压采样、放电曲线查表、电量百分比 |
| 存储 | `EEPROM/eeprom.c` | 24C02 地址规划、R0 持久化、日志环形缓冲 |
| 业务逻辑 | `APP/gas_app.c` | 报警状态机、日志触发、LED 指示 |
| 人机界面 | `APP/ui_gas.c` | LVGL 控件构建与状态刷新 |
| 程序入口 | `USER/main.c` | 初始化序列、定时器注册、主循环 |

### 初始化顺序

顺序不可随意调整，关键约束如下：

```c
delay_init();          // SysTick，后续所有延时依赖它
NVIC_PriorityGroupConfig();
uart_init(9600);       // 串口，供 printf 调试
TIM3_Int_Init(1,7199); // LVGL 时基

LED_Init();
KEY_Init();            // ⚠️ 必须早于屏幕初始化，见第 10.1 节

lv_init();
lv_port_disp_init();   // 内部调用 LCD_Init()，会先执行触摸校准
lv_port_indev_init();

GAS_APP_Init();        // EEPROM / MQ-7 / DHT11 / 电池
UI_Gas_Init();         // 构建界面
```

---

## 6. 核心算法

### 6.1 MQ-7 浓度换算链

从 ADC 原始值到浓度共四步，分别对应 `gas_mq7.c` 中的四个函数：

```
ADC 值 ──①──> 电压(mV) ──②──> Rs ──③──> Rs/R0 ──④──> ppm
```

**① ADC → 电压**

```
Vout = adc × VREF / 4095
```

`VREF` 定义于 `adc1.h`（默认 3300mV）。用万用表实测 3.3V 轨电压后修改此值可提高整体精度。

**② 电压 → 传感器电阻 Rs**

MQ-7 模块内部为分压结构：

```
VCC(5V) ──[ Rs 传感器 ]──┬──[ RL ]── GND
                         │
                         └── AO（输出电压 Vout）
```

因此：

```
Vout = VCC × RL / (Rs + RL)

反解得：Rs = RL × (VCC − Vout) / Vout
```

参数 `MQ7_RL_OHM`（默认 10kΩ）与 `MQ7_VC_MV`（默认 5000mV）定义于 `gas_mq7.h`。

**③ Rs → Rs/R0**

`R0` 为洁净空气中的基准电阻，由标定流程确定：

```
R0 = Rs(洁净空气) / 3.8
```

其中 3.8 为 MQ-7 手册给出的洁净空气 `Rs/R0` 典型值。

**④ Rs/R0 → 浓度**

按手册双对数曲线的幂函数拟合：

```
ppm = A × (Rs/R0)^B
```

默认 `A = 27.5`、`B = −1.5`。若有标准气体，可通过两点实测反解 A、B 提高精度。

**低通滤波**

为抑制 ADC 抖动，采样值经过一阶低通滤波：

```
filtered = filtered + α × (raw − filtered)
```

`α` 默认 0.25（`MQ7_FILTER_ALPHA`）。α 越小越平滑但响应越慢。

### 6.2 报警状态机

```
                 预热完成
   ┌────────┐  ──────────>  ┌────────┐  ≥25ppm   ┌─────────┐
   │ WARMUP │               │ NORMAL │ ────────> │ WARNING │
   └────────┘               └────────┘ <18ppm    └─────────┘
                                 ↑                    │ ≥50ppm
                                 │ <18ppm             │ 持续3s
                                 │                    ↓
                                 │              ┌─────────┐
                                 └───────────── │  ALARM  │
                                      <40ppm    └─────────┘

   读数饱和 ──> SENSOR_FAULT
```

**三重防误报机制**：

| 机制 | 参数 | 作用 |
|---|---|---|
| 迟滞 | 报警 50 / 解除 40 | 浓度稳定在阈值附近时不会反复跳变 |
| 持续时间确认 | 连续超阈值 3000ms | 过滤火柴、烟雾等瞬时干扰 |
| 预热屏蔽 | 180000ms | 预热期间传感器输出不稳定，不参与判定 |

**传感器故障判定**：当 `Vout` 接近或超过供电电压（`MQ7_VC_MV − 20`）或 ADC 读数饱和（≥4090）时，判定为接线异常而非"浓度爆表"，状态置为 `SENSOR_FAULT` 并记录日志。

### 6.3 电池电量换算

锂电池放电曲线呈"平台型"——3.7V 至 3.5V 区间电压变化极小但电量下降明显，因此**不能使用电压线性换算**。

实现方式为查表 + 区间线性插值，数据表定义于 `batt.c`：

```c
static const u16 s_curve_mv[]  = {4200, 4100, 4000, 3900, 3800, 3750, 3700, 3650, 3600, 3550, 3500, 3400, 3300};
static const u8  s_curve_pct[] = { 100,   95,   88,   80,   70,   60,   50,   40,   30,   20,   10,    5,    0};
```

取值时在相邻两点间线性插值，得到百分比。查表值刻意取得偏保守——电量显示偏乐观比偏悲观更危险。

### 6.4 DHT11 单总线时序

一次完整通信的时序：

```
主机:  ────┐                    ┌──────────────
           │   拉低 ≥18ms       │ 拉高 20~40us 后转输入
           └────────────────────┘
从机:                         ┌──┐   ┌──┐  ... 40 位数据 ...
                              │80│   │  │
                              └──┘   └──┘
                             应答低  应答高
```

**数据位判定**：每位以 50µs 低电平起始，随后高电平持续时间决定数值：

| 高电平时长 | 数据位 |
|---|---|
| 26 ~ 28 µs | 0 |
| 70 µs | 1 |

**实现策略**：等待低电平结束后延时 **40µs** 再采样——该时刻恰好位于 28µs 与 70µs 之间，容错余量最大。

**健壮性设计**：所有等待循环均带超时计数（上限约 90µs），传感器未接时不会死锁。

**阻塞时间**：拉低 18ms + 40 位传输约 4ms ≈ 22ms。该阻塞发生在 LVGL 任务回调中，会使界面停顿约 22ms，实际观感可忽略。

---

## 7. 存储设计

### 24C02 地址规划

24C02 容量仅 **256 字节**，且需与触摸屏校准参数**共用同一芯片**，地址必须严格规划。地址定义集中在 `eeprom.h`：

```
0   ~ 39    保留（24C01 时代的历史地址，不可占用）
40  ~ 53    触摸屏校准参数（touch.c 的 SAVE_ADDR_BASE = 40，占 14 字节）
54  ~ 159   保留（防止未来修改 touch.c 造成冲突）
160 ~ 167   本项目配置区
             160     魔数 'G'（标识 EEPROM 已初始化）
             161~164 R0 标定值（4 字节，小端）
             165     日志环形缓冲写指针
             166     日志已存条数
167  ~ 183  保留
184 ~ 439   日志环形缓冲区（32 条 × 8 字节）
440 ~ 254   预留扩展
255         24C02 存在性标志 0x55（AT24CXX_Check 使用，禁止占用）
```

### 日志记录格式

每条记录固定 8 字节：

| 偏移 | 字段 | 长度 | 说明 |
|---|---|---|---|
| 0 | `time_min` | 4 | 开机运行分钟数 |
| 4 | `co_ppm` | 2 | CO 浓度（取整） |
| 6 | `temp` | 1 | 温度 |
| 7 | `src` | 1 | 事件来源码 |

**为什么使用环形缓冲**：EEPROM 擦写寿命约 100 万次。若每次都从固定地址写入，该地址会被反复擦写而提前失效。环形缓冲使 32 个槽位轮流写入，等效寿命提升 32 倍。

**读取语义**：`EEPROM_LogGet(index, ...)` 中 `index = 0` 返回**最近一条**记录，索引越大记录越旧。

### 写入频率控制

| 场景 | 写入策略 |
|---|---|
| 状态发生跳变 | 立即写入一条 |
| 报警持续中 | 每 10 分钟补记一条 |
| 低电量 | 仅在首次出现时写入，避免持续擦写 |
| DHT11 读取失败 | **不写入**（属正常偶发，不值得占用存储寿命） |

---

## 8. 界面实现

### 配色方案

定义于 `ui_gas.c` 文件头部：

| 宏 | 色值 | 用途 |
|---|---|---|
| `C_BG` | `0x0A0F14` | 屏幕底色（近黑，比纯黑柔和） |
| `C_PANEL` | `0x121A22` | 面板底色 |
| `C_NEON` | `0x00E5FF` | 主色（青色霓虹） |
| `C_TEXT` | `0xD8E4EE` | 正文 |
| `C_DIM` | `0x5A6B7A` | 次要信息 |

状态色由 `state_color()` 统一映射：正常青 / 预热蓝 / 预警橙 / 报警红 / 故障灰。

### 视觉设计要点

| 元素 | 实现方式 |
|---|---|
| 读数卡片霓虹边框 | `lv_obj_set_style_border_color` + `shadow` 产生光晕 |
| 报警呼吸效果 | `lv_anim` 动画驱动边框透明度与光晕宽度周期变化（`glow_anim_cb`） |
| 电池图标 | `lv_canvas` 动态绘制（`draw_battery`），随电量变色。相比位图方案节省 Flash |
| 趋势曲线 | `lv_chart`（LINE 类型），40 点环形缓冲，每 200ms 整体重灌一次 |

### 趋势曲线的数据处理

使用环形缓冲 + 整体重灌而非 `lv_chart_set_next_value` 的 FIFO 滚动模式，原因是环形缓冲一旦回绕，元素顺序即被打乱。40 点重灌的开销很小（每 200ms 一次），换来逻辑清晰。

预热期间的读数不进入曲线，避免开机时曲线出现无意义的冲高。

### 界面文字语言

当前界面为英文。原因：`lv_conf.h` 中 `LV_FONT_SIMSUN_16_CJK = 0`，启用 CJK 字库会显著增加 Flash 占用，而本项目 Flash 仅余约 11KB。

**改为中文界面的步骤**：

1. 精简 Flash 占用（见第 9 节）
2. 将 `lv_conf.h` 中的 `LV_FONT_SIMSUN_16_CJK` 置 1
3. 将 `ui_gas.c` 中的字符串替换为中文

---

## 9. 资源占用

### 实测数据

```
Total ROM Size : 249972 字节 (244.11 kB) / 256 kB   余 11.9 kB
Total RW  Size :  37968 字节 ( 37.08 kB) /  48 kB   余 10.9 kB
```

### RAM 分配明细

| 项目 | 大小 | 说明 |
|---|---|---|
| LVGL 内存池 `work_mem_int` | 24576 | `lv_conf.h` 的 `LV_MEM_SIZE` |
| LVGL 绘制缓冲 `buf_1` | 6400 | 320 × 10 行 × 2 字节 |
| 主栈 `Stack_Mem` | 2048 | 启动文件定义 |
| 电池画布 `s_batt_buf` | 1920 | 32 × 15 × 2 字节 + 对齐 |
| 日志文本缓冲 `s_log_text` | 272 | 静态分配，避免占用 LVGL 堆 |
| 其它 | 约 2.7K | 各驱动缓冲区、LVGL 内部结构 |

### 为什么 LVGL 内存池需要 24KB

原始配置为 10KB，在界面控件数量增加后耗尽，触发下述连锁反应：

```
界面控件增多 → 内存池耗尽 → lv_mem_alloc 返回 NULL
→ LV_ASSERT_MALLOC 展开为 while(1) → 界面卡死（背光亮但无显示）
```

**这是本项目最隐蔽的问题**：`lv_conf.h` 中 `LV_USE_ASSERT_MALLOC = 1` 时，内存分配失败不会报错或复位，而是**静默进入死循环**。排查方法见第 11.2 节。

### 精简 Flash 占用的方法

Flash 仅余约 11KB，添加功能前建议先精简：

| 方法 | 预计收益 | 操作 |
|---|---|---|
| 关闭未使用的 LVGL 字体 | 每个 3~30KB | `lv_conf.h` 中 `LV_FONT_MONTSERRAT_xx` 仅保留用到的（本项目使用 14 / 28 / unscii_8） |
| 关闭性能与内存监视器 | 数 KB | `LV_USE_PERF_MONITOR` / `LV_USE_MEM_MONITOR` 置 0（本项目已关闭） |
| 移除电容屏驱动 | 0（会被链接器丢弃） | `gt9147.c` 等虽参与编译，但在 `--split_sections` 下未被调用即不占 Flash，可保留备用 |
| 减少 DHT11 相关代码 | 少量 | 若不需要温湿度功能可移除 |

### 减少 LVGL 内存占用的方法

| 方法 | 收益 |
|---|---|
| 减小绘制缓冲行数 | 每减少 1 行省 640 字节（会略降刷新效率） |
| 简化界面控件数量 | 每个控件约 200~400 字节 |
| 改用 basic 主题 | 默认主题会创建较多样式对象 |
| 降低 `LV_MEM_SIZE` | 需实测确认稳定 |

---

## 10. 已解决的问题记录

以下问题均在开发过程中实际遇到并解决，修改代码时请勿回退这些处理。

### 10.1 PB3/PB4 的 JTAG 占用导致花屏

**现象**：屏幕点亮但显示花屏、颜色错乱。

**原因**：2.8 寸 TFT 使用 PB0~PB15 作为 16 位数据总线，其中 **PB3 与 PB4 默认功能为 JTAG 的 JTDO 与 NJTRST**。未释放时数据线缺两位，导致显示异常。

**解决**：在屏幕初始化**之前**调用 `KEY_Init()`，其内部执行：

```c
GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
```

该语句关闭 JTAG、保留 SWD 调试功能，将 PB3/PB4 释放为普通 GPIO。

**约束**：`USER/main.c` 中的调用顺序不可调整，`LED_Init()` / `KEY_Init()` 必须早于 `lv_port_disp_init()`。

### 10.2 LVGL 内存池耗尽导致界面卡死

**现象**：屏幕背光正常点亮，但界面完全不显示、程序无响应，串口也无输出。

**原因**：界面控件数量增加后，10KB 的 LVGL 内存池不足以完成控件分配。

**关键机制**：`lv_obj.c` 中

```c
obj->spec_attr = lv_mem_alloc(sizeof(_lv_obj_spec_attr_t));
LV_ASSERT_MALLOC(obj->spec_attr);     // LV_USE_ASSERT_MALLOC=1 时展开为 while(1)
```

分配失败时进入**无限循环**，而非报错或复位。

**解决**：`lv_conf.h` 中 `LV_MEM_SIZE` 由 10KB 提升至 **24KB**。

**排查手段**：使用 pyOCD 读取 PC 寄存器，若持续停留在 `lv_obj_allocate_spec_attr` 函数地址区间即可确认。详见第 11.2 节。

### 10.3 修改头文件后改动不生效

**现象**：修改 `lv_conf.h` 后重新编译，程序行为无任何变化。

**原因**：Makefile 只定义了 `%.o: %.c` 规则，**没有头文件依赖规则**，make 无法感知 `.h` 变更。

**解决**：修改任何 `.h` 后必须执行 `build.bat clean` 再编译。

**教训**：曾因此误判为"配置项无效"，实际是编译产物未更新。验证方式见第 3 节。

### 10.4 ST-Link 不提供串口功能

**现象**：按文档使用 ST-Link 后找不到串口，无法查看 `printf` 输出。

**原因**：ST-Link 仅提供 SWD/JTAG 调试接口，**不具备 USB 转串口功能**。串口输出依赖开发板板载的 CH340（USB ↔ USART1）。

**解决**：查看串口需为开发板自身的 USB 口连接数据线；若无串口，可改用第 11.2 节的寄存器检查法进行诊断。

### 10.5 传感器功耗导致屏幕熄灭

**现象**：接入 MQ-7 后屏幕熄灭，拔掉后恢复。

**原因**：MQ-7 加热丝功耗约 150mA，叠加屏幕背光后整机约 400mA，接近 USB 口 500mA 上限。若使用线阻较大的 USB 线或供电不足的接口，电压跌落导致 MCU 掉电复位。

**诊断**：读取 `RCC_CSR`（地址 `0x40021024`）的 `POR` 标志位，若出现新的掉电复位标志即可确认。

**解决**：

- 确认 MQ-7 的 VCC 接 **5V**（接 3.3V 必然异常）
- 改用带独立供电的 USB 口或充电宝
- 使用充电宝供电时拔掉 ST-Link，避免多电源冲突

---

## 11. 调试方法

### 11.1 串口调试

| 参数 | 值 |
|---|---|
| 波特率 | 9600 |
| 数据位 / 停止位 / 校验 | 8 / 1 / None |
| 流控 | 无 |

正常启动输出：

```
===== Gas Detector boot =====
LCD ID=0x9341  320x240  Touch resistive
```

`LCD ID` 可用于判断屏幕驱动是否识别成功：`0x9341`（ILI9341）、`0x7789`（ST7789）、`0x0000` 表示通信失败。

### 11.2 无串口时的寄存器检查法

当开发板串口不可用时，可通过 ST-Link 读取芯片内部状态进行诊断。

**判断程序是否在运行**——连续采样 PC 寄存器，若 PC 持续变化则程序正常：

```python
from pyocd.core.helpers import ConnectHelper
s = ConnectHelper.session_with_chosen_probe(options={"target_override": "stm32f103rc"})
s.open(); t = s.target
for _ in range(10):
    t.halt(); print(hex(t.read_core_register("pc"))); t.resume()
```

**判断初始化进度**——读取外设时钟寄存器：

| 寄存器 | 地址 | 含义 |
|---|---|---|
| `RCC_APB2ENR` | `0x40021018` | bit2/3/4 对应 GPIOA/B/C 时钟，全为 1 说明 `LCD_Init` 已执行 |
| `RCC_APB1ENR` | `0x4002101C` | bit1 对应 TIM3，为 1 说明时基已初始化 |
| `GPIOC->ODR` | `0x4001100C` | bit10 为屏幕背光，为 1 说明背光已点亮 |
| `RCC_CSR` | `0x40021024` | bit27 为 POR（掉电复位）标志，bit26 为 PIN（复位引脚）标志 |

**定位卡死位置**——将 PC 值与符号表对照：

```powershell
arm-none-eabi-nm -n -S --defined-only build\GasDetector.axf > build\symbols.txt
```

在 `symbols.txt` 中查找包含该地址的函数即可定位。

**实际案例**：排障时测得背光 = 1、GPIO 时钟全开，但 PC 在 24 次采样中始终停留在 `0x08020260`，对照符号表为 `lv_obj_allocate_spec_attr`，据此确认为 LVGL 内存池耗尽。

### 11.3 在线调试

VS Code 中按 `F5` 启动调试（配置见 `.vscode/launch.json`）：

| 配置 | 用途 |
|---|---|
| Debug (pyOCD autostart) | 编译 + 烧录 + 停在 `main` |
| Attach to external server | 附着到手动启动的 GDB 服务 |
| Attach (pyOCD autostart, no build) | 附着到正在运行的芯片，不重新编译 |

> 使用前需在 VS Code 中信任该工作区（`Workspaces: Manage Workspace Trust`），否则扩展任务会被禁用。

### 11.4 全片擦除

当芯片因异常状态无法烧录时：

```powershell
pyocd erase -t stm32f103rc --mass
```

> **注意**：pyOCD 0.45 起，`erase` 子命令的参数为 `--mass`（旧版本为 `--chip`）。此操作不清除 24C02（独立芯片），日志与标定数据仍会保留。

---

## 12. 移植指南

将本项目移植到其他平台时的改动点：

### 更换显示屏

| 步骤 | 说明 |
|---|---|
| 1 | 修改 `lv_port_disp_template.c` 中 `MY_DISP_HOR_RES` 为实际屏幕宽度 |
| 2 | 确认 `disp_init()` 中的 `LCD_Display_Dir()` 参数与所需方向一致 |
| 3 | 若屏幕控制器不在 `lcd.c` 支持列表中，需补充初始化序列 |
| 4 | 按新分辨率调整 `ui_gas.c` 中的布局坐标（当前按 320×240 排版） |

### 更换主控

| 步骤 | 说明 |
|---|---|
| 1 | 修改 `Makefile` 的 `--cpu` 参数（如 Cortex-M4） |
| 2 | 更换 `OBJ/` 下的分散加载文件以匹配新芯片的 Flash/RAM 布局 |
| 3 | 确认 `stm32f10x_conf.h` 中启用的外设库与新芯片匹配 |
| 4 | 修改 `USER/stm32f10x.h` 中的器件宏（如 `STM32F10X_HD`） |

### 更换传感器

以增加一路 MQ-2（可燃气体）为例：

| 步骤 | 说明 |
|---|---|
| 1 | 选择空闲 ADC 通道（PA4~PA7 为 ADC1_IN4~IN7） |
| 2 | 复制 `gas_mq7.c/h` 为 `gas_mq2.c/h`，修改通道宏与曲线常数 |
| 3 | 在 `adc1.c` 的 GPIO 初始化中加入该引脚 |
| 4 | 在 `gas_status_t` 中增加对应字段 |
| 5 | 在 `gas_app.c` 的判定逻辑中纳入新通道 |
| 6 | 在 `ui_gas.c` 中增加显示控件 |

### 移植到其他 GUI

由于界面层（`ui_gas.c`）与逻辑层（`gas_app.c`）通过 `gas_status_t` 单向通信，替换 GUI 时只需重写 `ui_gas.c`，其余模块无需改动。

---

## 13. 编码规范

### 文件头注释

每个 `.c` / `.h` 文件开头应包含模块说明：用途、核心功能、对外接口、注意事项。

### 函数注释

对外接口使用标准注释块：

```c
/**
 * @brief  读取 MQ-7 传感器并更新浓度值
 * @param  ms_elapsed  距上次调用的毫秒数，用于预热计时
 * @retval 1 表示本次产生了新数据
 * @note   建议调用周期 100ms
 */
u8 MQ7_Task(u16 ms_elapsed);
```

### 命名约定

| 类型 | 规范 | 示例 |
|---|---|---|
| 全局变量 | `g_` 前缀 | `g_status`、`g_mq7` |
| 静态变量 | `s_` 前缀 | `s_filtered`、`s_trend_head` |
| 宏定义 | 全大写下划线分隔 | `MQ7_WARMUP_MS` |
| 函数 | 模块名_动作 | `MQ7_Init`、`EEPROM_LogAdd` |
| 类型 | `_t` 后缀 | `mq7_data_t`、`gas_state_t` |

### 注释原则

| 应注释 | 不应注释 |
|---|---|
| 硬件约束与参数来源 | 语法层面的逐行解释 |
| 算法依据与公式出处 | 显而易见的赋值操作 |
| 特殊处理的原因（Why） | 代码已在表达的"做什么"（What） |
| 调用约束与副作用 | 变更历史（交由版本控制记录） |

### 分层约束

| 层级 | 允许依赖 |
|---|---|
| `APP/ui_gas.c` | LVGL、`gas_app.h`、传感器数据结构体 |
| `APP/gas_app.c` | 各传感器驱动、EEPROM、`led.h` |
| 传感器驱动 | `adc1.h`、STL 标准外设库、`delay.h` |
| 禁止 | 驱动层调用界面层函数；界面层直接读取硬件寄存器 |

---

## 相关文档

- [项目说明与接线](../README.md)
- [使用手册](USER_GUIDE.md)
- [烧录指南](FLASH_GUIDE.md)
