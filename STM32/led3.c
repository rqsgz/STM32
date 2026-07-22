#include "stm32f4xx_hal.h"

/*
 * Black F407ZG 引脚定义
 * LED0: PF9  红灯, 低电平有效
 * LED1: PF10 绿灯, 低电平有效
 * KEY0: PE4  按键, 按下为高电平 (下拉)
 * KEY_UP: PA0 按键, 按下为高电平 (下拉)
 */

/* ──────────── 延时函数 (HSI=16MHz) ──────────── */
static void delay_ms(uint32_t ms)
{
  while (ms--) {
    for (volatile uint32_t i = 0; i < 8000; i++) {
      __NOP();
    }
  }
}

/* ──────────── 流水灯 ──────────── */
static void demo_led_flow(void)
{
  /*
   * 两个 LED 依次点亮：
   *   红灯亮 → 红灯灭 → 绿灯亮 → 绿灯灭 → 循环
   */

  /* 全灭 */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_SET);

  /* ① 红灯亮、绿灯灭 */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9,  GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_SET);
  delay_ms(500);

  /* ① 红灯灭 */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9, GPIO_PIN_SET);
  delay_ms(200);

  /* ② 绿灯亮、红灯灭 */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9,  GPIO_PIN_SET);
  delay_ms(500);

  /* ② 绿灯灭 */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_10, GPIO_PIN_SET);
  delay_ms(200);
}

/* ──────────── 按键控制 LED ──────────── */
static void demo_key_toggle(void)
{
  /*
   * KEY0  按一下 → 红灯翻转（亮→灭 / 灭→亮）
   * KEY_UP 按一下 → 绿灯翻转
   *
   * 使用边沿检测 + 消抖：只在"松开→按下"的瞬间触发一次
   */

  static uint8_t  key0_prev  = 0;   /* 上一次的按键状态 */
  static uint8_t  keyup_prev = 0;
  static uint32_t key0_debounce  = 0;   /* 消抖计时 */
  static uint32_t keyup_debounce = 0;

  uint8_t key0_now  = (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) == GPIO_PIN_SET) ? 1 : 0;
  uint8_t keyup_now = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET) ? 1 : 0;

  /* ── KEY0 消抖 + 边沿检测 ── */
  if (key0_now != key0_prev) {
    key0_debounce++;                              /* 状态变化，开始计数 */
    if (key0_debounce > 10) {                     /* 稳定约 10ms */
      key0_prev = key0_now;
      key0_debounce = 0;
      if (key0_now == 1) {                        /* 按下瞬间 */
        HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_9);    /* 红灯翻转！ */
      }
    }
  } else {
    key0_debounce = 0;
  }

  /* ── KEY_UP 消抖 + 边沿检测 ── */
  if (keyup_now != keyup_prev) {
    keyup_debounce++;
    if (keyup_debounce > 10) {
      keyup_prev = keyup_now;
      keyup_debounce = 0;
      if (keyup_now == 1) {
        HAL_GPIO_TogglePin(GPIOF, GPIO_PIN_10);   /* 绿灯翻转！ */
      }
    }
  } else {
    keyup_debounce = 0;
  }

  delay_ms(1);  /* 主循环延时 1ms，用于消抖基准 */
}

/* ──────────── GPIO 初始化 ──────────── */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef gpio = {0};

  /* 时钟 */
  __HAL_RCC_GPIOF_CLK_ENABLE();   /* LED */
  __HAL_RCC_GPIOE_CLK_ENABLE();   /* KEY0 */
  __HAL_RCC_GPIOA_CLK_ENABLE();   /* KEY_UP */

  /* ── PF9, PF10: 推挽输出 (LED) ── */
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;

  gpio.Pin = GPIO_PIN_9;
  HAL_GPIO_Init(GPIOF, &gpio);

  gpio.Pin = GPIO_PIN_10;
  HAL_GPIO_Init(GPIOF, &gpio);

  /* 初始全灭 */
  HAL_GPIO_WritePin(GPIOF, GPIO_PIN_9 | GPIO_PIN_10, GPIO_PIN_SET);

  /* ── PE4: 下拉输入 (KEY0, 按下为高) ── */
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Pin  = GPIO_PIN_4;
  HAL_GPIO_Init(GPIOE, &gpio);

  /* ── PA0: 下拉输入 (KEY_UP, 按下为高) ── */
  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLDOWN;
  gpio.Pin  = GPIO_PIN_0;
  HAL_GPIO_Init(GPIOA, &gpio);
}

/* ──────────── 主函数 ──────────── */
int main(void)
{
  MX_GPIO_Init();

  while (1)
  {
    /*
     * 选择演示模式（取消注释你要用的那一行）：
     */
    // demo_led_flow();       /* ① 流水灯 */
    demo_key_toggle();    /* ② 按键翻转 */
  }
}
