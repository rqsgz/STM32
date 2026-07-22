#include "stm32f4xx.h"
#include <stdio.h>

/*
 * LDR 光敏 — AO → PC0 (ADC2 CH10)
 * 亮度越高 → 电压越低 → mV 越小
 */

#define ADC_GND    1023
#define ADC_VCC    4095
#define ADC_RANGE  (ADC_VCC - ADC_GND)

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

static void adc_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
  RCC->APB2ENR |= RCC_APB2ENR_ADC2EN;
  GPIOC->MODER |= (3 << (0*2));
  ADC->CCR = 0;
  ADC2->SMPR1 |= (7 << ADC_SMPR1_SMP10_Pos);
  ADC2->SQR3 = 10;
  ADC2->CR2 |= ADC_CR2_ADON;
  for (volatile int i = 0; i < 100000; i++) __NOP();
}

static uint16_t adc_read(void) {
  ADC2->CR2 |= ADC_CR2_SWSTART;
  while (!(ADC2->SR & ADC_SR_EOC));
  return (uint16_t)(ADC2->DR & 0xFFFF);
}

int main(void) {
  uart1_init();
  adc_init();
  printf("\nLight Sensor - PC0\n\n");

  while (1) {
    uint16_t raw = adc_read();
    int32_t compensated = ((int32_t)raw - ADC_GND) * 3300 / ADC_RANGE;
    if (compensated < 0) compensated = 0;

    printf("Light: %ld mV\n", compensated);
    delay_ms(300);
  }
}
