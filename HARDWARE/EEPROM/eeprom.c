/**
 * @file    eeprom.c
 * @brief   24C02 EEPROM 存储层实现
 * @details 底层 I2C 时序与字节读写复用底层驱动（24cxx.c），
 *          本模块仅负责地址空间管理与日志环形缓冲逻辑。
 */

#include "eeprom.h"
#include "24cxx.h"
#include "myiic.h"
#include "delay.h"

/** 24C02 存在标志 */
static u8 s_present = 0;

/** 日志环形缓冲写指针 */
static u8 s_head = 0;

/** 已存储日志条数 */
static u8 s_count = 0;

void EEPROM_Init(void)
{
    u8 magic;
    u8 i;

    AT24CXX_Init();

    /* AT24CXX_Check 会读写地址 255 的标志字，可据此判断芯片是否在线 */
    s_present = (AT24CXX_Check() == 0) ? 1 : 0;

    if (!s_present)
    {
        /* 芯片未接入时保持空状态，后续所有操作安全返回 */
        s_head  = 0;
        s_count = 0;
        return;
    }

    magic = AT24CXX_ReadOneByte(EE_MAGIC_ADDR);

    if (magic != EE_MAGIC_VALUE)
    {
        /* 首次使用：初始化配置区并清空全部日志槽位 */
        AT24CXX_WriteOneByte(EE_MAGIC_ADDR, EE_MAGIC_VALUE);
        AT24CXX_WriteOneByte(EE_LOG_HEAD_ADDR, 0);
        AT24CXX_WriteOneByte(EE_LOG_COUNT_ADDR, 0);
        AT24CXX_WriteLenByte(EE_R0_ADDR, 0, 4);

        for (i = 0; i < EE_LOG_SLOT_MAX; i++)
        {
            AT24CXX_WriteOneByte(EE_LOG_BASE + (u16)i * EE_LOG_SLOT_SIZE, 0xFF);
        }

        s_head  = 0;
        s_count = 0;
        return;
    }

    /* 载入已有的日志状态 */
    s_head  = AT24CXX_ReadOneByte(EE_LOG_HEAD_ADDR);
    s_count = AT24CXX_ReadOneByte(EE_LOG_COUNT_ADDR);

    /* 数据有效性校验 */
    if (s_head >= EE_LOG_SLOT_MAX)  s_head  = 0;
    if (s_count > EE_LOG_SLOT_MAX)  s_count = EE_LOG_SLOT_MAX;
}

u8 EEPROM_IsPresent(void)
{
    return s_present;
}

void EEPROM_ReadR0(u32 *r0)
{
    if (!s_present)
    {
        *r0 = 0;
        return;
    }

    *r0 = AT24CXX_ReadLenByte(EE_R0_ADDR, 4);
}

void EEPROM_SaveR0(u32 r0)
{
    if (!s_present) return;

    AT24CXX_WriteLenByte(EE_R0_ADDR, r0, 4);
}

void EEPROM_LogAdd(ee_log_t *log)
{
    u16 addr;

    if (!s_present || log == 0) return;

    addr = EE_LOG_BASE + (u16)s_head * EE_LOG_SLOT_SIZE;

    /* 按固定字节序写入，保证跨平台可解析 */
    AT24CXX_WriteOneByte(addr + 0, (u8)(log->time_min));
    AT24CXX_WriteOneByte(addr + 1, (u8)(log->time_min >> 8));
    AT24CXX_WriteOneByte(addr + 2, (u8)(log->time_min >> 16));
    AT24CXX_WriteOneByte(addr + 3, (u8)(log->time_min >> 24));
    AT24CXX_WriteOneByte(addr + 4, (u8)(log->co_ppm));
    AT24CXX_WriteOneByte(addr + 5, (u8)(log->co_ppm >> 8));
    AT24CXX_WriteOneByte(addr + 6, log->temp);
    AT24CXX_WriteOneByte(addr + 7, log->src);

    s_head++;
    if (s_head >= EE_LOG_SLOT_MAX) s_head = 0;
    if (s_count < EE_LOG_SLOT_MAX) s_count++;

    AT24CXX_WriteOneByte(EE_LOG_HEAD_ADDR,  s_head);
    AT24CXX_WriteOneByte(EE_LOG_COUNT_ADDR, s_count);
}

u16 EEPROM_LogCount(void)
{
    return s_count;
}

u8 EEPROM_LogGet(u16 index, ee_log_t *log)
{
    u8  slot;
    u16 addr;

    if (!s_present || log == 0 || index >= s_count) return 0;

    /* 最近一条记录位于 head - 1，索引越大则越旧 */
    if (s_head == 0) slot = EE_LOG_SLOT_MAX - 1;
    else             slot = s_head - 1;
    if (index > 0)   slot = (u8)((slot + EE_LOG_SLOT_MAX - index) % EE_LOG_SLOT_MAX);

    addr = EE_LOG_BASE + (u16)slot * EE_LOG_SLOT_SIZE;

    log->time_min  = (u32)AT24CXX_ReadOneByte(addr + 0);
    log->time_min |= (u32)AT24CXX_ReadOneByte(addr + 1) << 8;
    log->time_min |= (u32)AT24CXX_ReadOneByte(addr + 2) << 16;
    log->time_min |= (u32)AT24CXX_ReadOneByte(addr + 3) << 24;

    log->co_ppm  = (u16)AT24CXX_ReadOneByte(addr + 4);
    log->co_ppm |= (u16)AT24CXX_ReadOneByte(addr + 5) << 8;

    log->temp = AT24CXX_ReadOneByte(addr + 6);
    log->src  = AT24CXX_ReadOneByte(addr + 7);

    /* 0xFF 为未写入槽位的标志（EEPROM 擦除态），据此过滤无效数据 */
    if (log->src == 0xFF) return 0;

    return 1;
}

void EEPROM_LogClear(void)
{
    if (!s_present) return;

    s_head  = 0;
    s_count = 0;

    AT24CXX_WriteOneByte(EE_LOG_HEAD_ADDR,  0);
    AT24CXX_WriteOneByte(EE_LOG_COUNT_ADDR, 0);

    /* 条数清零后所有槽位即被视为无效数据，无需逐条擦除 */
}
