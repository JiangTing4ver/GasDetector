/**
 * @file    dht11.c
 * @brief   DHT11 温湿度传感器驱动实现
 * @details 单总线通信时序如下（单位：µs）：
 *
 *              主机:  ────┐                    ┌──────────────────
 *                         │  拉低 ≥18ms        │ 拉高 20~40µs 后转输入
 *                         └────────────────────┘
 *              从机:                         ┌──┐   ┌──┐  ... 40 位数据 ...
 *                                            │80│   │  │
 *                                            └──┘   └──┘
 *                                           应答低  应答高
 *
 *          数据位判定：每位以 50µs 低电平起始，随后高电平持续时间决定数值：
 *              - 高电平 26 ~ 28µs  ->  数据位 0
 *              - 高电平 70µs       ->  数据位 1
 *
 *          本实现策略：等待低电平结束后延时 40µs 再采样，
 *          该时刻位于 28µs 与 70µs 之间，容错余量最大。
 *
 * @note    全部等待循环均设有超时保护，传感器未接入时不会死锁。
 */

#include "dht11.h"
#include "delay.h"

/** 全局传感器数据 */
dht11_data_t g_dht11;

/**
 * @brief  等待数据线达到指定电平
 * @param  level       目标电平（0 或 1）
 * @param  timeout_us  超时时间 (µs)
 * @retval 0 表示在超时前达到目标电平，1 表示超时
 */
static u8 dht11_wait_level(u8 level, u16 timeout_us)
{
    while (DHT11_SDA_READ() != level)
    {
        if (timeout_us-- == 0) return 1;
        delay_us(1);
    }
    return 0;
}

void DHT11_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_APB2PeriphClockCmd(DHT11_GPIO_CLK, ENABLE);

    GPIO_InitStructure.GPIO_Pin   = DHT11_GPIO_PIN;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DHT11_GPIO_PORT, &GPIO_InitStructure);

    DHT11_SDA_H();

    /* 数据手册要求上电后 1 秒方可通信 */
    delay_ms(1200);

    g_dht11.temperature = 0;
    g_dht11.humidity    = 0;
    g_dht11.ok          = 0;
    g_dht11.err_count   = 0;
}

u8 DHT11_Read(void)
{
    u8  buf[5];
    u8  i, j;
    u8  checksum;
    u16 pulse;

    /* ---------- 1. 主机发送起始信号 ---------- */
    DHT11_SDA_OUT();
    DHT11_SDA_L();
    delay_ms(20);           /* 数据手册要求 ≥18ms */
    DHT11_SDA_H();
    delay_us(30);           /* 20 ~ 40µs */
    DHT11_SDA_IN();

    /* ---------- 2. 从机应答：80µs 低电平 + 80µs 高电平 ---------- */
    if (dht11_wait_level(0, 100)) goto err;
    if (dht11_wait_level(1, 100)) goto err;
    if (dht11_wait_level(0, 100)) goto err;

    /* ---------- 3. 读取 40 位数据（5 字节） ---------- */
    for (i = 0; i < 5; i++)
    {
        buf[i] = 0;
        for (j = 0; j < 8; j++)
        {
            buf[i] <<= 1;

            /* 等待本位起始的 50µs 低电平结束 */
            pulse = 0;
            while (DHT11_SDA_READ() == 0)
            {
                if (++pulse > 90) goto err;
                delay_us(1);
            }

            /* 延时 40µs 后采样：仍为高电平则该位为 1 */
            delay_us(40);
            if (DHT11_SDA_READ() == 1)
            {
                buf[i] |= 0x01;

                /* 若为 1，剩余高电平约 30µs，等待其结束再读取下一位 */
                pulse = 0;
                while (DHT11_SDA_READ() == 1)
                {
                    if (++pulse > 90) goto err;
                    delay_us(1);
                }
            }
        }
    }

    /* ---------- 4. 校验和验证（前 4 字节之和的低 8 位） ---------- */
    checksum = (u8)(buf[0] + buf[1] + buf[2] + buf[3]);
    if (checksum != buf[4]) goto err;

    /* ---------- 5. 更新测量结果 ---------- */
    g_dht11.humidity    = buf[0];   /* buf[1] 为湿度小数位，DHT11 恒为 0 */
    g_dht11.temperature = buf[2];   /* buf[3] 为温度小数位，DHT11 恒为 0 */
    g_dht11.ok          = 1;
    g_dht11.err_count   = 0;
    return 1;

err:
    g_dht11.ok = 0;

    /* 饱和计数，避免溢出回绕 */
    if (g_dht11.err_count < 200) g_dht11.err_count++;

    return 0;
}
