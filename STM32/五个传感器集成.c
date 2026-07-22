/**
 * 五合一传感器: BH1750 + MPU6050 + DS18B20 + HC-SR04 + ADC
 * 注意: HC-SR04 接 5V 供电, 不要和 STM32 的 3.3V 混用
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
static void i2s(int32_t n,char*b){char t[12];int i=0,neg=0;if(n<0){neg=1;n=-n;}if(n==0)t[i++]='0';else while(n){t[i++]=(char)('0'+n%10);n/=10;}if(neg)t[i++]='-';int j=0;while(i>0)b[j++]=t[--i];b[j]='\0';}

/* ===== I2C ===== */
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

/* ===== BH1750 (命令直接发, 无寄存器地址) ===== */
static int bh_lux=0;
static void bh_init(void){i2s_start();i2c_w(0x23<<1);i2c_w(0x01);i2s_stop();i2s_start();i2c_w(0x23<<1);i2c_w(0x10);i2s_stop();}
static void bh_read(void){i2s_start();i2c_w((0x23<<1)|1);uint8_t hi=i2c_r(1),lo=i2c_r(0);i2s_stop();uint16_t r=((uint16_t)hi<<8)|lo;bh_lux=(int)((r*5)/6);}

/* ===== MPU6050 ===== */
static float fatan2(float y,float x){float ay=y<0?-y:y,ang;if(x>=0){float r=(x-ay)/(x+ay);ang=0.78539816f-0.78539816f*r;}else{float r=(x+ay)/(ay-x);ang=2.35619449f-0.78539816f*r;}return y<0?-ang:ang;}
static float fsqrt(float x){return __builtin_sqrtf(x);}
static float mpu_R,mpu_P,mpu_Y, gxb,gyb,gzb; static int mpu_ok;
static void mpu_raw(int16_t*ax,int16_t*ay,int16_t*az,int16_t*gx,int16_t*gy,int16_t*gz){uint8_t d[14];i2c_buf_r(0x68,0x3B,d,14);*ax=(int16_t)(((uint16_t)d[0]<<8)|d[1]);*ay=(int16_t)(((uint16_t)d[2]<<8)|d[3]);*az=(int16_t)(((uint16_t)d[4]<<8)|d[5]);*gx=(int16_t)(((uint16_t)d[8]<<8)|d[9]);*gy=(int16_t)(((uint16_t)d[10]<<8)|d[11]);*gz=(int16_t)(((uint16_t)d[12]<<8)|d[13]);}
static int mpu_init(void){if(i2c_reg_r(0x68,0x75)!=0x68){u1s("[MPU] not found\r\n");return-1;}i2c_reg_w(0x68,0x6B,0x01);i2c_reg_w(0x68,0x1B,0x18);i2c_reg_w(0x68,0x1C,0x00);i2c_reg_w(0x68,0x1A,0x03);i2c_reg_w(0x68,0x19,0x04);float g1=0,g2=0,g3=0;for(int i=0;i<100;i++){int16_t ax,ay,az,gx,gy,gz;mpu_raw(&ax,&ay,&az,&gx,&gy,&gz);g1+=gx;g2+=gy;g3+=gz;dus(5000);}gxb=g1/100.0f;gyb=g2/100.0f;gzb=g3/100.0f;mpu_ok=1;u1s("[MPU] ready\r\n");return 0;}
static void mpu_upd(float dt){if(!mpu_ok)return;int16_t ax,ay,az,gx,gy,gz;mpu_raw(&ax,&ay,&az,&gx,&gy,&gz);float gr=(gx-gxb)/16.384f,gp=(gy-gyb)/16.384f,gy2=(gz-gzb)/16.384f;float ar=ax/16384.0f,ap=ay/16384.0f,ay3=az/16384.0f;float ar2=fatan2(ap,ay3),ap2=fatan2(-ar,fsqrt(ap*ap+ay3*ay3));mpu_R=0.98f*(mpu_R+gr*dt)+0.02f*ar2;mpu_P=0.98f*(mpu_P+gp*dt)+0.02f*ap2;mpu_Y+=gy2*dt;mpu_R*=57.29578f;mpu_P*=57.29578f;mpu_Y*=57.29578f;}

