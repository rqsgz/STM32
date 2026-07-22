/**
 * Day 20: 传感器集成 + 温度报警 + 光照舵机反馈
 *
 * 新增:
 *   1. 温度超过阈值 → 蜂鸣器(PC12)响 + PC端 ALM:T 红色闪烁
 *   2. 光照太暗 → 舵机(PC6/TIM3)自动调节补光灯角度 + ALM:L
 *
 * 接线:
 *   蜂鸣器: PC12 (低电平有效)
 *   舵机:   PC6  (TIM3_CH1, 50Hz PWM)
 */

#include "stm32f4xx.h"

/* ================================================================
 * 阈值配置 (可调)
 * ================================================================ */

#define TEMP_ALARM_C    350     /* 35.0°C, 超温报警 (单位: 0.1°C) */
#define TEMP_CLEAR_C    330     /* 33.0°C, 解除报警 (回差 2°C) */
#define LIGHT_DIM_LUX   300     /* 30.0 lux 以下 → 太暗, 舵机补光 */
#define LIGHT_OK_LUX    500     /* 50.0 lux 以上 → 正常, 舵机归位 */

/* 舵机角度 */
#define SVO_IDLE         30     /* 补光灯收起 */
#define SVO_PARTIAL      60     /* 部分补光 */
#define SVO_FULL        120     /* 全补光 */

/* 角度→CCR: CCR = 50 + angle * 200 / 180 */
#define SVO_ANGLE(a)    (uint16_t)(50 + (a) * 200 / 180)

/* ================================================================
 * DWT
 * ================================================================ */

void dwt_init(void){CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;}
uint32_t dwt_get(void){return DWT->CYCCNT;}
static void dus(uint32_t us){uint32_t s=dwt_get();while((dwt_get()-s)<us*16);}

/* ================================================================
 * USART1 + USART2
 * ================================================================ */

static void u1c(char c){while(!(USART1->SR&USART_SR_TXE));USART1->DR=(uint8_t)c;}
static void u1s(const char*s){while(*s)u1c(*s++);}
static void u2c(char c){while(!(USART2->SR&USART_SR_TXE));USART2->DR=(uint8_t)c;}
static void u2s(const char*s){while(*s)u2c(*s++);}
static void i2s(int32_t n,char*b){char t[12];int i=0,neg=0;if(n<0){neg=1;n=-n;}if(n==0)t[i++]='0';else while(n){t[i++]=(char)('0'+n%10);n/=10;}if(neg)t[i++]='-';int j=0;while(i>0)b[j++]=t[--i];b[j]='\0';}
static void u1n(int32_t n){char b[12];i2s(n,b);u1s(b);}
static char*scat(char*d,const char*s){while(*s)*d++=*s++;*d='\0';return d;}
static char*scat_i(char*d,int32_t n){char b[12];i2s(n,b);return scat(d,b);}

/* ================================================================
 * I2C (PB5=SCL, PB6=SDA)
 * ================================================================ */

#define SCL_P 5
#define SDA_P 6
static void SCL_H(void){GPIOB->BSRR=(1U<<SCL_P);}
static void SCL_L(void){GPIOB->BSRR=(uint32_t)(1U<<SCL_P)<<16;}
static void SDA_H(void){GPIOB->BSRR=(1U<<SDA_P);}
static void SDA_L(void){GPIOB->BSRR=(uint32_t)(1U<<SDA_P)<<16;}
static int SDA_R(void){return(GPIOB->IDR&(1U<<SDA_P))?1:0;}
static void i2d(void){for(volatile int i=0;i<80;i++)__NOP();}
static void i2s_start(void){SDA_H();i2d();SCL_H();i2d();SDA_L();i2d();SCL_L();}
static void i2s_stop(void){SDA_L();i2d();SCL_H();i2d();SDA_H();i2d();}
static int i2c_w(uint8_t d){int a;for(int i=0;i<8;i++,d<<=1){if(d&0x80)SDA_H();else SDA_L();i2d();SCL_H();i2d();SCL_L();i2d();}SDA_H();i2d();SCL_H();i2d();a=SDA_R();SCL_L();i2d();SDA_H();return a;}
static uint8_t i2c_r(int ack){uint8_t d=0;SDA_H();for(int i=0;i<8;i++){SCL_H();i2d();d=(uint8_t)((d<<1)|(SDA_R()?1:0));SCL_L();i2d();}if(ack)SDA_L();else SDA_H();i2d();SCL_H();i2d();SCL_L();i2d();SDA_H();return d;}
static void i2c_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;GPIOB->OTYPER|=(1<<SCL_P)|(1<<SDA_P);GPIOB->OSPEEDR|=(3<<(SCL_P*2))|(3<<(SDA_P*2));GPIOB->PUPDR&=~((3<<(SCL_P*2))|(3<<(SDA_P*2)));GPIOB->MODER|=(1<<(SCL_P*2))|(1<<(SDA_P*2));GPIOB->BSRR=(1U<<SCL_P)|(1U<<SDA_P);}

