#include "stm32f4xx.h"
#include <stdio.h>

/*
 * printf → USART1 PA9(TX) → ATK-DAP → COM12
 *
 * 接线: ATK-DAP RX → PA9, ATK-DAP GND → GND
 * 串口助手: COM12, 115200-8-N-1, DTR=ON
 *
 * UART 帧格式 (115200-8-N-1):
 *   起始位 1bit(低) + 数据 8bit(LSB先) + 停止位 1bit(高)
 *   无校验位
 *   波特率 = 16MHz / (16 × 8.6875) = 115107 ≈ 115200, 误差 -0.08%
 */

static void delay_ms(uint32_t ms) {
  while (ms--)
    for (volatile uint32_t i = 0; i < 8000; i++) __NOP();
}

static void uart1_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

  /* PA9 → AF7 (USART1 TX) */
  GPIOA->MODER  &= ~GPIO_MODER_MODER9;
  GPIOA->MODER  |=  (2 << (9*2));
  GPIOA->AFR[1] &= ~(0xF << 4);
  GPIOA->AFR[1] |=  (7 << 4);

  /* PA10 → AF7 (USART1 RX, 备用) */
  GPIOA->MODER  &= ~GPIO_MODER_MODER10;
  GPIOA->MODER  |=  (2 << (10*2));
  GPIOA->AFR[1] &= ~(0xF << 8);
  GPIOA->AFR[1] |=  (7 << 8);

  /* 波特率 115200: BRR = (8<<4)|11 */
  USART1->BRR = (8 << 4) | 11;
  USART1->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

static void uart1_putc(char c) {
  while (!(USART1->SR & USART_SR_TXE));  /* 等发送寄存器空 */
  USART1->DR = c;
}

/* printf 重定向: printf → _write → uart1_putc → USART1 → ATK-DAP → PC */
int _write(int fd, const char *ptr, int len) {
  (void)fd;
  for (int i = 0; i < len; i++) {
    if (ptr[i] == '\n') uart1_putc('\r');  /* \n → \r\n */
    uart1_putc(ptr[i]);
  }
  return len;
}

int main(void) {
  /* LED 心跳 */
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOFEN;
  GPIOF->MODER |= (1 << (9*2)) | (1 << (10*2));
  GPIOF->BSRR = GPIO_PIN_9 | GPIO_PIN_10;

  uart1_init();

  printf("\n================================\n");
  printf("Hello STM32!\n");
  printf("通信: USART1 PA9 → ATK-DAP → COM12\n");
  printf("参数: 115200-8-N-1\n");
  printf("================================\n\n");

  uint32_t count = 0;

  while (1) {
    printf("Count: %lu\n", count++);

    /* 心跳灯 */
    GPIOF->BSRR = (uint32_t)GPIO_PIN_9 << 16;   /* 红亮 */
    GPIOF->BSRR = GPIO_PIN_10;                   /* 绿灭 */
    delay_ms(500);
    GPIOF->BSRR = GPIO_PIN_9;                    /* 红灭 */
    GPIOF->BSRR = (uint32_t)GPIO_PIN_10 << 16;   /* 绿亮 */
    delay_ms(500);
  }
}
