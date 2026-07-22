#include "stm32f4xx.h"

/*
 * 硬件 PWM 呼吸灯 — TIM3_CH1 (PC6)
 *
 * TIM3 配置:
 *   时钟 = 16MHz, PSC = 160-1 → 100kHz
 *   ARR = 999 → PWM 周期 = 10ms (100Hz 无闪烁)
 *   CCR1 = 0~999 调节占空比 (0% ~ 100%)
 *
 * 呼吸效果:
 *   CCR1 从 0 逐步加到 999（渐亮）
 *   再从 999 逐步减到 0（渐暗）
 *   循环
 */

/* ── 呼吸亮度表（100 级正弦曲线，CCR = 0~999） ── */
static const uint16_t breath_ccr[100] = {
    0,   1,   5,  11,  19,  30,  43,  58,  75,  95,
  117, 140, 165, 192, 220, 249, 278, 308, 338, 368,
  397, 425, 451, 476, 499, 520, 540, 557, 572, 585,
  596, 605, 611, 616, 618, 618, 616, 611, 605, 596,
  585, 572, 557, 540, 520, 499, 476, 451, 425, 397,
  368, 338, 308, 278, 249, 220, 192, 165, 140, 117,
   95,  75,  58,  43,  30,  19,  11,   5,   1,   0,
    0,   1,   5,  11,  19,  30,  43,  58,  75,  95,
  117, 140, 165, 192, 220, 249, 278, 308, 338, 368,
  397, 425, 451, 476, 499, 520, 540, 557, 572, 585,
};

/* ── 简单延时 ── */
static void delay_ms(uint32_t ms) {
  while (ms--)
    for (volatile uint32_t i = 0; i < 8000; i++) __NOP();
}

/* ── TIM3 PWM 初始化 ── */
static void tim3_pwm_init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

  /* 时基: 16MHz / 160 = 100kHz, ARR=999 → 10ms 周期 (100Hz) */
  TIM3->PSC   = 160 - 1;
  TIM3->ARR   = 999;
  TIM3->CNT   = 0;

  /* CH1 (PC6) → PWM 模式 1 */
  TIM3->CCMR1 &= ~TIM_CCMR1_OC1M;
  TIM3->CCMR1 |=  (6 << TIM_CCMR1_OC1M_Pos);    /* PWM mode 1 */
  TIM3->CCMR1 |=  TIM_CCMR1_OC1PE;              /* 预装载使能 */
  TIM3->CCER  |=  TIM_CCER_CC1E;                /* CH1 输出使能 */
  TIM3->CCR1  = 0;                               /* 初始占空比 = 0 */

  TIM3->CR1 |= TIM_CR1_ARPE;                    /* ARR 预装载 */
  TIM3->EGR |= TIM_EGR_UG;                      /* 更新 */
  TIM3->CR1 |= TIM_CR1_CEN;                     /* 启动 */
}

/* ── PC6 配置为 AF2 (TIM3_CH1) ── */
static void gpio_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;

  /* PC6 → AF2 (TIM3_CH1) */
  GPIOC->MODER  &= ~GPIO_MODER_MODER6;
  GPIOC->MODER  |=  (2 << (6*2));                /* 复用功能 */
  GPIOC->AFR[0] &= ~(0xF << 24);
  GPIOC->AFR[0] |=  (2 << 24);                  /* AF2 = TIM3 */
}

/* ── 主函数 ── */
int main(void) {
  gpio_init();
  tim3_pwm_init();

  uint16_t idx = 0;

  while (1) {
    /*
     * 更新 CCR1 → 改变占空比 → 亮度变化
     * 每 15ms 更新一级，呼吸周期 ≈ 100 * 15ms ≈ 1.5s
     */
    TIM3->CCR1 = breath_ccr[idx];                /* 更新占空比 */

    delay_ms(15);                                 /* 停留 15ms */

    idx = (idx + 1) % 100;                        /* 下一级亮度 */
  }
}
