#include "stm32f4xx.h"

/*
 * SG90 舵机 — PC6 (TIM3_CH1), 50Hz PWM
 * 0°→90°→180°→90° 循环, 每步 600ms
 */

#define SERVO_0   50     /* 0.5ms */
#define SERVO_90  150    /* 1.5ms */
#define SERVO_180 250    /* 2.5ms */

static void delay_ms(uint32_t ms) {
  while (ms--)
    for (volatile uint32_t i = 0; i < 8000; i++) __NOP();
}

static void servo_set(uint16_t ccr) {
  TIM3->CCR1 = ccr;
}

static void servo_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
  RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

  /* PC6 → AF2 (TIM3_CH1) */
  GPIOC->MODER  &= ~GPIO_MODER_MODER6;
  GPIOC->MODER  |=  (2 << (6*2));
  GPIOC->AFR[0] &= ~(0xF << 24);
  GPIOC->AFR[0] |=  (2 << 24);

  /* 100kHz, ARR=1999 → 50Hz */
  TIM3->PSC   = 160 - 1;
  TIM3->ARR   = 1999;
  TIM3->CNT   = 0;

  TIM3->CCMR1 &= ~TIM_CCMR1_OC1M;
  TIM3->CCMR1 |=  (6 << TIM_CCMR1_OC1M_Pos);
  TIM3->CCMR1 |=  TIM_CCMR1_OC1PE;
  TIM3->CCER  |=  TIM_CCER_CC1E;
  TIM3->CCR1  = SERVO_0;

  TIM3->CR1 |= TIM_CR1_ARPE;
  TIM3->EGR |= TIM_EGR_UG;
  TIM3->CR1 |= TIM_CR1_CEN;
}

int main(void) {
  servo_init();

  while (1) {
    servo_set(SERVO_0);    delay_ms(600);
    servo_set(SERVO_90);   delay_ms(600);
    servo_set(SERVO_180);  delay_ms(600);
    servo_set(SERVO_90);   delay_ms(600);
  }
}
