/**
 * 五传感器 + ESP8266 WiFi 透传
 * USART1 → COM12 调试, USART2 → ESP → UDP → NetAssist
 */
#include "stm32f4xx.h"

/* ======== DWT ======== */
void dwt_init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
uint32_t dwt_get(void) { return DWT->CYCCNT; }
static void dus(uint32_t us) {
    uint32_t s = dwt_get(); while ((dwt_get() - s) < us * 16);
}

/* ======== USART1 调试 + USART2 ESP ======== */
static void u1c(char c) { while (!(USART1->SR & USART_SR_TXE)); USART1->DR = (uint8_t)c; }
static void u1s(const char *s) { while (*s) u1c(*s++); }
static void u2c(char c) { while (!(USART2->SR & USART_SR_TXE)); USART2->DR = (uint8_t)c; }
static void u2s(const char *s) { while (*s) u2c(*s++); }
static void i2s(int32_t n, char *b) {
    char t[12]; int i = 0, neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) t[i++] = '0';
    else while (n) { t[i++] = (char)('0' + n % 10); n /= 10; }
    if (neg) t[i++] = '-';
    int j = 0; while (i > 0) b[j++] = t[--i]; b[j] = '\0';
}
static char *scat(char *d, const char *s) { while (*s) *d++ = *s++; *d = '\0'; return d; }
static char *scat_i(char *d, int32_t n) { char b[12]; i2s(n, b); return scat(d, b); }

/* ======== I2C (PB5=SCL, PB6=SDA) ======== */
#define S_P 5
#define D_P 6
static void sc_h(void) { GPIOB->BSRR = (1U << S_P); }
static void sc_l(void) { GPIOB->BSRR = (uint32_t)(1U << S_P) << 16; }
static void da_h(void) { GPIOB->BSRR = (1U << D_P); }
static void da_l(void) { GPIOB->BSRR = (uint32_t)(1U << D_P) << 16; }
static int  da_r(void) { return (GPIOB->IDR & (1U << D_P)) ? 1 : 0; }
static void i2d(void) { for (volatile int i = 0; i < 8; i++) __NOP(); }
static void ik_start(void) { da_h(); i2d(); sc_h(); i2d(); da_l(); i2d(); sc_l(); }
static void ik_stop(void)  { da_l(); i2d(); sc_h(); i2d(); da_h(); i2d(); }
static int ik_w(uint8_t d) { int a; for (int i = 0; i < 8; i++, d <<= 1) { if (d & 0x80) da_h(); else da_l(); i2d(); sc_h(); i2d(); sc_l(); i2d(); } da_h(); i2d(); sc_h(); i2d(); a = da_r(); sc_l(); i2d(); da_h(); return a; }
static uint8_t ik_r(int ack) { uint8_t d = 0; da_h(); for (int i = 0; i < 8; i++) { sc_h(); i2d(); d = (uint8_t)((d << 1) | (da_r() ? 1 : 0)); sc_l(); i2d(); } if (ack) da_l(); else da_h(); i2d(); sc_h(); i2d(); sc_l(); i2d(); da_h(); return d; }
static void i2c_init(void) { RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; GPIOB->OTYPER |= (1 << S_P) | (1 << D_P); GPIOB->OSPEEDR |= (3 << (S_P * 2)) | (3 << (D_P * 2)); GPIOB->PUPDR &= ~((3 << (S_P * 2)) | (3 << (D_P * 2))); GPIOB->MODER |= (1 << (S_P * 2)) | (1 << (D_P * 2)); GPIOB->BSRR = (1U << S_P) | (1U << D_P); }
static void ik_reg_w(uint8_t a, uint8_t r, uint8_t v) { ik_start(); ik_w(a << 1); ik_w(r); ik_w(v); ik_stop(); }
static uint8_t ik_reg_r(uint8_t a, uint8_t r) { uint8_t v; ik_start(); ik_w(a << 1); ik_w(r); ik_start(); ik_w((a << 1) | 1); v = ik_r(0); ik_stop(); return v; }
static void ik_buf_r(uint8_t a, uint8_t r, uint8_t *b, int n) { ik_start(); ik_w(a << 1); ik_w(r); ik_start(); ik_w((a << 1) | 1); for (int i = 0; i < n - 1; i++) b[i] = ik_r(1); b[n - 1] = ik_r(0); ik_stop(); }

