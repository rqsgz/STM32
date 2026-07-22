#include "stm32f4xx.h"

/*
 * 定时器输入捕获 — 测量 KEY0 按下时长
 *
 * TIM3_CH1 (PA6, AF2) 未必方便接到按键
 * → 改方案：用 TIM5 捕获 PA0 (KEY_UP) 的时长
 *   TIM5_CH1 在 PA0 (AF2)
 *
 * 但用户要用 KEY0... 换个思路：
 *   TIM4_CH2 在 PD12... 不对
 *   KEY0 = PE4 → TIM4_CH2 (AF2) 可以用！
 *
 * 实际上 PE4 的 AF2 既是 TIM3_CH2 也是 TIM4_CH2...
 * 我们直接用 GPIO 中断 EXTI 来捕获时间反而更简单！
 *
 * ── 改用 SysTick 计时 + EXTI 中断 ──
 * 原理：
 *   1. SysTick 做 1ms 计时器
 *   2. EXTI4 中断：上升沿→记录开始，下降沿→记录结束
 *   3. 差值 = 按下时长
 *   4. TIM2 仍然独立驱动 LED 闪烁
 */

static volatile uint32_t tick_ms    = 0;       /* SysTick 毫秒计数 */
static volatile uint32_t press_start = 0;       /* 按下时刻 */
static volatile uint32_t press_end   = 0;       /* 松开时刻 */
static volatile uint32_t press_done  = 0;       /* 测量完成 */

/* ── SysTick ── */
void SysTick_Handler(void) {
  tick_ms++;
}

static void systick_init(void) {
  SysTick_Config(16000);          /* 16MHz / 16000 = 1ms */
}

/* ── EXTI4 中断（KEY0 = PE4） ── */
void EXTI4_IRQHandler(void) {
  if (EXTI->PR & EXTI_PR_PR4) {
    EXTI->PR = EXTI_PR_PR4;                    /* 清中断标志 */

    if (GPIOE->IDR & GPIO_PIN_4) {
      /* 上升沿 → 按下 */
      press_start = tick_ms;
      /* 切换为下降沿触发 */
      EXTI->FTSR |= EXTI_FTSR_TR4;
      EXTI->RTSR &= ~EXTI_RTSR_TR4;
    } else {
      /* 下降沿 → 松开 */
      press_end   = tick_ms;
      press_done  = 1;
      /* 切回上升沿触发 */
      EXTI->RTSR |= EXTI_RTSR_TR4;
      EXTI->FTSR &= ~EXTI_FTSR_TR4;
    }
  }
}

/* ── 简单延时 ── */
static void delay_ms(uint32_t ms) {
  while (ms--)
    for (volatile uint32_t i = 0; i < 8000; i++) __NOP();
}

/* ── KEY_UP (PA0) 普通按键 ── */
static int keyup_edge(uint8_t *prev, uint8_t *db) {
  uint8_t now = (GPIOA->IDR & GPIO_PIN_0) ? 1 : 0;
  if (now != *prev) { (*db)++; if (*db > 30) { *prev = now; *db = 0; if (now) return 1; } }
  else *db = 0;
  return 0;
}

/* ── 根据按下时长更新 TIM2 闪烁 ── */
static void update_blink(void) {
  uint32_t ms;
  if (press_end > press_start)
    ms = press_end - press_start;
  else
    ms = (0xFFFFFFFF - press_start) + press_end;  /* 回绕处理 */
  press_done = 0;

  uint16_t period;
  if      (ms < 2000)  period = 99;             /* 短按 <2s → 5Hz */
  else if (ms < 3000)  period = 249;            /* 中按 2~3s → 2Hz */
  else                 period = 499;             /* 长按 >3s → 1Hz */

  TIM2->ARR = period;
  TIM2->EGR |= TIM_EGR_UG;
  TIM2->CNT = 0;
}

/* ── TIM2: LED 闪烁 ── */
void TIM2_IRQHandler(void) {
  if (TIM2->SR & TIM_SR_UIF) {
    TIM2->SR &= ~TIM_SR_UIF;
    GPIOF->ODR ^= GPIO_PIN_10;                   /* 翻转绿灯 */
  }
}

static void tim2_init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
  TIM2->PSC  = 16000 - 1;
  TIM2->ARR  = 499;
  TIM2->DIER |= TIM_DIER_UIE;
  TIM2->CR1  |= TIM_CR1_ARPE;
  TIM2->EGR  |= TIM_EGR_UG;
  TIM2->SR   &= ~TIM_SR_UIF;
  TIM2->CR1  |= TIM_CR1_CEN;
  NVIC_SetPriority(TIM2_IRQn, 2);
  NVIC_EnableIRQ(TIM2_IRQn);
}

/* ── 主函数 ── */
int main(void) {
  /* 时钟 */
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN | RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOAEN;
  RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN;         /* EXTI 需要 SYSCFG 时钟 */

  /* PF10 推挽输出，初始灭 */
  GPIOF->MODER &= ~GPIO_MODER_MODER10;
  GPIOF->MODER |=  (1 << (10*2));
  GPIOF->BSRR = GPIO_PIN_10;

  /* PA0(KEY_UP) 下拉输入 */
  GPIOA->MODER &= ~GPIO_MODER_MODER0;
  GPIOA->PUPDR &= ~GPIO_PUPDR_PUPDR0;
  GPIOA->PUPDR |=  (2 << (0*2));                 /* 下拉 */

  /* PE4(KEY0) 下拉输入，用于 EXTI4 */
  GPIOE->MODER &= ~GPIO_MODER_MODER4;
  GPIOE->PUPDR &= ~GPIO_PUPDR_PUPDR4;
  GPIOE->PUPDR |=  (2 << (4*2));                 /* 下拉 */

  /* EXTI4: PE4, 上升沿触发 */
  SYSCFG->EXTICR[1] &= ~SYSCFG_EXTICR2_EXTI4;
  SYSCFG->EXTICR[1] |=  SYSCFG_EXTICR2_EXTI4_PE;  /* EXTI4 → PE4 */
  EXTI->IMR  |= EXTI_IMR_MR4;                      /* 不屏蔽 */
  EXTI->RTSR |= EXTI_RTSR_TR4;                     /* 上升沿触发 */
  NVIC_SetPriority(EXTI4_IRQn, 0);
  NVIC_EnableIRQ(EXTI4_IRQn);

  /* 开机闪 3 下 */
  for (int i = 0; i < 3; i++) {
    GPIOF->BSRR = (uint32_t)GPIO_PIN_10 << 16;
    delay_ms(200);
    GPIOF->BSRR = GPIO_PIN_10;
    delay_ms(200);
  }

  /* 启动 SysTick + TIM2 */
  systick_init();
  tim2_init();

  uint8_t kup=0, kud=0;
  while (1) {
    if (press_done) update_blink();

    if (keyup_edge(&kup, &kud)) {
      TIM2->ARR = 499; TIM2->EGR |= TIM_EGR_UG; TIM2->CNT = 0;
    }
  }
}