/* ================================================================
 * MPU6050 (禁用)
 * ================================================================ */

static int m_ok,m_ax,m_ay,m_az;
static void mpu_read(void){
    i2s_start();i2c_w(0x68<<1);i2c_w(0x3B);i2s_start();i2c_w((0x68<<1)|1);
    uint8_t d[6];for(int i=0;i<5;i++)d[i]=i2c_r(1);d[5]=i2c_r(0);i2s_stop();
    m_ax=(int16_t)(((uint16_t)d[0]<<8)|d[1]);m_ay=(int16_t)(((uint16_t)d[2]<<8)|d[3]);m_az=(int16_t)(((uint16_t)d[4]<<8)|d[5]);
}
static void mpu_init(void){
    i2s_start();i2c_w(0x68<<1);i2c_w(0x75);i2s_start();i2c_w((0x68<<1)|1);
    uint8_t who=i2c_r(0);i2s_stop();
    if(who!=0x68&&who!=0x00){u1s("[MPU] -\r\n");m_ok=0;return;}
    i2s_start();i2c_w(0x68<<1);i2c_w(0x6B);i2c_w(0x00);i2s_stop();
    for(volatile int i=0;i<800000;i++)__NOP();
    i2s_start();i2c_w(0x68<<1);i2c_w(0x1C);i2c_w(0x00);i2s_stop();
    mpu_read();
    u1s("[MPU] raw=");u1n(m_ax);u1s("/");u1n(m_ay);u1s("/");u1n(m_az);
    if(m_ax==0&&m_ay==0&&m_az==0){u1s(" ALL0\r\n");m_ok=0;return;}
    u1s(" OK\r\n");m_ok=1;
}

/* ================================================================
 * BH1750
 * ================================================================ */

static int bh_l;  /* 单位: 0.1 lux */
static void bh_init(void){i2s_start();i2c_w(0x23<<1);i2c_w(0x01);i2s_stop();i2s_start();i2c_w(0x23<<1);i2c_w(0x10);i2s_stop();}
static void bh_read(void){i2s_start();i2c_w((0x23<<1)|1);uint8_t hi=i2c_r(1),lo=i2c_r(0);i2s_stop();bh_l=(int)(((((uint16_t)hi<<8)|lo)*5)/6);}

/* ================================================================
 * DS18B20 (PC7)
 * ================================================================ */

static void ow_o(void){GPIOC->MODER&=~(3U<<14);GPIOC->MODER|=(1U<<14);}
static void ow_i(void){GPIOC->MODER&=~(3U<<14);}
static void ow_l(void){GPIOC->BSRR=(uint32_t)(1U<<7)<<16;}
static void ow_h(void){GPIOC->BSRR=(1U<<7);}
static int ow_r(void){return(GPIOC->IDR&(1U<<7))?1:0;}
static int ow_rst(void){int p;__disable_irq();ow_o();ow_l();dus(480);ow_i();dus(60);p=ow_r()?0:1;dus(420);__enable_irq();return p;}
static void ow_wb(uint8_t d){for(int i=0;i<8;i++){__disable_irq();ow_o();ow_l();dus(1);if(d&1)ow_h();dus(60);ow_h();dus(1);__enable_irq();d>>=1;}}
static uint8_t ow_rb(void){uint8_t d=0;for(int i=0;i<8;i++){int b;__disable_irq();ow_o();ow_l();dus(1);ow_i();dus(5);b=ow_r();dus(55);__enable_irq();d>>=1;if(b)d|=0x80;}return d;}
static void ow_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN;GPIOC->PUPDR&=~(3U<<14);GPIOC->PUPDR|=(1U<<14);ow_i();}
enum{DS_I,DS_W}ds_st;static int ds_t10,ds_cnt;
static void ds_trig(void){if(ds_st!=DS_I)return;if(!ow_rst())return;ow_wb(0xCC);ow_wb(0x44);ds_cnt=0;ds_st=DS_W;}
static void ds_poll(void){if(ds_st!=DS_W)return;if(++ds_cnt<75)return;if(!ow_rst()){ds_st=DS_I;return;}ow_wb(0xCC);ow_wb(0xBE);uint8_t d[9];for(int i=0;i<9;i++)d[i]=ow_rb();int16_t r=(int16_t)(((uint16_t)d[1]<<8)|d[0]);ds_t10=(int)((r*10)/16);ds_st=DS_I;}

