/**
 * Day 19 最终版: 四传感器 + ESP8266 WiFi (MPU6050 禁用)
 */
#include "stm32f4xx.h"

/* ======== DWT ======== */
void dwt_init(void){CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;}
uint32_t dwt_get(void){return DWT->CYCCNT;}
static void dus(uint32_t us){uint32_t s=dwt_get();while((dwt_get()-s)<us*16);}

/* ======== USART1 + USART2 ======== */
static void u1c(char c){while(!(USART1->SR&USART_SR_TXE));USART1->DR=(uint8_t)c;}
static void u1s(const char*s){while(*s)u1c(*s++);}
static void u2c(char c){while(!(USART2->SR&USART_SR_TXE));USART2->DR=(uint8_t)c;}
static void u2s(const char*s){while(*s)u2c(*s++);}
static void i2s(int32_t n,char*b){char t[12];int i=0,neg=0;if(n<0){neg=1;n=-n;}if(n==0)t[i++]='0';else while(n){t[i++]=(char)('0'+n%10);n/=10;}if(neg)t[i++]='-';int j=0;while(i>0)b[j++]=t[--i];b[j]='\0';}
static void u1n(int32_t n){char b[12];i2s(n,b);u1s(b);}
static char*scat(char*d,const char*s){while(*s)*d++=*s++;*d='\0';return d;}
static char*scat_i(char*d,int32_t n){char b[12];i2s(n,b);return scat(d,b);}

/* ======== I2C (PB5=SCL, PB6=SDA) ======== */
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

/* ======== MPU6050 (简化: 只用加速度计) ======== */
static int m_ok,m_ax,m_ay,m_az;
static void mpu_read(void){
    i2s_start();i2c_w(0x68<<1);i2c_w(0x3B);i2s_start();i2c_w((0x68<<1)|1);
    uint8_t d[6];for(int i=0;i<5;i++)d[i]=i2c_r(1);d[5]=i2c_r(0);i2s_stop();
    m_ax=(int16_t)(((uint16_t)d[0]<<8)|d[1]);m_ay=(int16_t)(((uint16_t)d[2]<<8)|d[3]);m_az=(int16_t)(((uint16_t)d[4]<<8)|d[5]);
}
static void mpu_init(void){
    /* 先确认传感器存在 */
    i2s_start();i2c_w(0x68<<1);i2c_w(0x75);i2s_start();i2c_w((0x68<<1)|1);
    uint8_t who=i2c_r(0);i2s_stop();
    if(who!=0x68&&who!=0x00){u1s("[MPU] -\r\n");m_ok=0;return;}
    /* 唤醒 */
    i2s_start();i2c_w(0x68<<1);i2c_w(0x6B);i2c_w(0x00);i2s_stop();
    for(volatile int i=0;i<800000;i++)__NOP();
    /* 配置量程: ±2g */
    i2s_start();i2c_w(0x68<<1);i2c_w(0x1C);i2c_w(0x00);i2s_stop();
    mpu_read();
    u1s("[MPU] raw=");u1n(m_ax);u1s("/");u1n(m_ay);u1s("/");u1n(m_az);
    if(m_ax==0&&m_ay==0&&m_az==0){u1s(" ALL0\r\n");m_ok=0;return;}
    u1s(" OK\r\n");m_ok=1;
}

/* ======== BH1750 ======== */
static int bh_l;
static void bh_init(void){i2s_start();i2c_w(0x23<<1);i2c_w(0x01);i2s_stop();i2s_start();i2c_w(0x23<<1);i2c_w(0x10);i2s_stop();}
static void bh_read(void){i2s_start();i2c_w((0x23<<1)|1);uint8_t hi=i2c_r(1),lo=i2c_r(0);i2s_stop();bh_l=(int)(((((uint16_t)hi<<8)|lo)*5)/6);}

/* ======== DS18B20 (PC7) ======== */
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

/* ======== HC-SR04 (PB0=Trig, PB2=Echo) ======== */
static volatile uint32_t es,ee;static volatile int eok;
void EXTI2_IRQHandler(void){if(EXTI->PR&EXTI_PR_PR2){EXTI->PR=EXTI_PR_PR2;if(GPIOB->IDR&GPIO_PIN_2)es=dwt_get();else{ee=dwt_get();eok=1;}}}
static int sr_cm;
static void sr_trig(void){GPIOB->BSRR=GPIO_PIN_0;dus(15);GPIOB->BSRR=(uint32_t)GPIO_PIN_0<<16;}
static void sr_poll(void){if(!eok)return;eok=0;uint32_t w=ee-es;sr_cm=(int)(w/(16*58));if(sr_cm>400)sr_cm=-1;}
static void sr_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;GPIOB->MODER|=(1<<(0*2));GPIOB->PUPDR&=~(3U<<(2*2));RCC->APB2ENR|=RCC_APB2ENR_SYSCFGEN;SYSCFG->EXTICR[0]|=(1<<8);EXTI->IMR|=EXTI_IMR_MR2;EXTI->RTSR|=EXTI_RTSR_TR2;EXTI->FTSR|=EXTI_FTSR_TR2;NVIC_SetPriority(EXTI2_IRQn,1);NVIC_EnableIRQ(EXTI2_IRQn);}

