// Lab9.c
// Runs on LM4F120 or TM4C123
// Student names: Danica Corbita & Deepanshi Sharma 
// Last modification date: change this to the last modification date or look very silly
// Last Modified: 11/14/2018

// Analog Input connected to PE2=ADC1
// displays on Sitronox ST7735
// PF3, PF2, PF1 are heartbeats
// This U0Rx PC4 (in) is connected to other LaunchPad PC5 (out)
// This U0Tx PC5 (out) is connected to other LaunchPad PC4 (in)
// This ground is connected to other LaunchPad ground
// * Start with where you left off in Lab8. 
// * Get Lab8 code working in this project.
// * Understand what parts of your main have to move into the UART1_Handler ISR
// * Rewrite the SysTickHandler
// * Implement the s/w Fifo on the receiver end 
//    (we suggest implementing and testing this first)

#include <stdint.h>

#include "ST7735.h"
#include "PLL.h"
#include "ADC.h"
#include "print.h"
#include "../inc/tm4c123gh6pm.h"
#include "Uart.h"
#include "FiFo.h"

//*****the first three main programs are for debugging *****
// main1 tests just the ADC and slide pot, use debugger to see data
// main2 adds the LCD to the ADC and slide pot, ADC data is on Nokia
// main3 adds your convert function, position data is no Nokia

void DisableInterrupts(void); // Disable interrupts
void EnableInterrupts(void);  // Enable interrupts

#define PF1       (*((volatile uint32_t *)0x40025008))
#define PF2       (*((volatile uint32_t *)0x40025010))
#define PF3       (*((volatile uint32_t *)0x40025020))
uint32_t Data;      // 12-bit ADC
uint32_t Position;  // 32-bit fixed-point 0.001 cm
unsigned char signal[8];
unsigned char data[8];
unsigned int adc_data=0;
unsigned char dec=0;
unsigned int TxCounter=0;
int state=0;
char packetptr;

void SysTick_Init(void){
	NVIC_ST_CTRL_R = 0; 						
	NVIC_ST_RELOAD_R = 1333333; 
	NVIC_ST_CURRENT_R = 0; 
	NVIC_SYS_PRI3_R = (NVIC_SYS_PRI3_R & 0x00FFFFFF) | 0x20000000;		//is this necessary?
	NVIC_ST_CTRL_R = 0x07; 
}

// Initialize Port F so PF1, PF2 and PF3 are heartbeats
void PortF_Init(void){
// Intialize PortF for hearbeat
	volatile int delay=0;
	SYSCTL_RCGCGPIO_R &= ~0x20;		// enable port F clock
	SYSCTL_RCGCGPIO_R |= 0x20;
	delay=0;											// wait for clock to stabilize
	GPIO_PORTF_DIR_R &= ~0x0E;		// set PF1 - PF3 as output
	GPIO_PORTF_DIR_R |= 0x0E;
	GPIO_PORTF_AFSEL_R &= ~0x0E;	// disable alternate function for PF1 - PF3
	GPIO_PORTF_DEN_R &= ~0x0E;		// enable GPIO for PF1 - PF3
	GPIO_PORTF_DEN_R |= 0x0E;
}




uint32_t Status[20];             // entries 0,7,12,19 should be false, others true
char GetData[10];  // entries 1 2 3 4 5 6 7 8 should be 1 2 3 4 5 6 7 8
int main1(void){ // Make this main to test FiFo
  Fifo_Init();   // Assuming a buffer of size 6
  for(;;){
    Status[0]  = Fifo_Get(&GetData[0]);  // should fail,    empty
    Status[1]  = Fifo_Put(1);            // should succeed, 1 
    Status[2]  = Fifo_Put(2);            // should succeed, 1 2
    Status[3]  = Fifo_Put(3);            // should succeed, 1 2 3
    Status[4]  = Fifo_Put(4);            // should succeed, 1 2 3 4
    Status[5]  = Fifo_Put(5);            // should succeed, 1 2 3 4 5
    Status[6]  = Fifo_Put(6);            // should succeed, 1 2 3 4 5 6
    Status[7]  = Fifo_Put(7);            // should fail,    1 2 3 4 5 6 
    Status[8]  = Fifo_Get(&GetData[1]);  // should succeed, 2 3 4 5 6
    Status[9]  = Fifo_Get(&GetData[2]);  // should succeed, 3 4 5 6
    Status[10] = Fifo_Put(7);            // should succeed, 3 4 5 6 7
    Status[11] = Fifo_Put(8);            // should succeed, 3 4 5 6 7 8
    Status[12] = Fifo_Put(9);            // should fail,    3 4 5 6 7 8 
    Status[13] = Fifo_Get(&GetData[3]);  // should succeed, 4 5 6 7 8
    Status[14] = Fifo_Get(&GetData[4]);  // should succeed, 5 6 7 8
    Status[15] = Fifo_Get(&GetData[5]);  // should succeed, 6 7 8
    Status[16] = Fifo_Get(&GetData[6]);  // should succeed, 7 8
    Status[17] = Fifo_Get(&GetData[7]);  // should succeed, 8
    Status[18] = Fifo_Get(&GetData[8]);  // should succeed, empty
    Status[19] = Fifo_Get(&GetData[9]);  // should fail,    empty
  }
}

