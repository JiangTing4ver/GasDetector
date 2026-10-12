# 烧录与调试指南

本文档面向**零基础用户**，从接线开始，逐步完成编译、烧录与验证。
每一步都说明"做什么"和"应该看到什么"，遇到问题可查阅文末的错误对照表。

---

## 目录

- [1. 准备工作](#1-准备工作)
- [2. 接线](#2-接线)
- [3. 确认环境](#3-确认环境)
- [4. 编译](#4-编译)
- [5. 烧录](#5-烧录)
- [6. 验证串口输出](#6-验证串口输出)
- [7. 验证屏幕](#7-验证屏幕)
- [8. 错误对照表](#8-错误对照表)
- [9. 常用命令速查](#9-常用命令速查)

---

## 1. 准备工作

| 物品 | 说明 |
|---|---|
| 开发板 | 正点原子 MiniSTM32（STM32F103RCT6） |
| 调试器 | **ST-Link V2**（USB 接口 + 20 针或 4 针 SWD 排线） |
| USB 数据线 | 连接开发板 USB 口，用于**供电与串口通信** |
| 已安装软件 | Keil ARMCC、pyOCD、VS Code |

> **重要概念**：ST-Link 与开发板 USB 是**两条独立通路**
>
> | 设备 | 作用 |
> |---|---|
> | ST-Link | 仅负责 SWD 调试（下载程序、单步调试），**不提供串口功能** |
> | 开发板 USB（CH340） | 负责供电与串口输出，`printf` 的内容从这里输出 |
>
> 开发过程中两者通常需要**同时连接**：ST-Link 下载程序，USB 线查看串口日志。

---

## 2. 接线

### 2.1 ST-Link 与开发板

```
       ST-Link V2                        MiniSTM32 开发板
    +-------------+                    +----------------------+
    |  USB 口     |---[USB]---电脑     |  [USB 口]---电脑      |
    |             |                    |                      |
    |  20 针排线  |====================|  [JTAG/SWD 排座]     |
    +-------------+                    +----------------------+
```

使用 20 针排线时直接插入开发板的 JTAG 排座，**接口有防插反缺口**。

若使用 4 根杜邦线（SWD 模式）：

| ST-Link 引脚 | 开发板 | 说明 |
|---|---|---|
| SWDIO | PA13 | 数据线 |
| SWCLK | PA14 | 时钟线 |
| **GND** | GND | **必须连接**，遗漏会导致无法识别芯片 |
| 3.3V | 3.3V | 可选，开发板已由 USB 供电时可不接 |

### 2.2 外设接线

传感器接线请参考 [README 接线说明](../README.md#接线说明)。

---

## 3. 确认环境

### 3.1 确认 ST-Link 已被识别

打开 PowerShell（VS Code 中按 `` Ctrl+` `` 亦可），执行：

```powershell
C:\Users\<用户名>\.pyocd-venv\Scripts\pyocd.exe list --probes
```

**预期输出**：

```
  #   Probe/Board    Unique ID                  Target
--------------------------------------------------------
  0   STM32 STLink   680025000D0000415246524E   n/a
```

| 结果 | 说明 | 下一步 |
|---|---|---|
| 出现 `STM32 STLink` 行 | 接线正常 | 继续第 4 步 |
| 表格为空或报错 | 接线异常 | 检查 GND 与 USB 线，重新插拔排线 |

> `Target` 显示 `n/a` 属正常现象，表示尚未与芯片建立握手，不影响后续烧录。

### 3.2 确认串口设备

在 Windows「设备管理器」中展开「端口 (COM 和 LPT)」，查找 `USB-SERIAL CH340`。

| 结果 | 说明 | 处理 |
|---|---|---|
| 有 `USB-SERIAL CH340 (COMx)` | 正常，记住该 COM 号 | 继续 |
| 设备带黄色感叹号 | 驱动未安装 | 安装 CH340 驱动后重新插拔 |
| 无任何新设备 | 数据线不支持传输或未插好 | 更换 USB 数据线 |

> 若没有串口也不影响烧录与运行，可参考 [开发文档](DEVELOPER_GUIDE.md#112-无串口时的寄存器检查法) 使用寄存器检查法诊断。

---

## 4. 编译

```powershell
cd D:\Workspace\LVGL\02-GasDetector
.\build.bat
```

**预期输出末尾**：

```
Total RO  Size (Code + RO Data)               249744 ( 243.89kB)
Total RW  Size (RW Data + ZI Data)             37968 (  37.08kB)
Total ROM Size (Code + RO Data + RW Data)     249972 ( 244.11kB)
```

**产物**：`build\GasDetector.hex` 与 `build\GasDetector.axf`

> **关于 `make: Nothing to be done for 'all'.`**
> 这**不是错误**，而是表示"源文件未改动，现有编译产物已是最新"。

> ⚠️ **修改任何 `.h` 文件后必须完整重编**：
> ```powershell
> .\build.bat clean
> .\build.bat
> ```
> 本项目 Makefile 没有头文件依赖规则，直接执行 `build.bat` 不会重新编译，改动不会生效。

---

## 5. 烧录

确认当前目录为项目根目录，执行：

```powershell
C:\Users\<用户名>\.pyocd-venv\Scripts\pyocd.exe load -t stm32f103rc build\GasDetector.hex
```

**预期输出**：

```
0001656 I Loading D:\...\build\GasDetector.hex [load_cmd]
0001928 I Erasing... [loader]
[==================================================] 100%
0006871 I Programming... [loader]
[==================================================] 100%
0016505 I Erased 251904 bytes (123 sectors), programmed 251904 bytes (123 pages) ...
```

**看到 `Programmed ... pages` 即表示烧录成功**，设备会自动复位并运行。

### 命令参数说明

| 参数 | 含义 |
|---|---|
| `load` | 子命令：将程序写入芯片 Flash |
| `-t stm32f103rc` | 目标芯片型号 |
| `build\GasDetector.hex` | 待烧录文件路径 |

### 使用 VS Code 任务烧录

按 `Ctrl+Shift+P` → 输入 `Tasks: Run Task`，选择：

| 任务 | 作用 |
|---|---|
| **Build** | 仅编译（等同 `Ctrl+Shift+B`） |
| **Flash only (pyOCD)** | 仅烧录 |
| **Flash (pyOCD + ST-Link)** | 编译 + 烧录（日常使用推荐） |
| **Erase chip (pyOCD)** | 全片擦除（烧录失败时使用） |
| **Rebuild** | 清理后完整重建 |

> 使用任务前需信任该工作区：`Ctrl+Shift+P` → `Workspaces: Manage Workspace Trust`。
> 未信任时 VS Code 处于受限模式，任务与调试功能会被禁用。

### 设置命令别名（可选）

若觉得命令过长，可为 PowerShell 添加别名。执行 `notepad $PROFILE`，在文件末尾添加：

```powershell
function flash { C:\Users\<用户名>\.pyocd-venv\Scripts\pyocd.exe load -t stm32f103rc build\GasDetector.hex }
```

保存后重开终端，在项目目录下执行 `flash` 即可。

---

## 6. 验证串口输出

### 6.1 串口参数

| 参数 | 值 |
|---|---|
| 端口 | 设备管理器中 CH340 对应的 COM 号 |
| 波特率 | **9600** |
| 数据位 | 8 |
| 停止位 | 1 |
| 校验 | 无（None） |
| 流控 | 无 |

### 6.2 打开串口的三种方式

**方式 A：VS Code 扩展（推荐）**

1. 左侧点击扩展图标，搜索 `Serial Monitor`，安装 Microsoft 官方版本
2. `Ctrl+Shift+P` → `Serial Monitor: Focus on Serial Monitor View`
3. 底部选择 COM 口，波特率改为 `9600`，点击 Start Monitoring

**方式 B：串口助手软件**

XCOM、SSCOM 等均可，选择 COM 口与 9600 波特率后打开串口。

**方式 C：PowerShell 直接读取**

```powershell
$p = New-Object System.IO.Ports.SerialPort COM5,9600,None,8,one
$p.Open()
while ($true) { $p.ReadLine() }
```

查看可用串口：

```powershell
[System.IO.Ports.SerialPort]::GetPortNames()
```

### 6.3 按复位键后应看到

```
===== Gas Detector boot =====
LCD ID=0x9341  320x240  Touch resistive
```

### 6.4 输出信息解读

| 输出 | 含义 | 异常处理 |
|---|---|---|
| `LCD ID=0x9341` | 识别为 ILI9341 屏，正常 | 显示 `0x0000` 表示屏幕通信失败，检查排线 |
| `320x240` | 横屏分辨率正确 | 显示 `240x320` 表示方向配置被改动 |
| `Touch resistive` | 识别为电阻屏，符合预期 | 显示 `capacitive` 表示屏幕型号不符 |

---

## 7. 验证屏幕

### 7.1 首次上电：触摸校准（正常流程）

因为使用电阻屏且 EEPROM 中尚无校准数据，程序会先进入触摸校准：

```
+------------------------------------------+
|                                          |
|   +                                      |   <- 依次出现 4 个十字
|                                          |      用手指或触控笔点准中心
|                        +                 |
|                                          |
|              +                           |
+------------------------------------------+
```

逐个点准后自动保存至 EEPROM，**以后开机不再询问**。

> **看到此界面说明程序已正常运行**，这不是故障。

### 7.2 校准完成后进入主界面

```
+--------------------------------------------------------------+
| [WARM UP]            CO MONITOR              [电池] 100%     |
+--------------------------------------------------------------+
|  +------------------------------------------------------+    |
|  | CO                            + TREND -----------+   |    |
|  | 0                    ppm      |      ~~~~~       |   |    |
|  | sensor heating, please wait   +------------------+   |    |
|  +------------------------------------------------------+    |
|  ----------------------------------------------------------  |
|  TEMP 28 C     HUMI 44 %RH                                   |
|  Vout 0.67V     UP 00:09    LOG 11                           |
+--------------------------------------------------------------+
|    [ MAIN ]        [ LOG ]        [ CAL ]                    |
+--------------------------------------------------------------+
```

### 7.3 逐项检查

| 检查项 | 正常表现 | 异常处理 |
|---|---|---|
| 状态胶囊 | 蓝色 `WARM UP` + 180 秒倒计时 | 属正常流程，等待 3 分钟 |
| CO 读数 | 预热期显示剩余秒数，之后显示浓度值 | 始终 0.0 且 `Vout` 为 0 时，检查 MQ-7 的 AO 接线 |
| TEMP / HUMI | 显示实际温湿度 | 显示 `--` 时，检查 DHT11 上拉电阻 |
| 电池百分比 | 接近实际电量 | 始终 0% 时，检查分压电路 |
| 按键 | 点击有响应 | 无响应时重新校准触摸 |

### 7.4 验证报警功能

⚠️ **安全提示**：以下操作使用打火机气体，**只放气、不点火**。禁止使用酒精或香水（会污染传感器敏感材料，导致读数长期漂移）。更安全的方式是对着传感器哈气。

1. 用打火机对着 MQ-7 金属网罩释放少量气体（不点火）
2. 观察界面：

| 顺序 | 表现 |
|---|---|
| 1 | CO 读数快速上升 |
| 2 | 状态胶囊变橙色 `WARNING` |
| 3 | 状态变红色 `ALARM`，读数卡边框呼吸闪烁，LED 急促闪烁 |
| 4 | 切换到 `LOG` 页可见新记录 |

---

## 8. 错误对照表

### 8.1 烧录阶段

| 报错信息 | 原因 | 处理方法 |
|---|---|---|
| `No available debug probes are connected` | ST-Link 未插好或驱动异常 | 重新插拔；执行 `pyocd list --probes` 确认 |
| `Target not found` / `no ACK` | 接线松动，尤其 GND | 重新插拔排线；确认开发板 USB 已供电 |
| `'stm32f103rc' is not a recognized target` | 型号名称输入错误 | 执行 `pyocd list --targets` 查询准确名称 |
| `The transfer timed out` | 排线过长或信号干扰 | 更换较短排线；降低 SWD 频率 |
| `cannot open ... GasDetector.hex` | 路径错误或未编译 | 先执行 `.\build.bat`；确认当前目录正确 |
| 烧录过程卡住 | 芯片处于读保护等异常状态 | 执行全片擦除（见 8.4） |

### 8.2 编译阶段

| 输出 | 含义 |
|---|---|
| `make: Nothing to be done for 'all'.` | **正常**，源文件未改动，无需重编 |
| `#564: cannot open embedded assembler output file` | 绕过了 `build.bat` 直接调用 `make`，或添加了 `-j` 参数 |
| `Error: #20: identifier "xxx" is undefined` | 头文件未包含或外设库未启用（查看具体文件与标识符） |

### 8.3 屏幕阶段

| 现象 | 原因 | 处理方法 |
|---|---|---|
| **屏幕空白但背光亮** | LVGL 内存池耗尽，`LV_ASSERT_MALLOC` 进入死循环 | 确认 `lv_conf.h` 中 `LV_MEM_SIZE` 为 24KB，然后 `build.bat clean` 完整重编 |
| **修改 `lv_conf.h` 后无变化** | Makefile 无头文件依赖规则 | `.\build.bat clean` 后重新编译 |
| 完全白屏 | 排线松动或屏幕型号不符 | 查看串口输出的 `LCD ID`；重新插紧排线 |
| **花屏、颜色错乱** | PB3/PB4 未从 JTAG 功能释放 | 确认 `USER/main.c` 中 `KEY_Init()` 在屏幕初始化之前调用（项目代码已处理） |
| 屏幕偏暗或间歇熄灭 | 供电不足（MQ-7 加热丝功耗较大） | 使用带独立供电的 USB 口或充电宝 |
| 界面出来了但触摸点不准 | 校准数据异常 | 重新校准（见 8.5） |

### 8.3.1 无串口时如何判断程序状态

ST-Link 不提供串口功能。若板载 CH340 未被识别，可通过读取芯片内部寄存器诊断。

**判断程序是否运行**——连续采样 PC 寄存器：

```python
from pyocd.core.helpers import ConnectHelper
s = ConnectHelper.session_with_chosen_probe(options={"target_override": "stm32f103rc"})
s.open(); t = s.target
for _ in range(10):
    t.halt(); print(hex(t.read_core_register("pc"))); t.resume()
```

PC 持续变化表示程序正在运行；若始终停留在同一地址，对照符号表即可定位卡死位置。

**判断初始化进度**：

| 寄存器 | 地址 | 含义 |
|---|---|---|
| `RCC_APB2ENR` | `0x40021018` | bit2/3/4 为 GPIOA/B/C 时钟，全开说明 `LCD_Init` 已执行 |
| `RCC_APB1ENR` | `0x4002101C` | bit1 为 TIM3，为 1 说明时基已初始化 |
| `GPIOC->ODR` | `0x4001100C` | bit10 为屏幕背光，为 1 说明背光已点亮 |
| `RCC_CSR` | `0x40021024` | bit27 为掉电复位（POR）标志，反复出现说明供电不稳 |

### 8.4 全片擦除

烧录持续失败时执行：

```powershell
C:\Users\<用户名>\.pyocd-venv\Scripts\pyocd.exe erase -t stm32f103rc --mass
```

> **注意**：pyOCD 0.45 起参数为 `--mass`（旧版本为 `--chip`）。
> 该操作不会清除 24C02（独立芯片），日志与标定数据仍保留。

### 8.5 重新进行触摸校准

校准数据存放于 24C02 的 40~53 字节，正常情况下无需重新校准。若确需重做，可将该区域的标记字（地址 `40+13`）写入非 `0x0A` 的值，重启后程序会检测到未校准并重新进入校准流程。

---

## 9. 常用命令速查

```powershell
# 编译
cd D:\Workspace\LVGL\02-GasDetector
.\build.bat

# 修改头文件后必须完整重编
.\build.bat clean
.\build.bat

# 查看 ST-Link 是否连接
C:\Users\<用户名>\.pyocd-venv\Scripts\pyocd.exe list --probes

# 烧录
C:\Users\<用户名>\.pyocd-venv\Scripts\pyocd.exe load -t stm32f103rc build\GasDetector.hex

# 全片擦除（烧录失败时）
C:\Users\<用户名>\.pyocd-venv\Scripts\pyocd.exe erase -t stm32f103rc --mass

# 查看可用串口
[System.IO.Ports.SerialPort]::GetPortNames()

# 查看 Flash / RAM 占用
D:\apps\keil5MDK\ARM\ARMCC\bin\fromelf.exe --text -z build\GasDetector.axf
```

| 项目 | 值 |
|---|---|
| 串口参数 | 9600 / 8 / 1 / None / 无流控 |
| 目标芯片 | `stm32f103rc` |
| 成功判据 | 串口输出 `LCD ID=0x9341  320x240`，屏幕进入触摸校准或主界面 |

---

## 相关文档

- [项目说明与接线](../README.md)
- [使用手册](USER_GUIDE.md)
- [开发文档](DEVELOPER_GUIDE.md)