/* ================================================================
 * HC-SR04 (PB0=Trig, PB2=Echo)
 * ================================================================ */

static volatile uint32_t es,ee;static volatile int eok;
void EXTI2_IRQHandler(void){if(EXTI->PR&EXTI_PR_PR2){EXTI->PR=EXTI_PR_PR2;if(GPIOB->IDR&GPIO_PIN_2)es=dwt_get();else{ee=dwt_get();eok=1;}}}
static int sr_cm;
static void sr_trig(void){GPIOB->BSRR=GPIO_PIN_0;dus(15);GPIOB->BSRR=(uint32_t)GPIO_PIN_0<<16;}
static void sr_poll(void){if(!eok)return;eok=0;uint32_t w=ee-es;sr_cm=(int)(w/(16*58));if(sr_cm>400)sr_cm=-1;}
static void sr_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;GPIOB->MODER|=(1<<(0*2));GPIOB->PUPDR&=~(3U<<(2*2));RCC->APB2ENR|=RCC_APB2ENR_SYSCFGEN;SYSCFG->EXTICR[0]|=(1<<8);EXTI->IMR|=EXTI_IMR_MR2;EXTI->RTSR|=EXTI_RTSR_TR2;EXTI->FTSR|=EXTI_FTSR_TR2;NVIC_SetPriority(EXTI2_IRQn,1);NVIC_EnableIRQ(EXTI2_IRQn);}

/* ================================================================
 * ADC (PA1)
 * ================================================================ */

static void adc_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_ADC1EN;GPIOA->MODER|=(3U<<(1*2));ADC1->SMPR2|=(7U<<3);ADC1->SQR3=1;ADC1->CR2|=ADC_CR2_ADON;for(volatile int i=0;i<100000;i++)__NOP();}
static uint16_t adc_read(void){ADC1->CR2|=ADC_CR2_SWSTART;while(!(ADC1->SR&ADC_SR_EOC));return(uint16_t)(ADC1->DR&0xFFFF);}

/* ================================================================
 * 蜂鸣器 (PC12, 低电平有效)
 * ================================================================ */

static void buzzer_init(void){
    /* GPIOC 已在 ow_init 中使能时钟, 这里只配 PC12 */
    GPIOC->MODER |= (1U << (12*2));      /* PC12 = 推挽输出 */
    GPIOC->BSRR = (1U << 12);            /* 默认关 (高电平) */
}

static void buzzer_on(void) {
    GPIOC->BSRR = (uint32_t)(1U << 12) << 16;  /* PC12=低, 蜂鸣器响 */
}

static void buzzer_off(void) {
    GPIOC->BSRR = (1U << 12);                  /* PC12=高, 蜂鸣器关 */
}

/* ================================================================
 * 舵机 (PC6, TIM3_CH1, 50Hz PWM)
 *   PSC=160-1 → 100kHz, ARR=1999 → 50Hz
 *   CCR: 50(0°) ~ 150(90°) ~ 250(180°)
 * ================================================================ */

static void servo_init(void){
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;
    /* PC6 = AF2 (TIM3_CH1) */
    GPIOC->MODER |= (2U << (6*2));
    GPIOC->AFR[0] |= (2U << (6*4));     /* AF2 */
    TIM3->PSC = 160 - 1;
    TIM3->ARR = 1999;
    TIM3->CCMR1 |= (6U << TIM_CCMR1_OC1M_Pos);  /* PWM mode 1 */
    TIM3->CCER  |= TIM_CCER_CC1E;
    TIM3->CCR1  = SVO_ANGLE(SVO_IDLE);          /* 初始: 补光灯收起 */
    TIM3->CR1   |= TIM_CR1_CEN;
}

static void servo_set_angle(int angle_deg) {
    TIM3->CCR1 = SVO_ANGLE(angle_deg);
}

/* ================================================================
 * TIM6 系统节拍
 * ================================================================ */

static volatile uint32_t tick;
void TIM6_DAC_IRQHandler(void){if(TIM6->SR&TIM_SR_UIF){TIM6->SR=~TIM_SR_UIF;tick++;}}

/* ================================================================
 * 报警状态 (全局)
 * ================================================================ */