/* ======== ADC (PA1) ======== */
static void adc_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_ADC1EN;GPIOA->MODER|=(3U<<(1*2));ADC1->SMPR2|=(7U<<3);ADC1->SQR3=1;ADC1->CR2|=ADC_CR2_ADON;for(volatile int i=0;i<100000;i++)__NOP();}
static uint16_t adc_read(void){ADC1->CR2|=ADC_CR2_SWSTART;while(!(ADC1->SR&ADC_SR_EOC));return(uint16_t)(ADC1->DR&0xFFFF);}

/* ======== TIM6 ======== */
static volatile uint32_t tick;
void TIM6_DAC_IRQHandler(void){if(TIM6->SR&TIM_SR_UIF){TIM6->SR=~TIM_SR_UIF;tick++;}}

/* ======== 构建消息 ======== */
static int build_msg(char*buf){
    char*p=buf;
    p=scat(p,"[");p=scat_i(p,(int32_t)tick);p=scat(p,"] ");
    p=scat(p,"SR:");p=scat_i(p,sr_cm>0?sr_cm:-1);p=scat(p,"cm ");
    p=scat(p,"L:");p=scat_i(p,bh_l/10);p=scat(p,".");p=scat_i(p,bh_l%10);p=scat(p,"lux ");
    p=scat(p,"T:");int tp=ds_t10;if(tp<0){p=scat(p,"-");tp=-tp;}p=scat_i(p,tp/10);p=scat(p,".");p=scat_i(p,tp%10);p=scat(p,"C ");
    uint32_t mv=((uint32_t)adc_read()*3300)>>12;p=scat(p,"ADC:");p=scat_i(p,(int32_t)mv);p=scat(p,"mV\r\n");
    return(int)(p-buf);
}

/* ======== MAIN ======== */
int main(void){
    dwt_init();

    /* USART1 PA9 + USART2 PA2/PA3 */
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_USART1EN;RCC->APB1ENR|=RCC_APB1ENR_USART2EN;
    GPIOA->MODER&=~((3U<<(9*2))|(3U<<(2*2))|(3U<<(3*2)));GPIOA->MODER|=(2U<<(9*2))|(2U<<(2*2))|(2U<<(3*2));
    GPIOA->AFR[1]&=~(0xFU<<4);GPIOA->AFR[1]|=(7U<<4);GPIOA->AFR[0]&=~((0xFU<<8)|(0xFU<<12));GPIOA->AFR[0]|=(7U<<8)|(7U<<12);
    USART1->BRR=(8<<4)|11;USART1->CR1=USART_CR1_TE|USART_CR1_UE;
    USART2->BRR=(8<<4)|11;USART2->CR1=USART_CR1_TE|USART_CR1_UE;

    /* PC0 = ESP RST */
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN;GPIOC->MODER|=(1U<<(0*2));GPIOC->BSRR=(1U<<0);

    /* TIM6 */
    RCC->APB1ENR|=RCC_APB1ENR_TIM6EN;TIM6->PSC=1599;TIM6->ARR=9;TIM6->DIER|=TIM_DIER_UIE;
    NVIC_SetPriority(TIM6_DAC_IRQn,3);NVIC_EnableIRQ(TIM6_DAC_IRQn);TIM6->CR1|=TIM_CR1_CEN;

    /* IWDG 看门狗: LSI~32kHz, /64预分频=500Hz, 重载500=1秒超时 */
    IWDG->KR=0x5555;      /* 解锁 */
    IWDG->PR=0x04;        /* /64: 32k/64=500Hz */
    IWDG->RLR=500;        /* 500/500=1秒 */
    while(IWDG->SR&IWDG_SR_PVU);  /* 等预分频更新 */
    while(IWDG->SR&IWDG_SR_RVU);  /* 等重载更新 */
    IWDG->KR=0xAAAA;      /* 喂狗+启动 */
    u1s("IWDG enabled\r\n");

    /* LED PF9 亮一下证明程序启动 */
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOFEN;GPIOF->MODER|=(1<<(9*2));GPIOF->BSRR=(uint32_t)GPIO_PIN_9<<16;
    u1s("\r\n=== Day19 WiFi ===\r\n");
    /* 闪 3 次表示程序在跑 */
    for(int i=0;i<3;i++){GPIOF->BSRR=(uint32_t)GPIO_PIN_9<<16;for(volatile uint32_t j=0;j<800000;j++)__NOP();GPIOF->BSRR=GPIO_PIN_9;for(volatile uint32_t j=0;j<800000;j++)__NOP();}
    i2c_init();
    /* I2C 扫描 */
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

    uint32_t lt=tick;int bt=0,ds_t=0,st=0,wt=0;
    while(1){
        uint32_t nw=tick;if(nw==lt)continue;
        int dt=(int)(nw-lt);lt=nw;
        for(int i=0;i<dt;i++)ds_poll();
        bt+=dt;ds_t+=dt;st+=dt;wt+=dt;

        if(bt>=200){bt-=200;bh_read();}
        if(st>=200){st-=200;sr_trig();sr_poll();}
        if(ds_t>=750){ds_t-=750;ds_trig();}

        if(wt>=500){
            wt-=500;char msg[256];
            build_msg(msg);u1s(msg);u2s(msg);
        }
        IWDG->KR=0xAAAA;  /* 喂狗 */
    }
}