/* ===== DS18B20 ===== */
static void ow_out(void){GPIOC->MODER&=~(3U<<14);GPIOC->MODER|=(1U<<14);}
static void ow_inp(void){GPIOC->MODER&=~(3U<<14);}
static void ow_lo(void){GPIOC->BSRR=(uint32_t)(1U<<7)<<16;}
static void ow_hi(void){GPIOC->BSRR=(1U<<7);}
static int ow_in(void){return(GPIOC->IDR&(1U<<7))?1:0;}
static int ow_rst(void){int p;__disable_irq();ow_out();ow_lo();dus(480);ow_inp();dus(60);p=ow_in()?0:1;dus(420);__enable_irq();return p;}
static void ow_wb(uint8_t d){for(int i=0;i<8;i++){__disable_irq();ow_out();ow_lo();dus(1);if(d&1)ow_hi();dus(60);ow_hi();dus(1);__enable_irq();d>>=1;}}
static uint8_t ow_rb(void){uint8_t d=0;for(int i=0;i<8;i++){int b;__disable_irq();ow_out();ow_lo();dus(1);ow_inp();dus(5);b=ow_in();dus(55);__enable_irq();d>>=1;if(b)d|=0x80;}return d;}
static void ow_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN;GPIOC->PUPDR&=~(3U<<14);GPIOC->PUPDR|=(1U<<14);ow_inp();}
enum{DS_I,DS_W}ds_st;static int ds_t10=0,ds_cnt=0;
static void ds_trig(void){if(ds_st!=DS_I)return;if(!ow_rst())return;ow_wb(0xCC);ow_wb(0x44);ds_cnt=0;ds_st=DS_W;}
static void ds_poll(void){if(ds_st!=DS_W)return;ds_cnt++;if(ds_cnt<75)return;if(!ow_rst()){ds_st=DS_I;return;}ow_wb(0xCC);ow_wb(0xBE);uint8_t d[9];for(int i=0;i<9;i++)d[i]=ow_rb();int16_t r=(int16_t)(((uint16_t)d[1]<<8)|d[0]);ds_t10=(int)((r*10)/16);ds_st=DS_I;}

/* ===== HC-SR04 ===== */
static volatile uint32_t echo_s,echo_e;static volatile int echo_ok;
void EXTI2_IRQHandler(void){if(EXTI->PR&EXTI_PR_PR2){EXTI->PR=EXTI_PR_PR2;if(GPIOB->IDR&GPIO_PIN_2)echo_s=dwt_get();else{echo_e=dwt_get();echo_ok=1;}}}
static int sr04_cm=0;
static void sr04_trig(void){GPIOB->BSRR=GPIO_PIN_0;dus(15);GPIOB->BSRR=(uint32_t)GPIO_PIN_0<<16;}
static void sr04_poll(void){if(!echo_ok)return;echo_ok=0;uint32_t w=echo_e-echo_s;sr04_cm=(int)(w/(16*58));if(sr04_cm>400)sr04_cm=-1;}
static void sr04_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;GPIOB->MODER|=(1<<(0*2));GPIOB->PUPDR&=~(3U<<(2*2));RCC->APB2ENR|=RCC_APB2ENR_SYSCFGEN;SYSCFG->EXTICR[0]|=(1<<8);EXTI->IMR|=EXTI_IMR_MR2;EXTI->RTSR|=EXTI_RTSR_TR2;EXTI->FTSR|=EXTI_FTSR_TR2;NVIC_SetPriority(EXTI2_IRQn,1);NVIC_EnableIRQ(EXTI2_IRQn);}