static int alarm_temp;   /* 1=温度过高 */
static int alarm_light;  /* 1=光照太暗 */

/* ================================================================
 * 构建消息 (新增 ALM 和 SV 字段)
 *
 * 格式:
 *   [TICK] SR:XXcm L:XX.Xlux T:XX.XC ADC:XXXmV ALM:X SV:XXX\r\n
 *
 *   ALM: N=正常, T=温度报警, L=光照报警, B=两者都报警
 *   SV:  舵机当前角度 (度)
 *
 * PC端 (dashboard.py) 收到 ALM:T 或 ALM:L 或 ALM:B 时红色闪烁
 * ================================================================ */

static int build_msg(char*buf, int sv_angle){
    char*p=buf;
    p=scat(p,"[");p=scat_i(p,(int32_t)tick);p=scat(p,"] ");

    /* 超声波 */
    p=scat(p,"SR:");p=scat_i(p,sr_cm>0?sr_cm:-1);p=scat(p,"cm ");

    /* 光照 */
    p=scat(p,"L:");p=scat_i(p,bh_l/10);p=scat(p,".");p=scat_i(p,bh_l%10);p=scat(p,"lux ");

    /* 温度 */
    p=scat(p,"T:");int tp=ds_t10;
    if(tp<0){p=scat(p,"-");tp=-tp;}
    p=scat_i(p,tp/10);p=scat(p,".");p=scat_i(p,tp%10);p=scat(p,"C ");

    /* ADC 电压 */
    uint32_t mv=((uint32_t)adc_read()*3300)>>12;
    p=scat(p,"ADC:");p=scat_i(p,(int32_t)mv);p=scat(p,"mV ");

    /* 报警标志 */
    p=scat(p,"ALM:");
    if(alarm_temp && alarm_light) p=scat(p,"B");      /* Both */
    else if(alarm_temp)           p=scat(p,"T");      /* Temperature */
    else if(alarm_light)          p=scat(p,"L");      /* Light */
    else                          p=scat(p,"N");      /* Normal */

    /* 舵机角度 */
    p=scat(p," SV:");p=scat_i(p,sv_angle);p=scat(p,"\r\n");

    return(int)(p-buf);
}

/* ================================================================
 * MAIN
 * ================================================================ */

