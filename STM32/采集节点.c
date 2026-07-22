/**
 * 全部传感器: BH1750+MPU6050+DS18B20+HC-SR04+ADC + OLED + 按键 + 蓝牙帧
 */
#include "stm32f4xx.h"
#include <string.h>

/* ===== DWT ===== */
void dwt_init(void){CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;}
uint32_t dwt_get(void){return DWT->CYCCNT;}
static void dus(uint32_t us){uint32_t s=dwt_get();while((dwt_get()-s)<us*16);}

/* ===== USART1 ===== */
static void u1c(char c){while(!(USART1->SR&USART_SR_TXE));USART1->DR=(uint8_t)c;}
static void u1s(const char*s){while(*s)u1c(*s++);}
static void i2s_i(int32_t n,char*b){char t[12];int i=0,neg=0;if(n<0){neg=1;n=-n;}if(n==0)t[i++]='0';else while(n){t[i++]=(char)('0'+n%10);n/=10;}if(neg)t[i++]='-';int j=0;while(i>0)b[j++]=t[--i];b[j]='\0';}

/* ===== I2C 传感器总线 (PB5=SCL PB6=SDA) — 已验证MPU正常 ===== */
#define SCL_P 5
#define SDA_P 6
static void SCL_H(void){GPIOB->BSRR=(1U<<SCL_P);}
static void SCL_L(void){GPIOB->BSRR=(uint32_t)(1U<<SCL_P)<<16;}
static void SDA_H(void){GPIOB->BSRR=(1U<<SDA_P);}
static void SDA_L(void){GPIOB->BSRR=(uint32_t)(1U<<SDA_P)<<16;}
static int SDA_R(void){return(GPIOB->IDR&(1U<<SDA_P))?1:0;}
static void i2d(void){for(volatile int i=0;i<8;i++)__NOP();}
static void i2s_start(void){SDA_H();i2d();SCL_H();i2d();SDA_L();i2d();SCL_L();}
static void i2s_stop(void){SDA_L();i2d();SCL_H();i2d();SDA_H();i2d();}
static int i2c_w(uint8_t d){int a;for(int i=0;i<8;i++,d<<=1){if(d&0x80)SDA_H();else SDA_L();i2d();SCL_H();i2d();SCL_L();i2d();}SDA_H();i2d();SCL_H();i2d();a=SDA_R();SCL_L();i2d();SDA_H();return a;}
static uint8_t i2c_r(int ack){uint8_t d=0;SDA_H();for(int i=0;i<8;i++){SCL_H();i2d();d=(uint8_t)((d<<1)|(SDA_R()?1:0));SCL_L();i2d();}if(ack)SDA_L();else SDA_H();i2d();SCL_H();i2d();SCL_L();i2d();SDA_H();return d;}
static void i2c_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;GPIOB->OTYPER|=(1<<SCL_P)|(1<<SDA_P);GPIOB->OSPEEDR|=(3<<(SCL_P*2))|(3<<(SDA_P*2));GPIOB->PUPDR&=~((3<<(SCL_P*2))|(3<<(SDA_P*2)));GPIOB->MODER|=(1<<(SCL_P*2))|(1<<(SDA_P*2));GPIOB->BSRR=(1U<<SCL_P)|(1U<<SDA_P);}
static void i2c_reg_w(uint8_t adr,uint8_t reg,uint8_t val){i2s_start();i2c_w(adr<<1);i2c_w(reg);i2c_w(val);i2s_stop();}
static uint8_t i2c_reg_r(uint8_t adr,uint8_t reg){uint8_t v;i2s_start();i2c_w(adr<<1);i2c_w(reg);i2s_start();i2c_w((adr<<1)|1);v=i2c_r(0);i2s_stop();return v;}
static void i2c_buf_r(uint8_t adr,uint8_t reg,uint8_t*b,int n){i2s_start();i2c_w(adr<<1);i2c_w(reg);i2s_start();i2c_w((adr<<1)|1);for(int i=0;i<n-1;i++)b[i]=i2c_r(1);b[n-1]=i2c_r(0);i2s_stop();}

/* ===== BH1750 ===== */
static int bh_l=0;
static void bh_init(void){i2s_start();i2c_w(0x23<<1);i2c_w(0x01);i2s_stop();i2s_start();i2c_w(0x23<<1);i2c_w(0x10);i2s_stop();}
static void bh_read(void){i2s_start();i2c_w((0x23<<1)|1);uint8_t hi=i2c_r(1),lo=i2c_r(0);i2s_stop();uint16_t r=((uint16_t)hi<<8)|lo;bh_l=(int)((r*5)/6);}