/* ===== ADC ===== */
static void adc_init(void){RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_ADC1EN;GPIOA->MODER|=(3U<<(1*2));ADC1->SMPR2|=(7U<<3);ADC1->SQR3=1;ADC1->CR2|=ADC_CR2_ADON;for(volatile int i=0;i<100000;i++)__NOP();}
static uint16_t adc_read(void){ADC1->CR2|=ADC_CR2_SWSTART;while(!(ADC1->SR&ADC_SR_EOC));return(uint16_t)(ADC1->DR&0xFFFF);}

/* ===== TIM6 ===== */
static volatile uint32_t tick;
void TIM6_DAC_IRQHandler(void){if(TIM6->SR&TIM_SR_UIF){TIM6->SR=~TIM_SR_UIF;tick++;}}
static void tim6_init(void){RCC->APB1ENR|=RCC_APB1ENR_TIM6EN;TIM6->PSC=1599;TIM6->ARR=99;TIM6->DIER|=TIM_DIER_UIE;NVIC_SetPriority(TIM6_DAC_IRQn,3);NVIC_EnableIRQ(TIM6_DAC_IRQn);TIM6->CR1|=TIM_CR1_CEN;}

/* ===== 输出 ===== */
static void print(void){
    char b[128],n[12];

    /* 诊断: 打印原始 MPU6050 加速计值 */
    int16_t dax,day,daz,dgx,dgy,dgz;
    if(mpu_ok){mpu_raw(&dax,&day,&daz,&dgx,&dgy,&dgz);}
    else{dax=day=daz=dgx=dgy=dgz=0;}

    strcpy(b,"L:");i2s(bh_lux/10,n);strcat(b,n);strcat(b,".");i2s(bh_lux%10,n);strcat(b,n);strcat(b,"lux ");
    strcat(b,"T:");int t10=ds_t10;if(t10<0){strcat(b,"-");t10=-t10;}i2s(t10/10,n);strcat(b,n);strcat(b,".");i2s(t10%10,n);strcat(b,n);strcat(b,"C ");
    uint32_t mv=((uint32_t)adc_read()*3300)>>12;
    strcat(b,"A:");i2s((int32_t)mv,n);strcat(b,n);strcat(b,"mV ");
    /* 原始 MPU 数据 */
    strcat(b,"aX:");i2s(dax,n);strcat(b,n);
    strcat(b," aY:");i2s(day,n);strcat(b,n);
    strcat(b," aZ:");i2s(daz,n);strcat(b,n);
    strcat(b," SR:");i2s(sr04_cm,n);strcat(b,n);strcat(b,"cm\r\n");
    u1s(b);
}

/* ===== MAIN ===== */
int main(void){
    dwt_init();
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_USART1EN;
    GPIOA->MODER&=~(3U<<(9*2));GPIOA->MODER|=(2U<<(9*2));GPIOA->AFR[1]&=~(0xFU<<4);GPIOA->AFR[1]|=(7U<<4);
    USART1->BRR=(8<<4)|11;USART1->CR1=USART_CR1_TE|USART_CR1_UE;
    u1s("ABCD\r\n=== 5-in-1 Sensor ===\r\n");

    i2c_init();
    bh_init();u1s("BH1750 OK\r\n");
    mpu_init();
    ow_init();u1s(ow_rst()?"DS18B20 OK\r\n":"DS18B20 not found\r\n");ds_trig();
    sr04_init();u1s("HC-SR04 OK\r\n");
    adc_init();u1s("ADC OK\r\n");
    tim6_init();u1s("GO\r\n");

    uint32_t lt=tick;int bt=0,ds_t=0,mt=0,st=0,pt=0;
    while(1){
        uint32_t nw=tick;if(nw==lt)continue;int dt=(int)(nw-lt);lt=nw;
        for(int i=0;i<dt;i++)ds_poll();
        bt+=dt;ds_t+=dt;mt+=dt;st+=dt;pt+=dt;
        if(bt>=10){bt-=10;bh_read();}
        while(mt>=2){mt-=2;mpu_upd(0.02f);}
        if(st>=10){st-=10;sr04_trig();sr04_poll();}
        if(ds_t>=200){ds_t-=200;ds_trig();}
        if(pt>=100){pt-=100;print();}
    }
}
