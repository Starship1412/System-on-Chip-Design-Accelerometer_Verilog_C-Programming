/*--------------------------------------------------------------------------------------------------
	Demonstration program for Cortex-M0 SoC design - basic version, no CMSIS

	Enable the interrupt for UART - interrupt when a character is received.
	Repeat:
	  Set the LEDs to match the switches, then flash the 8 rightmost LEDs.
	  Go to sleep mode, waiting for an interrupt
		  On UART interrupt, the ISR sends the received character back to UART and stores it
			  main() also shows the character code on the 8 rightmost LEDs
	  When a whole message has been received, the stored characters are copied to another array
		  with their case inverted, then printed. 

	Version 6 - March 2023
	Edited April 2023 - SoC Group 14
  ------------------------------------------------------------------------------------------------*/

#include <stdio.h>					// needed for printf
#include <stdlib.h>
#include "DES_M0_SoC.h"			// defines registers in the hardware blocks used

// Define names for SPI slaves
#define NONE 0
#define DISP 1
#define ACL  2
#define BUF_SIZE						100				// size of the array to hold received characters
#define ASCII_CR						'\r'			// character to mark the end of input
#define CASE_BIT						('A' ^ 'a')		// bit pattern used to change the case of a letter
#define FLASH_DELAY					1000000		// delay for flashing LEDs, ~220 ms
#define INVERT_LEDS					(GPIO_LED ^= 0xff)		// inverts the 8 rightmost LEDs
#define ARRAY_SIZE(__x__)   (sizeof(__x__)/sizeof(__x__[0]))  // macro to find array size

// Global variables - shared between main and UART_ISR
volatile uint8  RxBuf[BUF_SIZE];	// array to hold received characters
volatile uint8  counter  = 0; 		// current number of characters in RxBuf[]
volatile uint8  BufReady = 0; 		// flag indicates data in RxBuf is ready for processing
volatile int16  reg_read;
volatile uint8  switch_read;
volatile uint8  junk;
volatile uint16  leds;
uint8* displayReg = (uint8*) DISPLAY_BASE;   // Access to each of the numbers displayed.

//////////////////////////////////////////////////////////////////
// Interrupt service routine, runs when UART interrupt occurs - see cm0dsasm.s
//////////////////////////////////////////////////////////////////
void UART_ISR() {
	char c;
	c = UART_RXD;	 				// read character from UART (there must be one waiting)
	RxBuf[counter]  = c;  // store in buffer
	counter++;            // increment counter, number of characters in buffer
	UART_TXD = c;  				// write character to UART (assuming transmit queue not full)
	/* Counter is now the position in the buffer that the next character should go into.
		If this is the end of the buffer, i.e. if counter == BUF_SIZE-1, then null terminate
		and indicate that a complete sentence has been received.
		If the character just put in was a carriage return, do the same.  */
	if (counter == BUF_SIZE-1 || c == ASCII_CR) {
		counter--;							// decrement counter (CR will be over-written)
		RxBuf[counter] = NULL;  // null terminate to make the array a valid string
		BufReady       = 1;	    // indicate that data is ready for processing
	}
}

//////////////////////////////////////////////////////////////////
// Interrupt service routine for System Tick interrupt
//////////////////////////////////////////////////////////////////
void SysTick_ISR() {
	// Do nothing - this interrupt is not used here
}

//////////////////////////////////////////////////////////////////
// Software delay function - delay time proportional to argument n
// As a rough guide, delay(1000) takes 160 us to 220 us, 
// depending on the compiler optimisation level.
//////////////////////////////////////////////////////////////////
void delay (uint32 n) {
	volatile uint32 i;
		for(i=0; i<n; i++);		// do nothing n times
}

// set CS low when argument is ACL, set CS high when argument is NONE
void SPIselect(uint8 sel) {
	if (sel) 							// if input not zero
		GPIO_ACL &= 0xFFFE; // bitwise AND sets the LSB (aclSSn) to 0
	else
		GPIO_ACL |= 0x0001; // bitwise OR sets the LSB (aclSSn) to 1
}

// function must generate 8 clock pulses
uint8 SPIbyte(uint8 TXdata) {
	uint8 RXbyte;															// read byte that will be returned
	int8 i;
	// bit 2 of GPIO_ACL is MOSI (input bit)
	for (i = 7 ; i >= 0 ; i--) {
		GPIO_ACL = ((TXdata >> i) & 1) << 2;		// bit i sent to MOSI (GPIO_ACL[2])
		delay(10);															// delay half clock cycle
		GPIO_ACL |= 0x2;												// send SCLK high
		RXbyte = (RXbyte<<1)|(GPIO_IN1>>15);		// sample MISO (MSB of GPIO_IN1)
		delay(10);															// delay half clock cycle
		GPIO_ACL &= ~0x2;	 											// send SCLK low
	}
	return RXbyte;
}

