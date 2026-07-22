#include "stm32f4xx.h"
#include <stdio.h>
#include <math.h>

/*
 * PWM 正弦波 — PB0 (TIM3_CH3) → PC0 (ADC2 CH10)
 * 接线: PB0 ←→ PC0 (直连, 无需 RC)
 *
 * 手动更新占空比, 每波完成采一次 ADC
 * 条状图会左右摆动 = 正弦波!
 */

#define N_SAMPLES 128
#define PWM_ARR   999
#define ADC_GND   1023
#define ADC_RANGE 3072

static uint16_t sine_table[N_SAMPLES];

static void delay_us(uint32_t us) {
  while(us--) for(volatile int i=0;i<14;i++) __NOP();
}

static void uart1_init(void){
  RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN; RCC->APB2ENR|=RCC_APB2ENR_USART1EN;
  GPIOA->MODER&=~(GPIO_MODER_MODER9|GPIO_MODER_MODER10);
  GPIOA->MODER|=(2<<(9*2))|(2<<(10*2));
  GPIOA->AFR[1]&=~((0xF<<4)|(0xF<<8)); GPIOA->AFR[1]|=(7<<4)|(7<<8);
  USART1->BRR=(8<<4)|11; USART1->CR1|=USART_CR1_TE|USART_CR1_RE|USART_CR1_UE;
}
static void uart1_putc(char c){ while(!(USART1->SR&USART_SR_TXE)); USART1->DR=c; }
int _write(int fd,const char*p,int n){
  (void)fd; for(int i=0;i<n;i++){ if(p[i]=='\n')uart1_putc('\r'); uart1_putc(p[i]); } return n;
}

static void adc_init(void){
  RCC->AHB1ENR|=RCC_AHB1ENR_GPIOCEN; RCC->APB2ENR|=RCC_APB2ENR_ADC2EN;
  GPIOC->MODER|=(3<<(0*2)); ADC->CCR=0;
  ADC2->SMPR1|=(7<<ADC_SMPR1_SMP10_Pos); ADC2->SQR3=10;
  ADC2->CR2|=ADC_CR2_ADON; for(volatile int i=0;i<100000;i++) __NOP();
}
static uint16_t adc_read(void){
  ADC2->CR2|=ADC_CR2_SWSTART; while(!(ADC2->SR&ADC_SR_EOC));
  return (uint16_t)(ADC2->DR&0xFFFF);
}

static void print_bar(uint16_t raw){
  int n=(raw-1023)*50/3072; if(n<0)n=0; if(n>50)n=50;
  printf("["); for(int i=0;i<50;i++) printf("%c",i<n?'#':'.'); printf("]");
}

int main(void){
  uart1_init();

  /* 正弦表: 0~PWM_ARR */
  for(int i=0;i<N_SAMPLES;i++)
    sine_table[i]=(uint16_t)(PWM_ARR/2.0f+(PWM_ARR/2.0f-1)*sinf(6.2831853f*i/N_SAMPLES));

  /* PB0 → TIM3_CH3 (AF2) */
  RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;
  GPIOB->MODER&=~(3<<(0*2)); GPIOB->MODER|=(2<<(0*2));
  GPIOB->AFR[0]&=~(0xF<<0);  GPIOB->AFR[0]|=(2<<0);

  /* TIM3 CH3 PWM */
  RCC->APB1ENR|=RCC_APB1ENR_TIM3EN;
  TIM3->PSC=0; TIM3->ARR=PWM_ARR;
  TIM3->CCMR2|=(6<<TIM_CCMR2_OC3M_Pos)|TIM_CCMR2_OC3PE;
  TIM3->CCER|=TIM_CCER_CC3E; TIM3->CCR3=PWM_ARR/2;
  TIM3->CR1|=TIM_CR1_ARPE; TIM3->EGR|=TIM_EGR_UG; TIM3->CR1|=TIM_CR1_CEN;

  adc_init();

  printf("\n================================\n");
  printf("PWM Sine Wave Generator\n");
  printf("PB0 (TIM3 CH3) → PC0 (ADC)\n");
  printf("16kHz PWM, 128 samples\n");
  printf("================================\n\n");

  while(1){
    /* 遍历整波, 每一步都采样 */
    for(int i=0;i<N_SAMPLES;i++){
      TIM3->CCR3=sine_table[i];

      /* 采 16 次平均 = 软件低通 */
      uint32_t sum=0;
      for(int k=0;k<16;k++) sum+=adc_read();
      uint16_t raw=(uint16_t)(sum/16);

      /* 每 4 步打印一次 (共 32 个点) */
      if(i%4==0){
        int32_t mv=((int32_t)raw-ADC_GND)*3300/ADC_RANGE;
        if(mv<0)mv=0;
        printf("ADC:%4u  %4ld mV  ",raw,mv);
        print_bar(raw);
        printf("\n");
      }
      delay_us(20);  /* ~50µs/步 → 128步≈6.4ms/波 → ~156Hz 正弦 */
    }
  }
}
