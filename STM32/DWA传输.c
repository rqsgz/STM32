/**
 * dac_sine_demo.c — DAC2 (PA5) + DMA + TIM7 正弦波输出
 *
 * === 启动自检 (前 1 秒) ===
 *   软件手动写 DAC, 电压从 0V 匀速升到 3.3V
 *   示波器 CH1 看到一条直线上爬 = 硬件 OK
 *
 * === 然后自动进入 DMA 模式 ===
 *   TIM7 → TRGO → DAC 触发 → DMA 循环搬正弦表 → 100Hz 正弦波
 *
 * === 接线 ===
 *   CH1 探头 → PA5 (先看自检斜坡, 再看正弦波)
 *   CH2 探头 → PC7 (采样时钟方波)
 *   接地夹   → GND (共地, 接一个就行)
 */

#include "stm32f4xx.h"
#include <math.h>

#define SINE_SAMPLES    256
#define SINE_FREQ_HZ    100

static uint16_t sine_tab[SINE_SAMPLES];

/* ---------- USART1 (PA9 TX, 115200) ---------- */

static void u1c(char c) {
    while (!(USART1->SR & USART_SR_TXE));
    USART1->DR = (uint8_t)c;
}
static void u1s(const char *s) { while (*s) u1c(*s++); }
static void u1n(int32_t n) {
    char b[12]; int i = 0;
    if (n < 0) { u1c('-'); n = -n; }
    if (n == 0) b[i++] = '0';
    else while (n) { b[i++] = (char)('0' + n % 10); n /= 10; }
    while (i) u1c(b[--i]);
}

/* ---------- DWT 微秒延时 ---------- */

static void dus(uint32_t us) {
    uint32_t s = DWT->CYCCNT;
    while ((DWT->CYCCNT - s) < us * 16);
}

/* ---------- TIM7 ISR: 翻转 PC7 作为采样时钟 ---------- */

void TIM7_IRQHandler(void) {
    if (TIM7->SR & TIM_SR_UIF) {
        TIM7->SR = ~TIM_SR_UIF;
        GPIOC->ODR ^= (1U << 7);
    }
}

/* ================================================================
 * main
 * ================================================================ */

