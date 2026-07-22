#include "stm32f4xx.h"

/*
 * 定时器中断 — PF10 绿灯精确闪烁
 * KEY0: 1Hz→2Hz→5Hz    KEY_UP: 回 1Hz
 *
 * TIM2 直接寄存器操作，TIM2 在 APB1 上
 * APB1 时钟 = HSI 16MHz（上电默认）
 * PSC=16000-1, ARR=499 → 中断周期 = 500ms → 1Hz 闪烁
 */

static volatile int cur_hz = 0;                        /* 0=1Hz, 1=2Hz, 2=5Hz */
static const uint16_t arr[] = { 499, 249, 99 };        /* 500ms/250ms/100ms */

/* ── 简单延时 ── */
static void delay_ms(uint32_t ms) {
  while (ms--)
    for (volatile uint32_t i = 0; i < 8000; i++) __NOP();
}

/* ── 按键消抖 ── */
static int key_edge(GPIO_TypeDef *port, uint16_t pin,
                    uint8_t *prev, uint8_t *db) {
  uint8_t now = (port->IDR & pin) ? 1 : 0;
  if (now != *prev) { (*db)++; if (*db > 30) { *prev = now; *db = 0; if (now) return 1; } }
  else *db = 0;
  return 0;
}

/* ── 定时器初始化（纯寄存器） ── */
static void timer_init(void) {
  RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;     /* TIM2 时钟 */

  TIM2->PSC = 16000 - 1;                    /* 预分频: 16MHz/16000 = 1kHz */
  TIM2->ARR = arr[0];                       /* 自动重载: 500ms */
  TIM2->DIER |= TIM_DIER_UIE;              /* 使能更新中断 */
  TIM2->CR1 |= TIM_CR1_ARPE;               /* 自动重载预装载 */
  TIM2->EGR |= TIM_EGR_UG;                 /* 生成更新事件，加载影子寄存器 */
  TIM2->SR &= ~TIM_SR_UIF;                 /* 清除中断标志 */

  NVIC_SetPriority(TIM2_IRQn, 1);
  NVIC_EnableIRQ(TIM2_IRQn);

  TIM2->CR1 |= TIM_CR1_CEN;                /* 启动定时器 */
}

/* ── 切频率 ── */
static void timer_set_hz(int idx) {
  cur_hz = idx;
  TIM2->ARR = arr[idx];
  TIM2->EGR |= TIM_EGR_UG;                 /* 立即更新 */
  TIM2->CNT = 0;
}

/* ── TIM2 中断服务函数 ── */
void TIM2_IRQHandler(void) {
  if (TIM2->SR & TIM_SR_UIF) {              /* 更新中断 */
    TIM2->SR &= ~TIM_SR_UIF;                /* 清标志 */
    GPIOF->ODR ^= GPIO_PIN_10;              /* 翻转 PF10（绿灯） */
  }
}

/* ── 主函数 ── */
int main(void) {
  /* GPIOF 时钟 */
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN | RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_GPIOAEN;

  /* PF10 推挽输出，初始灭 (高电平，低电平有效) */
  GPIOF->MODER &= ~GPIO_MODER_MODER10;
  GPIOF->MODER |=  (1 << (10*2));
  GPIOF->BSRR = GPIO_PIN_10;                /* 灭 */

  /* PE4(KEY0), PA0(KEY_UP) 下拉输入 */
  GPIOE->MODER &= ~GPIO_MODER_MODER4;       /* 输入模式 */
  GPIOE->PUPDR &= ~GPIO_PUPDR_PUPDR4;
  GPIOE->PUPDR |=  (2 << (4*2));            /* 下拉 */
  GPIOA->MODER &= ~GPIO_MODER_MODER0;
  GPIOA->PUPDR &= ~GPIO_PUPDR_PUPDR0;
  GPIOA->PUPDR |=  (2 << (0*2));            /* 下拉 */

  /* 开机闪 3 下 */
  for (int i = 0; i < 3; i++) {
    GPIOF->BSRR = (uint32_t)GPIO_PIN_10 << 16;   /* 亮 */
    delay_ms(200);
    GPIOF->BSRR = GPIO_PIN_10;                    /* 灭 */
    delay_ms(200);
  }

  /* 启动定时器 */
  timer_init();

  uint8_t k0p=0,k0d=0,kup=0,kud=0;
  while (1) {
    if (key_edge(GPIOE, GPIO_PIN_4, &k0p, &k0d)) {
      timer_set_hz((cur_hz + 1) % 3);
    }
    if (key_edge(GPIOA, GPIO_PIN_0, &kup, &kud)) {
      timer_set_hz(0);
    }
  }
}
