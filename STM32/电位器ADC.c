#include "stm32f4xx.h"
#include <stdio.h>

/*
 * ADC 电位器 — 用 ADC2 CH10 (PC0)
 * 接线: 电位器 OUT → PC0, VCC → 3.3V, GND → GND
 *
 * 已知: GND→1023, 3.3V→4095, 差值=3072
 * 补偿: mv = (raw - 1023) * 3300 / 3072
 */

static void delay_ms(uint32_t ms) {
  while (ms--)
    for (volatile uint32_t i = 0; i < 8000; i++) __NOP();
}

static void uart1_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  GPIOA->MODER  &= ~(GPIO_MODER_MODER9 | GPIO_MODER_MODER10);
  GPIOA->MODER  |=  (2 << (9*2)) | (2 << (10*2));
  GPIOA->AFR[1] &= ~((0xF << 4) | (0xF << 8));
  GPIOA->AFR[1] |=  (7 << 4) | (7 << 8);
  USART1->BRR = (8 << 4) | 11;
  USART1->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void uart1_putc(char c) {
  while (!(USART1->SR & USART_SR_TXE));
  USART1->DR = c;
}

int _write(int fd, const char *ptr, int len) {
  (void)fd;
  for (int i = 0; i < len; i++) {
    if (ptr[i] == '\n') uart1_putc('\r');
    uart1_putc(ptr[i]);
  }
  return len;
}

int main(void) {
  uart1_init();
  printf("\nADC Potentiometer - PC0 (ADC2 CH10)\n");
  printf("Connect: OUT→PC0  VCC→3.3V  GND→GND\n\n");

  /* ADC2 CH10 (PC0) */
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
  RCC->APB2ENR |= RCC_APB2ENR_ADC2EN;

  GPIOC->MODER |= (3 << (0*2));  /* PC0 analog */

  ADC->CCR = 0;  /* ADCPRE = /2 = 8MHz */
  ADC2->SMPR1 |= (7 << ADC_SMPR1_SMP10_Pos);  /* CH10 */
  ADC2->SQR3 = 10;
  ADC2->CR2 |= ADC_CR2_ADON;
  for (volatile int i = 0; i < 100000; i++) __NOP();

  while (1) {
    ADC2->CR2 |= ADC_CR2_SWSTART;
    while (!(ADC2->SR & ADC_SR_EOC));
    uint16_t raw = (uint16_t)(ADC2->DR & 0xFFFF);

    /* 补偿: (raw-1023)*3300/(4095-1023) */
    int32_t compensated = ((int32_t)raw - 1023) * 3300 / 3072;
    if (compensated < 0) compensated = 0;

    printf("raw:%4u  %ld mV\n", raw, compensated);
    delay_ms(200);
  }
}