// function to read from accelerometer
int16 AccRead(uint8 address) {        // takes in a value and calls it 'address'
	int8 byteRXL;
	int16 byteRXH;
	int16 ret_val;
	SPIselect(ACL);											// sets CS low to start SPI transaction
	SPIbyte(0x0B);											// sends read instruction
	SPIbyte(address);										// sends address
	byteRXL = SPIbyte(0xFF);						// gets reg data (sends junk) 
	byteRXH = SPIbyte(0xFF);						// gets reg data, high byte. will read next address automatically.
	SPIselect(NONE);										// sets CS high to end SPI transaction
	ret_val = byteRXL | (byteRXH << 8); // shift high bytes to left and add low bytes
	return ret_val;
}

// function to write to accelerometer
void AccWrite(uint8 address, uint8 byteTX) {
	SPIselect(ACL);							// sets CS low to start SPI transaction
	SPIbyte(0x0A);							// sends write instruction
	SPIbyte(address);						// sends address
	SPIbyte(byteTX);						// sends byte
	SPIselect(NONE);
}

// function to display acceleration value on LEDs
uint16 OH_LED(int16 reg_read) {
	uint16 value = 0xFFFF;
	uint16 x;
	if (reg_read > 0) {
	 reg_read = abs(reg_read);			// remove sign
	 printf("reg_read pos: %d\n", reg_read);
	 x = reg_read/128;							// scale down
	 printf("lights on: %d\n", x);
	 value = value << (16-x);				// turns 1-x LEDs off on right side
	}
	else {
	 printf("reg_read neg: %d\n", reg_read);
	 reg_read = abs(reg_read);			// remove sign
	 x = reg_read/128;							// scale down
	 printf("lights on: %d\n", x);
	 value = value >> (16-x);				// turns 1-x LEDs off on right side
	}
	return value;
}

void displayValue(int16 value) {
	uint8 i = 0;
	if(value<0) {
		value = -value;
	  displayReg[4] = 17;            // display of negative sign
	} else
		displayReg[4] = 31;            // If it is a positive number or 0, this digital position does not show anything.
	displayReg[8] = 0xFF;            // Set hex mode for each digit.
	displayReg[9] = 0xFF;            // Enable each digit.
	displayReg[5] = 31;              // This digital position does not show anything.
	displayReg[6] = 31;              // This digital position does not show anything.
	displayReg[7] = 31;              // This digital position does not show anything.
	for (i = 0; i < 4; i++) {        // Display numbers one by one. In total there are 4 figures to be displayed.
	 displayReg[i] = value%10;
	 value = value/10;
	}
}

//////////////////////////////////////////////////////////////////
// Main Function
//////////////////////////////////////////////////////////////////
int main(void) {

// ========================  Initialisation ==========================================

	// Configure the UART - the control register decides which events cause interrupts
	UART_CTL = (1 << UART_RX_FIFO_NOTEMPTY_BIT_POS);	// enable rx data available interrupt only

	// Configure the interrupt system in the processor (NVIC)
	NVIC_Enable = (1 << NVIC_UART_BIT_POS);		        // Enable the UART interrupt

	delay(FLASH_DELAY);												        // wait a short time
	printf("\n\nWelcome to Cortex-M0 SoC\n");		      // print a welcome message
	AccWrite(0x2D, 0x2);		                          // set POWER_CTL register
	AccWrite(0x2C, 0x1);		                          // set FILTER_CTL register

// ========================  Working Loop ==========================================

	while(1) {		                          // loop forever	
			delay(2000000);                     // delay of about 400ms. large fraction of a second. sets refresh rate
			switch_read = GPIO_SW; 					    // check value from switches
			switch_read &= 0x3;							    // zero all bits except 2 LSB
			
			// set register to read based on input from last two switches
			// 00 - X-Axis
			// 01 - Y-Axis
			// 1x - Z-Axis
			switch (switch_read) {
				case 0:
					reg_read = AccRead(0x0E); 			// read xdata reg
					leds = OH_LED(reg_read);
					GPIO_LED = leds;								// output to LEDs
					printf("X-Axis: %u\n", leds);
					break;
				case 1:
					reg_read = AccRead(0x10); 			// read ydata reg
					leds = OH_LED(reg_read);
					GPIO_LED = leds;								// output to LEDs
					printf("Y-Axis: %u\n", leds);
					break;
				case 2:
					reg_read = AccRead(0x12); 			// read zdata reg
					leds = OH_LED(reg_read);
					GPIO_LED = leds;								// output to LEDs
					printf("Z-Axis: %u\n", leds);
				case 3:
					reg_read = AccRead(0x12); 			// read zdata reg
					leds = OH_LED(reg_read);
					GPIO_LED = leds;								// output to LEDs
					printf("Z-Axis: %u\n", leds);
				default:
					break;
			}
		displayValue(reg_read);               // display gravitational acceleration
	} // end of infinite loop
}  // end of main
