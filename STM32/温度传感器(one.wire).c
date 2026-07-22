#include "stm32f4xx.h"
#include <stdio.h>

/*
 * DS18B20 数字温度传感器 — PC7 (1-Wire)
 * 接线: - → GND, + → 3.3V, OUT → PC7
 * 模块自带 4.7kΩ 上拉电阻
 */

/* ---- DWT 精确定时 ---- */
static void delay_init(void) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
static void delay_us(uint32_t us) {
  uint32_t s = DWT->CYCCNT;
  uint32_t t = us * 16;  /* 16MHz */
  while ((DWT->CYCCNT - s) < t);
}
static void delay_ms(uint32_t ms) { while (ms--) delay_us(1000); }

/* ---- 1-Wire (PC7, 开漏输出) ---- */
#define OW_PORT GPIOC
#define OW_PIN  7
static void ow_low(void)    { OW_PORT->BSRR = (uint32_t)(1U<<OW_PIN)<<16; }
static void ow_release(void) { OW_PORT->BSRR = (1U<<OW_PIN); }
static int  ow_read(void)   { return (OW_PORT->IDR & (1U<<OW_PIN)) ? 1 : 0; }

/* ---- UART ---- */
static void uart1_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
  RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
  GPIOA->MODER  &= ~(GPIO_MODER_MODER9 | GPIO_MODER_MODER10);
  GPIOA->MODER  |=  (2<<(9*2)) | (2<<(10*2));
  GPIOA->AFR[1] &= ~((0xF<<4) | (0xF<<8));
  GPIOA->AFR[1] |=  (7<<4) | (7<<8);
  USART1->BRR = (8<<4)|11;
  USART1->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}
static void uart1_putc(char c) { while (!(USART1->SR & USART_SR_TXE)); USART1->DR = c; }
int _write(int fd, const char *p, int n) {
  (void)fd;
  for (int i = 0; i < n; i++) { if (p[i]=='\n') uart1_putc('\r'); uart1_putc(p[i]); }
  return n;
}

/* ---- 1-Wire 操作 ---- */
static void ow_init(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN;
  OW_PORT->OTYPER  |= (1<<OW_PIN);           /* 开漏 */
  OW_PORT->PUPDR   &= ~(3<<(OW_PIN*2));      /* 无内部上拉，靠模块 */
  OW_PORT->OSPEEDR |= (3<<(OW_PIN*2));       /* 高速 */
  OW_PORT->MODER   |= (1<<(OW_PIN*2));       /* 输出 */
  ow_release();
}

static int ow_reset(void) {
  ow_low();     delay_us(490);
  ow_release(); delay_us(70);
  int r = ow_read();
  delay_us(430);
  return r;
}

static void ow_write_bit(int b) {
  ow_low(); delay_us(10);
  if (b) ow_release();
  delay_us(65); ow_release(); delay_us(5);
}

static void ow_write(uint8_t d) {
  for (int i = 0; i < 8; i++, d >>= 1) ow_write_bit(d & 1);
}

static int ow_read_bit(void) {
  ow_low();      delay_us(5);
  ow_release();  delay_us(20);  /* 等总线稳定 */
  int b = ow_read();
  delay_us(55);
  return b;
}

static uint8_t ow_read_byte(void) {
  uint8_t d = 0;
  for (int i = 0; i < 8; i++) if (ow_read_bit()) d |= (1U<<i);
  return d;
}

/* ---- Main ---- */
int main(void) {
  uart1_init();
  delay_init();
  ow_init();

  printf("\n================================\n");
  printf("DS18B20 Temperature Sensor\n");
  printf("PC7 (1-Wire)\n");
  printf("================================\n\n");

  if (ow_reset()) {
    printf("ERROR: No device found!\n");
    while (1) delay_ms(1000);
  }

  while (1) {
    /* 启动转换 */
    ow_reset(); delay_us(200);
    ow_write(0xCC);    /* Skip ROM */
    ow_write(0x44);    /* Convert T */
    delay_ms(750);     /* 12-bit 最大转换时间 */

    /* 读取温度 */
    ow_reset(); delay_us(200);
    ow_write(0xCC);    /* Skip ROM */
    ow_write(0xBE);    /* Read Scratchpad */

    uint8_t buf[9];
    for (int i = 0; i < 9; i++) buf[i] = ow_read_byte();

    int16_t raw = (int16_t)((buf[1] << 8) | buf[0]);
    int32_t tx  = (int32_t)raw * 10 / 16;  /* 0.0625°C → 0.1°C */
    printf("Temp: %ld.%ld C\n", tx / 10, (tx >= 0 ? tx : -tx) % 10);

    delay_ms(500);
  }
}
