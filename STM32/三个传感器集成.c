/**
 * 多传感器采集 — BH1750(I2C) + DS18B20(OneWire) + 光敏(ADC)
 * 接线: BH1750 SCL→PB5 SDA→PB6 | DS18B20 OUT→PC7 | 光敏 AO→PA1
 * 输出: 串口每秒 "L:xxx.xlux T:xx.xC A:xxxxmV"
 */

#include "stm32f4xx.h"
#include <string.h>

/* ================================================================
 * DWT
 * ================================================================ */
void dwt_init(void) { CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT=0; DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk; }
uint32_t dwt_get(void) { return DWT->CYCCNT; }

/* ================================================================
 * USART1 调试输出 (115200, PA9=TX)
 * ================================================================ */
static void u1c(char c) { while(!(USART1->SR&USART_SR_TXE)); USART1->DR=(uint8_t)c; }
static void u1s(const char *s) { while(*s) u1c(*s++); }
static void i2s(int32_t n, char *b) {
    char t[12]; int i=0,neg=0;
    if(n<0){neg=1;n=-n;} if(n==0)t[i++]='0';
    else while(n){t[i++]=(char)('0'+n%10);n/=10;}
    if(neg)t[i++]='-'; int j=0; while(i>0)b[j++]=t[--i]; b[j]='\0';
}

/* ================================================================
 * BH1750 I2C 位带驱动 (PB5=SCL, PB6=SDA)
 * ================================================================ */
#define SCL_PORT GPIOB
#define SCL_PIN  5
#define SDA_PORT GPIOB
#define SDA_PIN  6

static void i2c_dly(void) { for(volatile int i=0;i<5;i++)__NOP(); }
static void SCL_H(void) { SCL_PORT->BSRR=(1U<<SCL_PIN); }
static void SCL_L(void) { SCL_PORT->BSRR=(uint32_t)(1U<<SCL_PIN)<<16; }
static void SDA_H(void) { SDA_PORT->BSRR=(1U<<SDA_PIN); }
static void SDA_L(void) { SDA_PORT->BSRR=(uint32_t)(1U<<SDA_PIN)<<16; }
static int  SDA_R(void)  { return (SDA_PORT->IDR&(1U<<SDA_PIN))?1:0; }

static void i2c_init(void) {
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;
    SCL_PORT->OTYPER|=(1<<SCL_PIN)|(1<<SDA_PIN);
    SCL_PORT->OSPEEDR|=(3<<(SCL_PIN*2))|(3<<(SDA_PIN*2));
    SCL_PORT->PUPDR&=~((3<<(SCL_PIN*2))|(3<<(SDA_PIN*2)));
    SCL_PORT->MODER|=(1<<(SCL_PIN*2))|(1<<(SDA_PIN*2));
    SCL_PORT->BSRR=(1U<<SCL_PIN)|(1U<<SDA_PIN);
}
static void i2c_start(void) { SDA_H();i2c_dly();SCL_H();i2c_dly();SDA_L();i2c_dly();SCL_L(); }
static void i2c_stop(void)  { SDA_L();i2c_dly();SCL_H();i2c_dly();SDA_H();i2c_dly(); }
static int i2c_write(uint8_t d) {
    for(int i=0;i<8;i++,d<<=1){ if(d&0x80)SDA_H();else SDA_L(); i2c_dly();SCL_H();i2c_dly();SCL_L();i2c_dly(); }
    SDA_H();i2c_dly();SCL_H();i2c_dly(); int a=SDA_R()?1:0; SCL_L();i2c_dly();SDA_H(); return a;
}
static uint8_t i2c_read(int ack) {
    uint8_t d=0; SDA_H();
    for(int i=0;i<8;i++){ SCL_H();i2c_dly(); d=(uint8_t)((d<<1)|(SDA_R()?1:0)); SCL_L();i2c_dly(); }
    if(ack)SDA_L();else SDA_H(); i2c_dly();SCL_H();i2c_dly();SCL_L();i2c_dly();SDA_H(); return d;
}

#define BH1750_ADDR 0x23
static int bh_lux10 = 0;

static void bh_init(void) {
    i2c_init();
    i2c_start();i2c_write(BH1750_ADDR<<1);i2c_write(0x01);i2c_stop();  /* power on */
    i2c_start();i2c_write(BH1750_ADDR<<1);i2c_write(0x10);i2c_stop();  /* continuous hres */
}
static void bh_read(void) {
    i2c_start();i2c_write((BH1750_ADDR<<1)|1);
    uint8_t hi=i2c_read(1),lo=i2c_read(0); i2c_stop();
    uint16_t r=((uint16_t)hi<<8)|lo; bh_lux10=(int)((r*5)/6);  /* lux*10 */
}

/* ================================================================
 * DS18B20 OneWire 驱动 (PC7)
 * ================================================================ */
#define OW_PORT GPIOC
#define OW_PIN  7

static void ow_dus(uint32_t us) { uint32_t s=dwt_get(); while((dwt_get()-s)<us*16); }
static void ow_out(void){ OW_PORT->MODER&=~(3U<<(OW_PIN*2));OW_PORT->MODER|=(1U<<(OW_PIN*2)); }
static void ow_inp(void){ OW_PORT->MODER&=~(3U<<(OW_PIN*2)); }
static void ow_lo(void){ OW_PORT->BSRR=(uint32_t)(1U<<OW_PIN)<<16; }
static void ow_hi(void){ OW_PORT->BSRR=(1U<<OW_PIN); }
static int  ow_rd(void){ return (OW_PORT->IDR&(1U<<OW_PIN))?1:0; }