/* ======== BH1750 ======== */
static int bh_l;
static void bh_init(void) { ik_start(); ik_w(0x23 << 1); ik_w(0x01); ik_stop(); ik_start(); ik_w(0x23 << 1); ik_w(0x10); ik_stop(); }
static void bh_read(void) { ik_start(); ik_w((0x23 << 1) | 1); uint8_t hi = ik_r(1), lo = ik_r(0); ik_stop(); bh_l = (int)(((((uint16_t)hi << 8) | lo) * 5) / 6); }

/* ======== MPU6050 ======== */
static float fatan2(float y, float x) { float ay = y < 0 ? -y : y, ang; if (x >= 0) { float r = (x - ay) / (x + ay); ang = 0.78539816f - 0.78539816f * r; } else { float r = (x + ay) / (ay - x); ang = 2.35619449f - 0.78539816f * r; } return y < 0 ? -ang : ang; }
static float fsqrt(float x) { return __builtin_sqrtf(x); }
static float mR, mP, mY, gxb, gyb, gzb; static int m_ok;
static void mpu_raw(int16_t *ax, int16_t *ay, int16_t *az, int16_t *gx, int16_t *gy, int16_t *gz) { uint8_t d[14]; ik_buf_r(0x68, 0x3B, d, 14); *ax = (int16_t)(((uint16_t)d[0] << 8) | d[1]); *ay = (int16_t)(((uint16_t)d[2] << 8) | d[3]); *az = (int16_t)(((uint16_t)d[4] << 8) | d[5]); *gx = (int16_t)(((uint16_t)d[8] << 8) | d[9]); *gy = (int16_t)(((uint16_t)d[10] << 8) | d[11]); *gz = (int16_t)(((uint16_t)d[12] << 8) | d[13]); }
static void mpu_init(void) { if (ik_reg_r(0x68, 0x75) != 0x68) { u1s("[MPU] -\r\n"); m_ok = 0; return; } ik_reg_w(0x68, 0x6B, 0x01); ik_reg_w(0x68, 0x1B, 0x18); ik_reg_w(0x68, 0x1C, 0x00); ik_reg_w(0x68, 0x1A, 0x03); ik_reg_w(0x68, 0x19, 0x04); float g1 = 0, g2 = 0, g3 = 0; for (int i = 0; i < 100; i++) { int16_t ax, ay, az, gx, gy, gz; mpu_raw(&ax, &ay, &az, &gx, &gy, &gz); g1 += gx; g2 += gy; g3 += gz; dus(5000); } gxb = g1 / 100.0f; gyb = g2 / 100.0f; gzb = g3 / 100.0f; m_ok = 1; u1s("[MPU] OK\r\n"); }
static void mpu_upd(float dt) { if (!m_ok) return; int16_t ax, ay, az, gx, gy, gz; mpu_raw(&ax, &ay, &az, &gx, &gy, &gz); float gr = (gx - gxb) / 16.384f, gp = (gy - gyb) / 16.384f, gy2 = (gz - gzb) / 16.384f; float ar2 = fatan2(ay / 16384.0f, az / 16384.0f), ap2 = fatan2(-ax / 16384.0f, fsqrt((ay / 16384.0f) * (ay / 16384.0f) + (az / 16384.0f) * (az / 16384.0f))); mR = 0.98f * (mR + gr * dt) + 0.02f * ar2; mP = 0.98f * (mP + gp * dt) + 0.02f * ap2; mY += gy2 * dt; mR *= 57.29578f; mP *= 57.29578f; mY *= 57.29578f; }

