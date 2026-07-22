#include "stm32f4xx_hal.h"

/* Black F407ZG: LED0=PF9(红), LED1=PF10(绿), 低电平有效 */

static void delay_ms(uint32_t ms)
{
  /* HSI=16MHz, 粗略延时: 每ms约 8000 个循环 */
  while (ms--) {
    for (volatile uint32_t i = 0; i < 8000; i++) {
      __NOP();
    }
  }
}

int main(void)
{
  /* 使能 GPIOF 时钟 */
  __HAL_RCC_GPIOF_CLK_ENABLE();

  /* 初始化 PF9 (红灯) 和 PF10 (绿灯) 为推挽输出 */
  GPIO_InitTypeDef gpio = {0};
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;

  gpio.Pin = GPIO_PIN_9;
  HAL_GPIO_Init(GPIOF, &gpio);

  gpio.Pin = GPIO_PIN_10;
  HAL_GPIO_Init(GPIOF, &gpio);

  /* 初始: 两灯都灭 (高电平) */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9  | GPIO_PIN_10, GPIO_PIN_SET);

  while (1)
  {
    /* 红灯亮, 绿灯灭 */
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9,  GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_SET);
    delay_ms(500);

    /* 红灯灭, 绿灯亮 */
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9,  GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_RESET);
    delay_ms(500);
  }
}