int main(void)
{
    /* --- DWT --- */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* --- USART1 --- */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    GPIOA->MODER &= ~((3U << (9*2)) | (3U << (10*2)));
    GPIOA->MODER |= (2U << (9*2)) | (2U << (10*2));
    GPIOA->AFR[1] |= (7U << 4) | (7U << 8);
    USART1->BRR = (8 << 4) | 11;
    USART1->CR1 = USART_CR1_TE | USART_CR1_UE;

    u1s("\r\n===== DAC Demo =====\r\n");

    /* --- GPIO: PA5=analog, PC7=output --- */
    GPIOA->MODER |= (3U << (5*2));
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
    GPIOC->MODER |= (1U << (7*2));

    /* --- LED PF9 --- */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;
    GPIOF->MODER |= (1U << (9*2));
    GPIOF->BSRR = (uint32_t)GPIO_PIN_9;    /* 灭 */

    /* ================================================================
     * 阶段 1: 硬件自检 — 软件手动斜坡 0→3.3V (约 1 秒)
     *
     *   CH1 应该看到一条直线从 0V 缓慢爬到 ~3.3V
     *   如果看到 → DAC 硬件正常 → 自动进入阶段 2
     *   如果没看到 → 检查 PA5 接线 / 探头 / 接地
     * ================================================================ */

    /* 使能 DAC */
    RCC->APB1ENR |= RCC_APB1ENR_DACEN;
    DAC->CR |= DAC_CR_EN2;         /* 开启 DAC2, 无触发, 无 DMA */

    u1s("[TEST] DAC ramp 0→3.3V. Watch CH1...\r\n");

    /* 软件逐个写 DAC 值: 0→4095, 每步 ~250us, 总共约 1 秒 */
    for (int v = 0; v <= 4095; v++) {
        DAC->DHR12R2 = (uint16_t)v;
        dus(240);                   /* ~240us * 4096 ≈ 1 秒 */
    }

    /* 停在 3.3V 一小会儿, 方便看清 */
    DAC->DHR12R2 = 4095;
    u1s("[TEST] Holding 3.3V for 0.5s...\r\n");
    dus(500000);

    /* 再降到 1.65V (中点) */
    DAC->DHR12R2 = 2048;
    u1s("[TEST] Dropping to mid (1.65V)...\r\n");
    dus(500000);

    u1s("[TEST] Done. If you saw voltage move → DAC HW OK!\r\n");

    /* ================================================================
     * 阶段 2: 生成正弦表
     * ================================================================ */
    for (int i = 0; i < SINE_SAMPLES; i++) {
        double ph = 2.0 * 3.141592653589793 * i / SINE_SAMPLES;
        sine_tab[i] = (uint16_t)(sin(ph) * 2047.0 + 2048.0);
    }
    u1s("Sine table ready\r\n");

    /* ================================================================
     * 阶段 3: 配置 DAC 触发模式 (TIM7 TRGO)
     * ================================================================ */
    DAC->CR &= ~(0x3FFF << 16);                  /* 清除 channel 2 */
    DAC->CR |= (2U << DAC_CR_TSEL2_Pos);         /* TSEL2=010: TIM7 TRGO */
    DAC->CR |= DAC_CR_TEN2 | DAC_CR_DMAEN2;      /* 触发+DMA */
    /* 注意: EN2 保持, 不要清掉 */

    /* ================================================================
     * 阶段 4: DMA2 Stream3 Channel7
     * ================================================================ */
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;

    DMA2_Stream3->CR = 0;
    while (DMA2_Stream3->CR & DMA_SxCR_EN);

    DMA2_Stream3->PAR  = (uint32_t)&DAC->DHR12R2;
    DMA2_Stream3->M0AR = (uint32_t)sine_tab;
    DMA2_Stream3->NDTR = SINE_SAMPLES;
    DMA2_Stream3->FCR  = 0;

    DMA2_Stream3->CR =
          (7U << DMA_SxCR_CHSEL_Pos)             /* CH7 → DAC2 */
        | (1U << DMA_SxCR_MSIZE_Pos)             /* 16-bit */
        | (1U << DMA_SxCR_PSIZE_Pos)             /* 16-bit */
        | DMA_SxCR_MINC                          /* 内存递增 */
        | DMA_SxCR_CIRC                          /* 循环 */
        | (1U << DMA_SxCR_DIR_Pos);              /* M→P */

    /* ================================================================
     * 阶段 5: TIM7
     * ================================================================ */
    RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;

    TIM7->PSC = 0;
    TIM7->ARR = (uint16_t)(16000000UL / (SINE_FREQ_HZ * SINE_SAMPLES) - 1);

    TIM7->CR2 &= ~TIM_CR2_MMS;
    TIM7->CR2 |= TIM_CR2_MMS_1;                  /* MMS=010: Update→TRGO */

    TIM7->DIER |= TIM_DIER_UIE;
    NVIC_SetPriority(TIM7_IRQn, 2);
    NVIC_EnableIRQ(TIM7_IRQn);

    u1s("TIM7+DMA+DAC ready. ARR="); u1n(TIM7->ARR);
    u1s(", sine="); u1n(SINE_FREQ_HZ); u1s("Hz\r\n");

    /* ================================================================
     * 阶段 6: 启动 (预装→DMA→TIM)
     * ================================================================ */
    DAC->DHR12R2 = sine_tab[0];                   /* ① 预装 */
    DMA2_Stream3->CR |= DMA_SxCR_EN;              /* ② DMA 开 */
    TIM7->CR1 |= TIM_CR1_CEN;                     /* ③ 定时器开 */

    /* ================================================================
     * 阶段 7: 主循环 — LED 心跳
     * ================================================================ */
    u1s("===== Running =====\r\n");
    u1s("CH1→PA5 CH2→PC7 GND→GND\r\n");
    u1s("LED blink = CPU idle\r\n");

    uint32_t last = DWT->CYCCNT;
    while (1) {
        if (DWT->CYCCNT - last >= 16000000UL) {
            last = DWT->CYCCNT;
            GPIOF->ODR ^= GPIO_PIN_9;
        }
    }
}