/* ===== MPU6050 ===== */
static float fatan2(float y,float x){float ay=y<0?-y:y,ang;if(x>=0){float r=(x-ay)/(x+ay);ang=0.78539816f-0.78539816f*r;}else{float r=(x+ay)/(ay-x);ang=2.35619449f-0.78539816f*r;}return y<0?-ang:ang;}
static float fsqrt(float x){return __builtin_sqrtf(x);}
static float mR,mP,mY,gxb,gyb,gzb;static int m_ok,m_ax,m_ay,m_az;
static void mpu_raw(int16_t*ax,int16_t*ay,int16_t*az,int16_t*gx,int16_t*gy,int16_t*gz){uint8_t d[14];i2c_buf_r(0x68,0x3B,d,14);*ax=(int16_t)(((uint16_t)d[0]<<8)|d[1]);*ay=(int16_t)(((uint16_t)d[2]<<8)|d[3]);*az=(int16_t)(((uint16_t)d[4]<<8)|d[5]);*gx=(int16_t)(((uint16_t)d[8]<<8)|d[9]);*gy=(int16_t)(((uint16_t)d[10]<<8)|d[11]);*gz=(int16_t)(((uint16_t)d[12]<<8)|d[13]);}
static int mpu_init(void){if(i2c_reg_r(0x68,0x75)!=0x68){u1s("[MPU] not found\r\n");return-1;}i2c_reg_w(0x68,0x6B,0x01);i2c_reg_w(0x68,0x1B,0x18);i2c_reg_w(0x68,0x1C,0x00);i2c_reg_w(0x68,0x1A,0x03);i2c_reg_w(0x68,0x19,0x04);float g1=0,g2=0,g3=0;for(int i=0;i<100;i++){int16_t ax,ay,az,gx,gy,gz;mpu_raw(&ax,&ay,&az,&gx,&gy,&gz);g1+=gx;g2+=gy;g3+=gz;dus(5000);}gxb=g1/100.0f;gyb=g2/100.0f;gzb=g3/100.0f;m_ok=1;u1s("[MPU] ready\r\n");return 0;}
static void mpu_upd(float dt){if(!m_ok)return;int16_t ax,ay,az,gx,gy,gz;mpu_raw(&ax,&ay,&az,&gx,&gy,&gz);m_ax=ax;m_ay=ay;m_az=az;float gr=(gx-gxb)/16.384f,gp=(gy-gyb)/16.384f,gy2=(gz-gzb)/16.384f;float ar=ax/16384.0f,ap=ay/16384.0f,ay3=az/16384.0f;float ar2=fatan2(ap,ay3),ap2=fatan2(-ar,fsqrt(ap*ap+ay3*ay3));mR=0.98f*(mR+gr*dt)+0.02f*ar2;mP=0.98f*(mP+gp*dt)+0.02f*ap2;mY+=gy2*dt;mR*=57.29578f;mP*=57.29578f;mY*=57.29578f;}

/* ===== DS18B20 ===== */
static void ow_out(void){GPIOC->MODER&=~(3U<<14);GPIOC->MODER|=(1U<<14);}
static void ow_inp(void){GPIOC->MODER&=~(3U<<14);}
static void ow_lo(void){GPIOC->BSRR=(uint32_t)(1U<<7)<<16;}
static void ow_hi(void){GPIOC->BSRR=(1U<<7);}
static int ow_rd(void){return(GPIOC->IDR&(1U<<7))?1:0;}
static int ow_rst(void){int p;__disable_irq();ow_out();ow_lo();dus(480);ow_inp();dus(60);p=ow_rd()?0:1;dus(420);__enable_irq();return p;}
static void ow_wb(uint8_t d){for(int i=0;i<8;i++){__disable_irq();ow_out();ow_lo();dus(1);if(d&1)ow_hi();dus(60);ow_hi();dus(1);__enable_irq();d>>=1;}}
static uint8_t ow_rb(void){uint8_t d=0;for(int i=0;i<8;i++){int b;__disable_irq();ow_out();ow_lo();dus(1);ow_inp();dus(5);b=ow_rd();dus(55);__enable_irq();d>>=1;if(b)d|=0x80;}return d;}
static void ow_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN;GPIOC->PUPDR&=~(3U<<14);GPIOC->PUPDR|=(1U<<14);ow_inp();}
enum{DS_I,DS_W}ds_st;static int ds_t10,ds_cnt;
static void ds_trig(void){if(ds_st!=DS_I)return;if(!ow_rst())return;ow_wb(0xCC);ow_wb(0x44);ds_cnt=0;ds_st=DS_W;}
static void ds_poll(void){if(ds_st!=DS_W)return;ds_cnt++;if(ds_cnt<75)return;if(!ow_rst()){ds_st=DS_I;return;}ow_wb(0xCC);ow_wb(0xBE);uint8_t d[9];for(int i=0;i<9;i++)d[i]=ow_rb();int16_t r=(int16_t)(((uint16_t)d[1]<<8)|d[0]);ds_t10=(int)((r*10)/16);ds_st=DS_I;}