int main(void){
    dwt_init();

    /* ---- USART1 PA9 + USART2 PA2/PA3 ---- */
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_USART1EN;RCC->APB1ENR|=RCC_APB1ENR_USART2EN;
    GPIOA->MODER&=~((3U<<(9*2))|(3U<<(2*2))|(3U<<(3*2)));GPIOA->MODER|=(2U<<(9*2))|(2U<<(2*2))|(2U<<(3*2));
    GPIOA->AFR[1]&=~(0xFU<<4);GPIOA->AFR[1]|=(7U<<4);GPIOA->AFR[0]&=~((0xFU<<8)|(0xFU<<12));GPIOA->AFR[0]|=(7U<<8)|(7U<<12);
    USART1->BRR=(8<<4)|11;USART1->CR1=USART_CR1_TE|USART_CR1_UE;
    USART2->BRR=(8<<4)|11;USART2->CR1=USART_CR1_TE|USART_CR1_UE;

    /* ---- PC0 = ESP RST ---- */
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN;GPIOC->MODER|=(1U<<(0*2));GPIOC->BSRR=(1U<<0);

    /* ---- TIM6 系统节拍 (1kHz) ---- */
    RCC->APB1ENR|=RCC_APB1ENR_TIM6EN;TIM6->PSC=1599;TIM6->ARR=9;TIM6->DIER|=TIM_DIER_UIE;
    NVIC_SetPriority(TIM6_DAC_IRQn,3);NVIC_EnableIRQ(TIM6_DAC_IRQn);TIM6->CR1|=TIM_CR1_CEN;

    /* ---- IWDG 看门狗 (1秒超时) ---- */
    IWDG->KR=0x5555;
    IWDG->PR=0x04;        /* /64: 32k/64=500Hz */
    IWDG->RLR=500;        /* 500/500=1秒 */
    while(IWDG->SR&IWDG_SR_PVU);
    while(IWDG->SR&IWDG_SR_RVU);
    IWDG->KR=0xAAAA;

    /* ---- LED PF9 ---- */
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOFEN;GPIOF->MODER|=(1<<(9*2));GPIOF->BSRR=(uint32_t)GPIO_PIN_9<<16;

    /* ---- 蜂鸣器 + 舵机 ---- */
    buzzer_init();
    servo_init();

    u1s("\r\n===== Day20: Sensor + Alarm + Servo =====\r\n");
    u1s("BUZZER: PC12 | SERVO: PC6/TIM3\r\n");

    /* 闪 3 次 */
    for(int i=0;i<3;i++){GPIOF->BSRR=(uint32_t)GPIO_PIN_9<<16;for(volatile uint32_t j=0;j<800000;j++)__NOP();GPIOF->BSRR=GPIO_PIN_9;for(volatile uint32_t j=0;j<800000;j++)__NOP();}

    /* ---- I2C + 传感器 ---- */
    i2c_init();
    u1s("I2C:");
    for(uint8_t a=1;a<127;a++){
        i2s_start();int ack=i2c_w(a<<1);i2s_stop();
        if(ack==0){u1s(" 0x");u1c("0123456789ABCDEF"[a>>4]);u1c("0123456789ABCDEF"[a&0xF]);}
    }
    u1s("\r\n");
    bh_init();u1s("BH1750 done\r\n");
    u1s("[MPU] skip\r\n");m_ok=0;
    ow_init();u1s(ow_rst()?"DS18B20 OK\r\n":"DS18B20 -\r\n");ds_trig();
    sr_init();u1s("HC-SR04 OK\r\n");
    adc_init();u1s("ADC OK\r\n");

    u1s("=== GO ===\r\n");
    u1s("Temp>35C→BUZZER | Light<30lux→SERVO\r\n");

    /* ---- 初始状态 ---- */
    alarm_temp = 0;
    alarm_light = 0;
    int sv_angle = SVO_IDLE;    /* 当前舵机角度 */
    int sv_target = SVO_IDLE;   /* 目标舵机角度 */

    uint32_t lt=tick;int bt=0,ds_t=0,st=0,wt=0;
    while(1){
        uint32_t nw=tick;if(nw==lt)continue;
        int dt=(int)(nw-lt);lt=nw;
        for(int i=0;i<dt;i++)ds_poll();
        bt+=dt;ds_t+=dt;st+=dt;wt+=dt;

        /* BH1750: 每 200ms */
        if(bt>=200){bt-=200;bh_read();}

        /* HC-SR04: 每 200ms */
        if(st>=200){st-=200;sr_trig();sr_poll();}

        /* DS18B20: 每 750ms 触发转换 */
        if(ds_t>=750){ds_t-=750;ds_trig();}

        /* ================================================================
         * 报警逻辑 (每 500ms 判断一次, 和发送同步)
         * ================================================================ */

        if(wt>=500){
            wt-=500;

            /* --- 温度报警 (带回差) --- */
            if(ds_t10 > TEMP_ALARM_C) {
                if(!alarm_temp) { alarm_temp=1; buzzer_on();  u1s("[ALARM] Temperature HIGH!\r\n"); }
            } else if(ds_t10 < TEMP_CLEAR_C) {
                if(alarm_temp)  { alarm_temp=0; buzzer_off(); u1s("[OK] Temperature normal\r\n"); }
            }

            /* --- 光照检测 → 舵机角度 (带回差) --- */
            if(bh_l < LIGHT_DIM_LUX) {
                sv_target = SVO_FULL;               /* <30 lux → 全补光 */
                if(!alarm_light) { alarm_light=1; u1s("[ALARM] Light DIM! Servo→FULL\r\n"); }
            } else if(bh_l < LIGHT_OK_LUX) {
                sv_target = SVO_PARTIAL;            /* 30-50 lux → 部分补光 */
                if(alarm_light) { alarm_light=0; u1s("[OK] Light improving\r\n"); }
            } else {
                sv_target = SVO_IDLE;               /* ≥50 lux → 关闭补光 */
                if(alarm_light) { alarm_light=0; u1s("[OK] Light normal\r\n"); }
            }

            /* --- 舵机平滑过渡 (每步 5°) --- */
            if(sv_angle < sv_target) {
                sv_angle += 5;
                if(sv_angle > sv_target) sv_angle = sv_target;
                servo_set_angle(sv_angle);
            } else if(sv_angle > sv_target) {
                sv_angle -= 5;
                if(sv_angle < sv_target) sv_angle = sv_target;
                servo_set_angle(sv_angle);
            }

            /* --- 发送消息 (USART1 + USART2 → ESP8266 → PC) --- */
            char msg[256];
            build_msg(msg, sv_angle);
            u1s(msg);
            u2s(msg);
        }

        IWDG->KR=0xAAAA;  /* 喂狗 */
    }
}