// Get fit from excel and code the convert routine with the constants
// from the curve-fit
uint32_t Convert(uint32_t input){
  Position = ((input*446)/1000)-487;
  return(Position);
}


// final main program for bidirectional communication
// Sender sends using SysTick Interrupt
// Receiver receives using RX
int main(void){ 
  PLL_Init(Bus80MHz);     // Bus clock is 80 MHz 
  ST7735_InitR(INITR_REDTAB);
  ADC_Init();    // initialize to sample ADC
  PortF_Init();
	SysTick_Init();
  Uart_Init();       // initialize UART
  ST7735_SetCursor(0,0);
  LCD_OutFix(0);
  ST7735_OutString(" cm");
//Enable SysTick Interrupt by calling SysTick_Init()
  EnableInterrupts();
  while(1){
    //--UUU--Complete this  - see lab manual
		while(state == 0){
		}																	// wait until adc mailbox is full
		state = 0;												// clear state of mailbox
		ST7735_SetCursor(0,0);						// set cursor to left corner
		do{
			Fifo_Get(&packetptr);
			data[0] = packetptr; 								// waits until fifo isn't empty
		}while (data[0] == 0x00);
		
		for(int j=1; j < 8; j++){								// place fifo data into output array
			Fifo_Get(&packetptr);
			data[j] = packetptr;
		}
		
		if((data[0] == 0x02) && (data[7] == 0x03)){
			data[0] = 0;											// reset packet start char
			data[7] = 0;											// reset packet end char
			
			for(int i=1; i < 6; i++){
				char place = 0;							// place holding character for debugging purposes
				place = data[i];
				ST7735_OutChar(place);				// output character to lcd
				data[i] = 0;										// reset index in output array
			}
			
			ST7735_SetCursor(6,0);					// set cursor to end
			ST7735_OutString(" cm.");				// print " cm."
			
		}
		
		else{
			for(int x=0;x < 8; x++){								// reset indexes in output array
				data[x] = 0;
			}
		}
  }
}

/* SysTick ISR
*/
void SysTick_Handler(void){ // every 20 ms
 //Sample ADC, convert to distance, create 8-byte message, send message out UART1
	int i=0;
	unsigned int temp=0;
	unsigned int convert=0;
	PF2 ^= 0x04;      // Heartbeat
	adc_data = ADC_In();
	state=1;
	PF2 ^= 0x04;      // Heartbeat
	convert = Convert(adc_data);
	temp = convert;
	signal[0] = 0x02;					// packet start
	dec = temp / 1000;
	temp %= 1000;
	signal[1] = dec + 0x30;		// ASCII char of first number
	signal[2] = 0x2E;					// decimal point
	dec = temp / 100;
	temp %= 100;
	signal[3] = dec + 0x30;		// ASCII char of second number
	dec = temp / 10;
	temp %= 10;
	signal[4] = dec + 0x30;		// ASCII char of third number
	signal[5] = temp + 0x30;		// ASCII char of last number
	signal[6] = 0x0D;					// ASCII char of carriage return
	signal[7] = 0x03;					// packet end
	for(i=0;i < 8; i++){
		Uart_OutChar(signal[i]);	// output packet using UART
	}
	
	TxCounter++; 								// increment transmit counter
	PF2 ^= 0xFF;								// toggle heartbeat	
}