static void ow_init(void) {
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN;
    OW_PORT->PUPDR&=~(3U<<(OW_PIN*2));OW_PORT->PUPDR|=(1U<<(OW_PIN*2)); ow_inp();
}
static int ow_rst(void) {
    int p;__disable_irq();ow_out();ow_lo();ow_dus(480);ow_inp();ow_dus(60);p=ow_rd()?0:1;ow_dus(420);__enable_irq();return p;
}
static void ow_wb(uint8_t d){ for(int i=0;i<8;i++){__disable_irq();ow_out();ow_lo();ow_dus(1);if(d&1)ow_hi();ow_dus(60);ow_hi();ow_dus(1);__enable_irq();d>>=1;} }
static uint8_t ow_rb(void){ uint8_t d=0;for(int i=0;i<8;i++){int b;__disable_irq();ow_out();ow_lo();ow_dus(1);ow_inp();ow_dus(5);b=ow_rd();ow_dus(55);__enable_irq();d>>=1;if(b)d|=0x80;}return d; }

enum { DS_IDLE, DS_WAIT } ds_st;
static int ds_t10=0, ds_ok=0, ds_cnt=0;

static void ds_trig(void) {
    if(ds_st!=DS_IDLE)return; if(!ow_rst())return;
    ow_wb(0xCC);ow_wb(0x44);ds_cnt=0;ds_st=DS_WAIT;
}
static void ds_poll(void) {
    if(ds_st!=DS_WAIT)return; ds_cnt++;
    if(ds_cnt<75)return;
    if(!ow_rst()){ds_st=DS_IDLE;return;}
    ow_wb(0xCC);ow_wb(0xBE);
    uint8_t d[9];for(int i=0;i<9;i++)d[i]=ow_rb();
    int16_t r=(int16_t)(((uint16_t)d[1]<<8)|d[0]);ds_t10=(int)((r*10)/16);ds_ok=1;ds_st=DS_IDLE;
}

/* ================================================================
 * TIM6: 100Hz (10ms/tick)
 * ================================================================ */
static volatile uint32_t g_tick;
void TIM6_DAC_IRQHandler(void) { if(TIM6->SR&TIM_SR_UIF){TIM6->SR=~TIM_SR_UIF;g_tick++;} }
static void tim6_init(void) {
    RCC->APB1ENR|=RCC_APB1ENR_TIM6EN;TIM6->PSC=1599;TIM6->ARR=99;TIM6->DIER|=TIM_DIER_UIE;
    NVIC_SetPriority(TIM6_DAC_IRQn,3);NVIC_EnableIRQ(TIM6_DAC_IRQn);TIM6->CR1|=TIM_CR1_CEN;
}

/* ================================================================
 * ADC: PA1 (ADC1_IN1)
 * ================================================================ */
static void adc_init(void) {
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_ADC1EN;
    GPIOA->MODER|=(3U<<(1*2));ADC1->SMPR2|=(7U<<3);ADC1->SQR3=1;ADC1->CR2|=ADC_CR2_ADON;
    for(volatile int i=0;i<100000;i++)__NOP();
}
static uint16_t adc_read(void) { ADC1->CR2|=ADC_CR2_SWSTART;while(!(ADC1->SR&ADC_SR_EOC));return(uint16_t)(ADC1->DR&0xFFFF); }

/* ================================================================
 * 输出格式
 * ================================================================ */
static void print(int l10, int t10, uint16_t adc) {
    char b[64],n[12];
    strcpy(b,"L:");i2s(l10/10,n);strcat(b,n);strcat(b,".");i2s(l10%10,n);strcat(b,n);strcat(b,"lux ");
    strcat(b,"T:");if(t10<0){strcat(b,"-");t10=-t10;}i2s(t10/10,n);strcat(b,n);strcat(b,".");i2s(t10%10,n);strcat(b,n);strcat(b,"C ");
    uint32_t mv=((uint32_t)adc*3300)>>12;
    strcat(b,"A:");i2s((int32_t)mv,n);strcat(b,n);strcat(b,"mV\r\n");
    u1s(b);
}

/* ================================================================
 * MAIN
 * ================================================================ */
int main(void) {
    dwt_init();

    /* USART1 */
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_USART1EN;
    GPIOA->MODER&=~(3U<<(9*2));GPIOA->MODER|=(2U<<(9*2));
    GPIOA->AFR[1]&=~(0xFU<<4);GPIOA->AFR[1]|=(7U<<4);
    USART1->BRR=(8<<4)|11;USART1->CR1=USART_CR1_TE|USART_CR1_UE;
    u1s("\r\n=== Multi-Sensor ===\r\n");

    bh_init(); u1s("BH1750 OK\r\n");
    ow_init(); u1s(ow_rst()?"DS18B20 found\r\n":"DS18B20 NOT found\r\n");
    ds_trig();
    adc_init(); u1s("ADC OK\r\n");
    tim6_init(); u1s("GO\r\n");

    uint32_t lt=g_tick;
    int bh_t=0,ds_t=0,pr_t=0;
    uint16_t av=0;

    while(1) {
        uint32_t nw=g_tick; if(nw==lt)continue;
        int dt=(int)(nw-lt); lt=nw;
        for(int i=0;i<dt;i++)ds_poll();
        bh_t+=dt;ds_t+=dt;pr_t+=dt;
        if(bh_t%10==0)av=adc_read();
        if(bh_t>=20){bh_t-=20;bh_read();}
        if(ds_t>=200){ds_t-=200;ds_trig();}
        if(pr_t>=100){pr_t-=100;print(bh_lux10,ds_t10,av);}
    }
}