/* ===== HC-SR04 ===== */
static volatile uint32_t es,ee;static volatile int eok;
void EXTI2_IRQHandler(void){if(EXTI->PR&EXTI_PR_PR2){EXTI->PR=EXTI_PR_PR2;if(GPIOB->IDR&GPIO_PIN_2)es=dwt_get();else{ee=dwt_get();eok=1;}}}
static int sr_cm;
static void sr_trig(void){GPIOB->BSRR=GPIO_PIN_0;dus(15);GPIOB->BSRR=(uint32_t)GPIO_PIN_0<<16;}
static void sr_poll(void){if(!eok)return;eok=0;uint32_t w=ee-es;sr_cm=(int)(w/(16*58));if(sr_cm>400)sr_cm=-1;}
static void sr_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;GPIOB->MODER|=(1<<(0*2));GPIOB->PUPDR&=~(3U<<(2*2));RCC->APB2ENR|=RCC_APB2ENR_SYSCFGEN;SYSCFG->EXTICR[0]|=(1<<8);EXTI->IMR|=EXTI_IMR_MR2;EXTI->RTSR|=EXTI_RTSR_TR2;EXTI->FTSR|=EXTI_FTSR_TR2;NVIC_SetPriority(EXTI2_IRQn,1);NVIC_EnableIRQ(EXTI2_IRQn);}

/* ===== ADC ===== */
static void adc_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_ADC1EN;GPIOA->MODER|=(3U<<(1*2));ADC1->SMPR2|=(7U<<3);ADC1->SQR3=1;ADC1->CR2|=ADC_CR2_ADON;for(volatile int i=0;i<100000;i++)__NOP();}
static uint16_t adc_read(void){ADC1->CR2|=ADC_CR2_SWSTART;while(!(ADC1->SR&ADC_SR_EOC));return(uint16_t)(ADC1->DR&0xFFFF);}

/* ===== I2C OLED (PC8=SCL PC9=SDA) ===== */
static void o8_H(void){GPIOC->BSRR=(1U<<8);}
static void o8_L(void){GPIOC->BSRR=(uint32_t)(1U<<8)<<16;}
static void o9_H(void){GPIOC->BSRR=(1U<<9);}
static void o9_L(void){GPIOC->BSRR=(uint32_t)(1U<<9)<<16;}
static int o9_R(void){return(GPIOC->IDR&(1U<<9))?1:0;}
static void oi2c_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN;GPIOC->OTYPER|=(1<<8)|(1<<9);GPIOC->OSPEEDR|=(3<<16)|(3<<18);GPIOC->PUPDR&=~((3<<16)|(3<<18));GPIOC->MODER|=(1<<16)|(1<<18);GPIOC->BSRR=(1U<<8)|(1U<<9);}
static void o_start(void){o9_H();i2d();o8_H();i2d();o9_L();i2d();o8_L();}
static void o_stop(void){o9_L();i2d();o8_H();i2d();o9_H();i2d();}
static int o_w(uint8_t d){int a;for(int i=0;i<8;i++,d<<=1){if(d&0x80)o9_H();else o9_L();i2d();o8_H();i2d();o8_L();i2d();}o9_H();i2d();o8_H();i2d();a=o9_R();o8_L();i2d();o9_H();return a;}