/* ======== DS18B20 (PC7) ======== */
static void ow_o(void) { GPIOC->MODER &= ~(3U << 14); GPIOC->MODER |= (1U << 14); }
static void ow_i(void) { GPIOC->MODER &= ~(3U << 14); }
static void ow_l(void) { GPIOC->BSRR = (uint32_t)(1U << 7) << 16; }
static void ow_h(void) { GPIOC->BSRR = (1U << 7); }
static int  ow_r(void) { return (GPIOC->IDR & (1U << 7)) ? 1 : 0; }
static int ow_rst(void) { int p; __disable_irq(); ow_o(); ow_l(); dus(480); ow_i(); dus(60); p = ow_r() ? 0 : 1; dus(420); __enable_irq(); return p; }
static void ow_wb(uint8_t d) { for (int i = 0; i < 8; i++) { __disable_irq(); ow_o(); ow_l(); dus(1); if (d & 1) ow_h(); dus(60); ow_h(); dus(1); __enable_irq(); d >>= 1; } }
static uint8_t ow_rb(void) { uint8_t d = 0; for (int i = 0; i < 8; i++) { int b; __disable_irq(); ow_o(); ow_l(); dus(1); ow_i(); dus(5); b = ow_r(); dus(55); __enable_irq(); d >>= 1; if (b) d |= 0x80; } return d; }
static void ow_init(void) { RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN; GPIOC->PUPDR &= ~(3U << 14); GPIOC->PUPDR |= (1U << 14); ow_i(); }
enum { DS_I, DS_W } ds_st; static int ds_t10, ds_cnt;
static void ds_trig(void) { if (ds_st != DS_I) return; if (!ow_rst()) return; ow_wb(0xCC); ow_wb(0x44); ds_cnt = 0; ds_st = DS_W; }
static void ds_poll(void) { if (ds_st != DS_W) return; if (++ds_cnt < 75) return; if (!ow_rst()) { ds_st = DS_I; return; } ow_wb(0xCC); ow_wb(0xBE); uint8_t d[9]; for (int i = 0; i < 9; i++) d[i] = ow_rb(); int16_t r = (int16_t)(((uint16_t)d[1] << 8) | d[0]); ds_t10 = (int)((r * 10) / 16); ds_st = DS_I; }

/* ======== HC-SR04 (PB0=Trig, PB2=Echo) ======== */
static volatile uint32_t es, ee; static volatile int eok;
void EXTI2_IRQHandler(void) { if (EXTI->PR & EXTI_PR_PR2) { EXTI->PR = EXTI_PR_PR2; if (GPIOB->IDR & GPIO_PIN_2) es = dwt_get(); else { ee = dwt_get(); eok = 1; } } }
static int sr_cm;
static void sr_trig(void) { GPIOB->BSRR = GPIO_PIN_0; dus(15); GPIOB->BSRR = (uint32_t)GPIO_PIN_0 << 16; }
static void sr_poll(void) { if (!eok) return; eok = 0; uint32_t w = ee - es; sr_cm = (int)(w / (16 * 58)); if (sr_cm > 400) sr_cm = -1; }
static void sr_init(void) { RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN; GPIOB->MODER |= (1 << (0 * 2)); GPIOB->PUPDR &= ~(3U << (2 * 2)); RCC->APB2ENR |= RCC_APB2ENR_SYSCFGEN; SYSCFG->EXTICR[0] |= (1 << 8); EXTI->IMR |= EXTI_IMR_MR2; EXTI->RTSR |= EXTI_RTSR_TR2; EXTI->FTSR |= EXTI_FTSR_TR2; NVIC_SetPriority(EXTI2_IRQn, 1); NVIC_EnableIRQ(EXTI2_IRQn); }

/* ======== ADC (PA1) ======== */
static void adc_init(void) { RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN; RCC->APB2ENR |= RCC_APB2ENR_ADC1EN; GPIOA->MODER |= (3U << (1 * 2)); ADC1->SMPR2 |= (7U << 3); ADC1->SQR3 = 1; ADC1->CR2 |= ADC_CR2_ADON; for (volatile int i = 0; i < 100000; i++) __NOP(); }
static uint16_t adc_read(void) { ADC1->CR2 |= ADC_CR2_SWSTART; while (!(ADC1->SR & ADC_SR_EOC)); return (uint16_t)(ADC1->DR & 0xFFFF); }

/* ======== TIM6 ======== */
static volatile uint32_t tick;
void TIM6_DAC_IRQHandler(void) { if (TIM6->SR & TIM_SR_UIF) { TIM6->SR = ~TIM_SR_UIF; tick++; } }

