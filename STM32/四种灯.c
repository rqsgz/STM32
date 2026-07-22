#include "stm32f4xx_hal.h"

/*
 * Black F407ZG 多模式 LED Demo — 非阻塞版本
 *
 * LED: PF9(红) PF10(绿) PC6(外接) PC7(外接) — 低电平有效
 * KEY0(PE4): 切模式   KEY_UP(PA0): 回流水灯
 */

/* ──────── 引脚定义 ──────── */
typedef struct {
  GPIO_TypeDef *port;
  uint16_t      pin;
} led_t;

static const led_t leds[] = {
  { GPIOF, GPIO_PIN_9  },
  { GPIOF, GPIO_PIN_10 },
  { GPIOC, GPIO_PIN_6  },
  { GPIOC, GPIO_PIN_7  },
};
#define LED_N 4

/* ──────── 工具 ──────── */
static void led_on (int i) { HAL_GPIO_WritePin(leds[i].port, leds[i].pin, GPIO_PIN_RESET); }
static void led_off(int i) { HAL_GPIO_WritePin(leds[i].port, leds[i].pin, GPIO_PIN_SET);   }
static void all_off(void)  { for (int i = 0; i < LED_N; i++) led_off(i); }

static void delay_ms(uint32_t ms) {
  while (ms--)
    for (volatile uint32_t i = 0; i < 8000; i++) __NOP();
}

static void delay_us(uint32_t us) {
  while (us--)
    for (volatile uint32_t i = 0; i < 16; i++) __NOP();
}

/* ──────── 按键（快速消抖，适配非阻塞轮询） ──────── */
static uint8_t key_edge(void) {
  /*
   * 每 1ms 调用一次，检测 KEY0 / KEY_UP 按下瞬间
   * 返回: 1=KEY0, 2=KEY_UP, 0=无
   */
  static uint8_t  k0_prev = 0, ku_prev = 0;
  static uint8_t  k0_cnt  = 0, ku_cnt  = 0;

  uint8_t k0 = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4) ? 1 : 0;
  uint8_t ku = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) ? 1 : 0;
  uint8_t ret = 0;

  /* KEY0 消抖 */
  if (k0 != k0_prev) { k0_cnt++; if (k0_cnt > 30) { k0_prev = k0; k0_cnt = 0; if (k0) ret = 1; } }
  else               { k0_cnt = 0; }

  /* KEY_UP 消抖 */
  if (ku != ku_prev) { ku_cnt++; if (ku_cnt > 30) { ku_prev = ku; ku_cnt = 0; if (ku) ret = 2; } }
  else               { ku_cnt = 0; }

  return ret;
}

/* ──────── ① 流水灯 ──────── */
static void waterflow_step(void) {
  static uint8_t i = 0;
  static uint32_t tick = 0;

  if (tick == 0) {
    all_off();
    led_on(i);
  }
  tick++;

  if (tick >= 120) {    /* 120ms 后切下一颗 */
    tick = 0;
    i = (i + 1) % LED_N;
  }
}

/* ──────── ② 跑马灯（来回） ──────── */
static void horse_step(void) {
  static int8_t  dir  = 1;
  static int8_t  i    = 0;
  static uint32_t tick = 0;

  if (tick == 0) {
    all_off();
    led_on(i);
  }
  tick++;

  if (tick >= 120) {
    tick = 0;
    i += dir;
    if (i >= LED_N - 1) dir = -1;
    if (i <= 0)         dir =  1;
  }
}

/* ──────── ③ 呼吸灯 — 4 灯同时渐亮渐暗 ──────── */
static const uint8_t breath_table[100] = {
   0, 1, 3, 6,10,15,20,26,32,38, 45,51,57,62,67,71,75,78,80,82,
  84,85,86,87,88,88,88,88,87,86, 85,84,82,80,78,75,71,67,62,57,
  51,45,38,32,26,20,15,10, 6, 3,  1, 0, 1, 3, 6,10,15,20,26,32,
  38,45,51,57,62,67,71,75,78,80, 82,84,85,86,87,88,88,88,88,87,
  86,85,84,82,80,78,75,71,67,62, 57,51,45,38,32,26,20,15,10, 6,
};

static void breathing_step(void) {
  /*
   * 每次调用运行一个 10ms 的 PWM 周期（100Hz），
   * 每个亮度停留 20 个周期（200ms），完整呼吸周期约 2 秒。
   */
  static uint16_t table_idx = 0;
  static uint8_t  stay_cnt  = 0;

  uint8_t bright = breath_table[table_idx];

  /* 一次 PWM 周期：10ms，100 步 */
  for (int t = 0; t < 100; t++) {
    if (t < bright)
      { for (int i = 0; i < LED_N; i++) led_on(i); }
    else
      { all_off(); }
    delay_us(100);
  }

  stay_cnt++;
  if (stay_cnt >= 5) {             /* 每 5 周期 → 呼吸周期 ≈ 5s */
    stay_cnt = 0;
    table_idx = (table_idx + 1) % 100;
  }
}

/* ──────── ④ 全闪 ──────── */
static void allflash_step(void) {
  static uint8_t  state = 0;
  static uint32_t tick  = 0;

  tick++;
  if (tick >= 60) {   /* 每 60ms 翻转 */
    tick = 0;
    state = !state;
    if (state) for (int i = 0; i < LED_N; i++) led_on(i);
    else       all_off();
  }
}

/* ──────── GPIO 初始化 ──────── */
static void MX_GPIO_Init(void) {
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;

  gpio.Pin = GPIO_PIN_9;  HAL_GPIO_Init(GPIOF, &gpio);
  gpio.Pin = GPIO_PIN_10; HAL_GPIO_Init(GPIOF, &gpio);
  gpio.Pin = GPIO_PIN_6;  HAL_GPIO_Init(GPIOC, &gpio);
  gpio.Pin = GPIO_PIN_7;  HAL_GPIO_Init(GPIOC, &gpio);
  all_off();

  gpio.Mode = GPIO_MODE_INPUT;
  gpio.Pull = GPIO_PULLDOWN;

  gpio.Pin = GPIO_PIN_4;  HAL_GPIO_Init(GPIOE, &gpio);
  gpio.Pin = GPIO_PIN_0;  HAL_GPIO_Init(GPIOA, &gpio);
}

/* ──────── 主函数 ──────── */
int main(void) {
  MX_GPIO_Init();

  int mode = 0;  /* 0=流水灯 1=跑马灯 2=呼吸灯 3=全闪 */

  while (1) {
    /* ── 按键轮询（每 1ms 一次） ── */
    uint8_t key = key_edge();

    if (key == 1) {               /* KEY0 → 切模式 */
      mode = (mode + 1) % 4;
      all_off();
    }
    if (key == 2) {               /* KEY_UP → 回流水灯 */
      mode = 0;
      all_off();
    }

    /* ── 执行当前模式一步 ── */
    switch (mode) {
      case 0: waterflow_step();  break;
      case 1: horse_step();      break;
      case 2: breathing_step();  break;
      case 3: allflash_step();   break;
    }

    delay_ms(1);   /* 主循环节拍：1ms */
  }
}