#define O_ADDR 0x3C
static void o_cmd(uint8_t c){o_start();o_w(O_ADDR<<1);o_w(0x00);o_w(c);o_stop();}
static void o_dat(uint8_t*b,int n){o_start();o_w(O_ADDR<<1);o_w(0x40);for(int i=0;i<n;i++)o_w(b[i]);o_stop();}
static uint8_t obuf[1024];
static void o_init(void){oi2c_init();dus(100000);uint8_t i[]={0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,0x8D,0x14,0x20,0x00,0xA1,0xC8,0xDA,0x12,0x81,0xCF,0xD9,0xF1,0xDB,0x40,0xA4,0xA6,0xAF};for(int j=0;j<(int)sizeof(i);j++)o_cmd(i[j]);}
static void o_clr(void){memset(obuf,0,sizeof(obuf));}
static void o_show(void){for(int p=0;p<8;p++){o_cmd(0xB0+p);o_cmd(0x00);o_cmd(0x10);o_dat(obuf+p*128,128);}}
static void o_px(int x,int y,int c){if(x<0||x>127||y<0||y>63)return;if(c)obuf[x+(y/8)*128]|=(1<<(y%8));else obuf[x+(y/8)*128]&=~(1<<(y%8));}
static const uint8_t f5x7[][5]={/* 95 chars */
{0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},{0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},{0x00,0x08,0x14,0x22,0x41},{0x14,0x14,0x14,0x14,0x14},{0x41,0x22,0x14,0x08,0x00},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},{0x3E,0x41,0x41,0x51,0x32},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x00,0x7F,0x41,0x41},{0x02,0x04,0x08,0x10,0x20},{0x41,0x41,0x7F,0x00,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},{0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},{0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x08,0x14,0x54,0x54,0x3C},{0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x00,0x7F,0x10,0x28,0x44},{0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},{0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},{0x08,0x04,0x08,0x10,0x08},
};
static void o_ch(int x,int y,char c){if(c<32||c>126)c=' ';const uint8_t*f=f5x7[c-32];for(int col=0;col<5;col++){uint8_t l=f[col];for(int row=0;row<7;row++)o_px(x+col,y+row,(l>>row)&1);}}
static void o_str(int x,int y,const char*s){while(*s){o_ch(x,y,*s++);x+=6;}}

/* ===== OLED 页面 ===== */
static int o_page=0;
static void o_draw(void){
    char b[32];char n[12];o_clr();
    if(o_page==0){strcpy(b,"LIGHT:");i2s_i(bh_l/10,n);strcat(b,n);strcat(b,".");i2s_i(bh_l%10,n);strcat(b,n);strcat(b," lux");o_str(0,10,b);}
    else if(o_page==1){strcpy(b,"MPUaX:");i2s_i(m_ax,n);strcat(b,n);strcat(b," aY:");i2s_i(m_ay,n);strcat(b,n);o_str(0,10,b);strcpy(b,"aZ:");i2s_i(m_az,n);strcat(b,n);o_str(0,24,b);}
    else if(o_page==2){strcpy(b,"TEMP:");int t=ds_t10;if(t<0)t=-t;i2s_i(t/10,n);strcat(b,n);strcat(b,".");i2s_i(t%10,n);strcat(b,n);strcat(b," C");o_str(0,10,b);}
    else if(o_page==3){strcpy(b,"DIST:");i2s_i(sr_cm,n);strcat(b,n);strcat(b," cm");o_str(0,10,b);}
    else if(o_page==4){uint32_t mv=((uint32_t)adc_read()*3300)>>12;strcpy(b,"ADC:");i2s_i((int32_t)mv,n);strcat(b,n);strcat(b," mV");o_str(0,10,b);}
    o_show();
}

/* ===== 按键 (PE4) + 蜂鸣器 (PC12) ===== */
static void btn_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOEEN;GPIOE->PUPDR|=(1U<<(4*2));}
static int btn_read(void){return(GPIOE->IDR&(1U<<4))?1:0;}
static void buz_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN;GPIOC->MODER|=(1U<<(12*2));}
static void buz_on(void){GPIOC->BSRR=(uint32_t)(1U<<12)<<16;}
static void buz_off(void){GPIOC->BSRR=(1U<<12);}

/* ===== HC05 UART3 ===== */
static void hc05_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;RCC->APB1ENR|=RCC_APB1ENR_USART3EN;GPIOB->MODER&=~((3U<<(10*2))|(3U<<(11*2)));GPIOB->MODER|=(2U<<(10*2))|(2U<<(11*2));GPIOB->AFR[1]&=~((0xFU<<8)|(0xFU<<12));GPIOB->AFR[1]|=(7U<<8)|(7U<<12);USART3->BRR=0x683;USART3->CR1=USART_CR1_TE|USART_CR1_RE|USART_CR1_UE;RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN;GPIOC->MODER|=(1U<<(0*2));GPIOC->BSRR=(1U<<0);}