/* ======== 构建消息 ======== */
static int build_msg(char *buf) {
    char *p = buf;
    p = scat(p, "["); p = scat_i(p, (int32_t)tick); p = scat(p, "] ");
    p = scat(p, "SR:"); p = scat_i(p, sr_cm > 0 ? sr_cm : -1); p = scat(p, "cm ");
    p = scat(p, "L:"); p = scat_i(p, bh_l / 10); p = scat(p, "."); p = scat_i(p, bh_l % 10); p = scat(p, "lux ");
    p = scat(p, "MPU:"); p = scat_i(p, (int32_t)mR); p = scat(p, "/"); p = scat_i(p, (int32_t)mP); p = scat(p, "/"); p = scat_i(p, (int32_t)mY); p = scat(p, " ");
    p = scat(p, "T:"); int tp = ds_t10; if (tp < 0) { p = scat(p, "-"); tp = -tp; } p = scat_i(p, tp / 10); p = scat(p, "."); p = scat_i(p, tp % 10); p = scat(p, "C ");
    uint32_t mv = ((uint32_t)adc_read() * 3300) >> 12; p = scat(p, "ADC:"); p = scat_i(p, (int32_t)mv); p = scat(p, "mV\r\n");
    return (int)(p - buf);
}

/* ======== MAIN ======== */
int main(void) {
    dwt_init();

    /* USART1 调试 (PA9) */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    GPIOA->MODER &= ~(3U << (9 * 2)); GPIOA->MODER |= (2U << (9 * 2));
    GPIOA->AFR[1] &= ~(0xFU << 4); GPIOA->AFR[1] |= (7U << 4);
    USART1->BRR = (8 << 4) | 11; USART1->CR1 = USART_CR1_TE | USART_CR1_UE;

    /* USART2 ESP (PA2=TX, PA3=RX) */
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    GPIOA->MODER &= ~((3U << (2 * 2)) | (3U << (3 * 2)));
    GPIOA->MODER |= (2U << (2 * 2)) | (2U << (3 * 2));
    GPIOA->AFR[0] &= ~((0xFU << 8) | (0xFU << 12)); GPIOA->AFR[0] |= (7U << 8) | (7U << 12);
    USART2->BRR = (8 << 4) | 11; USART2->CR1 = USART_CR1_TE | USART_CR1_UE;

    /* PC0 = ESP RST (HIGH) */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOCEN; GPIOC->MODER |= (1U << (0 * 2)); GPIOC->BSRR = (1U << 0);

    /* TIM6 */
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
    TIM6->PSC = 1599; TIM6->ARR = 9; TIM6->DIER |= TIM_DIER_UIE;
    NVIC_SetPriority(TIM6_DAC_IRQn, 3); NVIC_EnableIRQ(TIM6_DAC_IRQn);
    TIM6->CR1 |= TIM_CR1_CEN;

    u1s("\r\n=== Day19 WiFi ===\r\n");

    /* 传感器 */
    i2c_init(); bh_init(); u1s("BH1750 OK\r\n");
    mpu_init();
    ow_init(); u1s(ow_rst() ? "DS18B20 OK\r\n" : "DS18B20 -\r\n"); ds_trig();
    sr_init(); u1s("HC-SR04 OK\r\n");
    adc_init(); u1s("ADC OK\r\n");
    u1s("=== GO ===\r\n");

    uint32_t lt = tick;
    int bt = 0, ds_t = 0, mt = 0, st = 0, wt = 0;

    while (1) {
        uint32_t nw = tick; if (nw == lt) continue;
        int dt = (int)(nw - lt); lt = nw;
        for (int i = 0; i < dt; i++) ds_poll();
        bt += dt; ds_t += dt; mt += dt; st += dt; wt += dt;

        if (bt >= 200) { bt -= 200; bh_read(); }
        while (mt >= 2) { mt -= 2; mpu_upd(0.02f); }
        if (st >= 200) { st -= 200; sr_trig(); sr_poll(); }
        if (ds_t >= 750) { ds_t -= 750; ds_trig(); }

        if (wt >= 500) {
            wt -= 500;
            char msg[256];
            build_msg(msg);
            u1s(msg);  /* 调试串口 */
            u2s(msg);  /* → ESP → WiFi → NetAssist */
        }
    }
}
