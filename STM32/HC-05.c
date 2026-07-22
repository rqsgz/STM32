/**
 * HC-05 Transparent Demo — MASTER+SLAVE unified
 * IS_MASTER: 0=Slave, 1=Master
 */

#include "stm32f4xx.h"
#include <string.h>

#define IS_MASTER  0  /* <--- 0=Slave, 1=Master */

static void u1c(char c){while(!(USART1->SR&USART_SR_TXE));USART1->DR=(uint8_t)c;}
static void u1s(const char*s){while(*s)u1c(*s++);}
static void u32s(uint32_t n,char*b){char t[12];int i=0;
    if(n==0)t[i++]='0';else while(n){t[i++]=(char)('0'+n%10);n/=10;}
    int j=0;while(i>0)b[j++]=t[--i];b[j]='\0';}
static void dwt_init(void){CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CYCCNT=0;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;}
static uint32_t dwt_get(void){return DWT->CYCCNT;}

static volatile uint8_t rb[256];static volatile uint16_t rh,rt;
void USART3_IRQHandler(void){
    if(USART3->SR&USART_SR_RXNE){uint8_t b=(uint8_t)(USART3->DR&0xFF);uint16_t n=(rh+1)%256;if(n!=rt){rb[rh]=b;rh=n;}}
    if(USART3->SR&(USART_SR_ORE|USART_SR_FE))(void)USART3->DR;
}
static int rget(void){if(rh==rt)return-1;uint8_t b=rb[rt];rt=(rt+1)%256;return b;}
static void u3s(const char*s){while(*s){while(!(USART3->SR&USART_SR_TXE));USART3->DR=(uint8_t)*s++;}}

int main(void){
    dwt_init();

    /* USART1 */
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;RCC->APB2ENR|=RCC_APB2ENR_USART1EN;
    GPIOA->MODER&=~(3U<<(9*2));GPIOA->MODER|=(2U<<(9*2));
    GPIOA->AFR[1]&=~(0xFU<<4);GPIOA->AFR[1]|=(7U<<4);
    USART1->BRR=(8<<4)|11;USART1->CR1=USART_CR1_TE|USART_CR1_UE;

#if IS_MASTER
    u1s("=== MASTER ===\r\n");
#else
    u1s("=== SLAVE ===\r\n");
#endif

    /* PB5=KEY LOW (transparent mode) */
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN;
    GPIOB->MODER&=~(3U<<(5*2));GPIOB->MODER|=(1U<<(5*2));
    GPIOB->BSRR=(uint32_t)(1U<<5)<<16;
    u1s("KEY=LOW\r\n");

    /* USART3 @ 9600 + IRQ */
    RCC->APB1ENR|=RCC_APB1ENR_USART3EN;
    GPIOB->MODER&=~((3U<<(10*2))|(3U<<(11*2)));
    GPIOB->MODER|=(2U<<(10*2))|(2U<<(11*2));
    GPIOB->AFR[1]&=~((0xFU<<8)|(0xFU<<12));GPIOB->AFR[1]|=(7U<<8)|(7U<<12);
    USART3->BRR=0x683;USART3->CR1=USART_CR1_TE|USART_CR1_RE|USART_CR1_UE|USART_CR1_RXNEIE;
    NVIC_SetPriority(USART3_IRQn,2);NVIC_EnableIRQ(USART3_IRQn);

    u1s("Wait pairing...\r\n");
    for(volatile int i=0;i<16000000;i++)__NOP();
    u1s("=== GO ===\r\n");

    uint32_t cnt=0,last=dwt_get();
    char lb[128];int lpos=0;
    while(1){
        /* RX: accumulate until \n */
        int c=rget();
        if(c>=0){
            if(c=='\r'){} /* skip */
            else if(c=='\n'){
                lb[lpos]='\0';
                if(lpos>0){u1s("[RX] ");u1s(lb);u1s("\r\n");
#if !IS_MASTER
                    u3s("1\r\n"); u1s("[TX] 1\r\n");
#endif
                }
                lpos=0;
            }else{if(lpos<126)lb[lpos++]=(char)c;}
        }

        /* TX: periodic */
        uint32_t per=IS_MASTER?32000000U:80000000U;
        if((dwt_get()-last)>per){last=dwt_get();cnt++;
#if IS_MASTER
            u3s("Hello\r\n"); u1s("[TX] Hello\r\n");
#else
            u3s("Beat\r\n");  u1s("[TX] Beat\r\n");
#endif
        }
    }
}