/* ===== 协议帧 ===== */
#define F_SOF 0x5A
#define F_EOF 0xA5
static uint16_t crc16(uint16_t crc,uint8_t b){crc^=(uint16_t)b<<8;for(int i=0;i<8;i++)crc=(crc&0x8000)?(uint16_t)((crc<<1)^0x1021):(uint16_t)(crc<<1);return crc;}
static uint8_t* tlv(uint8_t*b,uint8_t t,const void*d,uint8_t len){*b++=t;*b++=len;memcpy(b,d,len);return b+len;}
static void hc05_send(uint8_t*b,int n){for(int i=0;i<n;i++){while(!(USART3->SR&USART_SR_TXE));USART3->DR=b[i];}}
static int frame_pack(uint8_t*buf,uint8_t seq,uint32_t tms){
    buf[0]=F_SOF;buf[1]=0x01;buf[2]=0x01;buf[4]=0;buf[5]=0;buf[6]=seq;buf[7]=(uint8_t)(tms>>24);buf[8]=(uint8_t)(tms>>16);buf[9]=(uint8_t)(tms>>8);buf[10]=(uint8_t)tms;
    uint8_t*p=buf+11;
    uint16_t dm=(uint16_t)(sr_cm>0?sr_cm*10:0);p=tlv(p,0x01,&dm,2);
    uint16_t lx=(uint16_t)(bh_l/10);p=tlv(p,0x02,&lx,2);
    int16_t md[3]={(int16_t)(mR*100),(int16_t)(mP*100),(int16_t)(mY*100)};p=tlv(p,0x03,md,6);
    int16_t tp=(int16_t)ds_t10;p=tlv(p,0x05,&tp,2);
    uint16_t av=adc_read();p=tlv(p,0x06,&av,2);
    uint16_t plen=(uint16_t)(p-(buf+11));buf[3]=(uint8_t)plen;buf[4]=(uint8_t)(plen>>8);
    uint16_t crc=0;for(int i=0;i<11+plen;i++)crc=crc16(crc,buf[i]);*p++=(uint8_t)(crc&0xFF);*p++=(uint8_t)(crc>>8);*p++=F_EOF;
    return (int)(p-buf);
}

/* ===== TIM6 ===== */
static volatile uint32_t tick;
void TIM6_DAC_IRQHandler(void){if(TIM6->SR&TIM_SR_UIF){TIM6->SR=~TIM_SR_UIF;tick++;}}
static void tim6_init(void){RCC->APB1ENR|=RCC_APB1ENR_TIM6EN;TIM6->PSC=1599;TIM6->ARR=99;TIM6->DIER|=TIM_DIER_UIE;NVIC_SetPriority(TIM6_DAC_IRQn,3);NVIC_EnableIRQ(TIM6_DAC_IRQn);TIM6->CR1|=TIM_CR1_CEN;}

/* ===== MAIN ===== */
int main(void){
    dwt_init();
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_USART1EN;
    GPIOA->MODER&=~(3U<<(9*2));GPIOA->MODER|=(2U<<(9*2));GPIOA->AFR[1]&=~(0xFU<<4);GPIOA->AFR[1]|=(7U<<4);
    USART1->BRR=(8<<4)|11;USART1->CR1=USART_CR1_TE|USART_CR1_UE;
    u1s("\r\n=== ALL SENSORS ===\r\n");

    i2c_init();bh_init();u1s("BH1750 OK\r\n");
    mpu_init();
    ow_init();u1s(ow_rst()?"DS18B20 OK\r\n":"DS18B20 not found\r\n");ds_trig();
    sr_init();u1s("HC-SR04 OK\r\n");
    adc_init();u1s("ADC OK\r\n");
    o_init();o_clr();o_str(0,10,"BOOT OK");o_show();u1s("OLED OK\r\n");
    btn_init();buz_init();buz_off();
    hc05_init();u1s("HC05 OK\r\n");
    tim6_init();u1s("GO\r\n");

    uint32_t lt=tick;int bt=0,ds_t=0,mt=0,st=0,pt=0,btn_db=0,btn_rel=0;uint8_t fs=0;
    while(1){
        uint32_t nw=tick;if(nw==lt)continue;int dt=(int)(nw-lt);lt=nw;
        for(int i=0;i<dt;i++)ds_poll();
        bt+=dt;ds_t+=dt;mt+=dt;st+=dt;pt+=dt;
        if(bt>=10){bt-=10;bh_read();}
        while(mt>=2){mt-=2;mpu_upd(0.02f);}
        if(st>=10){st-=10;sr_trig();sr_poll();}
        if(ds_t>=200){ds_t-=200;ds_trig();}

        int bn=btn_read();
        if(bn){btn_db++;if(btn_db>4&&!btn_rel){o_page=(o_page+1)%5;btn_rel=1;}}else{btn_db=0;btn_rel=0;}
        if(btn_db>200)btn_db=0;
        if(sr_cm>0&&sr_cm<20)buz_on();else buz_off();

        if(pt>=100){pt-=100;o_draw();uint8_t fb[260];int fl=frame_pack(fb,fs++,tick);hc05_send(fb,fl);}
    }
}
