/* --COPYRIGHT--,BSD
 * Copyright (c) 2014, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * --/COPYRIGHT--*/
//******************************************************************************
//!
//! ADC12_A - Sample A0 Input, AVcc Ref, Repeated Single Conversion
//!
//!  This example shows how to perform repeated conversions on a single channel
//!  using "repeat-single-channel" mode.  AVcc is used for the reference and
//!  repeated conversions are performed on Channel A0. Each conversion result
//!  is moved to an 8-element array called results[].  Test by applying a
//!  voltage to channel A0, then running. Open a watch window in debugger and
//!  view the results. Set Breakpoint1 in the index increment line to see the
//!  array value change sequentially and Breakpoint to see the entire array of
//!  conversion results in "results[]" for the specified Num_of_Results. This
//!  can run even in LPM4 mode as ADC has its own clock.
//!
//!                MSP430F552x
//!             -----------------
//!         /|\|                 |
//!          | |                 |
//!          --|RST       P6.0/A0|<- Vin
//!            |                 |
//!
//!
//! This example uses the following peripherals and I/O signals.  You must
//! review these and change as needed for your own board:
//! - ADC12_A peripheral
//! - GPIO Port peripheral
//! - A0
//!
//! This example uses the following interrupt handlers.  To use this example
//! in your own application you must add these interrupt handlers to your
//! vector table.
//! - ADC12_A_VECTOR
//!
//******************************************************************************

#include <msp430.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


#include "driverlib.h"

#define motor_down_pin GPIO_PIN1
#define motor_up_pin GPIO_PIN2
#define motor_port GPIO_PORT_P1

#define led_1_port GPIO_PORT_P1
#define led_1_pin GPIO_PIN0

#define led_2_port GPIO_PORT_P4
#define led_2_pin GPIO_PIN7

#define usart_port GPIO_PORT_P4
#define usart_rx_pin GPIO_PIN5 
#define usart_tx_pin GPIO_PIN4


//from http://software-dl.ti.com/msp430/msp430_public_sw/mcu/msp430/MSP430BaudRateConverter/index.html
//9.6 kbaud
#define usart_clock_prescale   3
#define usart_mod_reg_1  0
#define usart_mod_reg_2  3
#define usart_oversampling USCI_A_UART_LOW_FREQUENCY_BAUDRATE_GENERATION 

#define usart_base USCI_A1_BASE

#define clock_source_master UCS_XT2CLK_SELECT
#define clock_divider_master UCS_CLOCK_DIVIDER_1
#define clock_source_master_external 0

#define clock_source_subsystem UCS_XT1CLK_SELECT
#define clock_divider_subsystem UCS_CLOCK_DIVIDER_1

#define xt1_freq 32000
#define xt2_freq 40000000
//#define xt2_drive_strength UCS_XT2DRIVE_4MHZ_8MH
#define xt2_drive_strength UCS_XT2DRIVE_24MHZ_32MHZ


#define adc_port GPIO_PORT_P6
#define adc_pin  GPIO_PIN0


#define  log_temperature_buffer_size   3
#define  temperature_buffer_size   1 << log_temperature_buffer_size   

#define UART_PRINTF

#define time_out_value_ms 10000

volatile uint16_t temperature_buffer[temperature_buffer_size];
//Needs to be global in this
//example. Otherwise, the
//compiler removes it because it
//is not used for anything.

void memset_16(void *block, int c, size_t size){
  memset(block,c,2*size);
}

void motor_stop(void) {
  //Set all P1 pins HI
  GPIO_setOutputHighOnPin(
			  motor_port,motor_down_pin+motor_up_pin             );
}

void motor_down(void) {
  motor_stop();
  GPIO_setOutputLowOnPin(motor_port,motor_down_pin);
}

void motor_up(void) {
  motor_stop();
  GPIO_setOutputLowOnPin(motor_port,motor_up_pin);
}

void ports_init(void) {
			    
  motor_stop();
  //Set P1.x to output direction
  GPIO_setAsOutputPin(
		      motor_port,motor_down_pin+motor_up_pin
		      );
  GPIO_setAsOutputPin(
		      led_1_port,led_1_pin);

  GPIO_setAsOutputPin(
		      led_2_port,led_2_pin);

	
}

void clocks_init(void) {
  UCS_clockSignalInit(UCS_SMCLK,clock_source_subsystem,clock_divider_subsystem);

  UCS_setExternalClockSource(xt1_freq,xt2_freq);


  if (clock_source_master_external) {
    UCS_XT2Start(xt2_drive_strength);
  


    if (UCS_faultFlagStatus(UCS_XT2OFFG)){
      printf("Failed to enable XT2. Stay at internal clock.\n");
    } else {
      UCS_clockSignalInit(UCS_MCLK,clock_source_master,clock_divider_master);
    }
  }
}

void adc_init(void) {

  //Enable A/D channel A0
  GPIO_setAsPeripheralModuleFunctionInputPin(adc_port,
					     adc_pin
					     );

  //Initialize the ADC12_A Module
  /*
   * Base address of ADC12_A Module Use internal ADC12_A bit as
   * sample/hold signal to start conversion USE SMCLK Digital
   * Oscillator as clock source Use clock divider of 32. With an
   * external frequency of 32khz, this gives a sampling frequency of
   * 1kHz.
   */
  ADC12_A_init(ADC12_A_BASE,
	       ADC12_A_SAMPLEHOLDSOURCE_SC,
	       //ADC12_A_CLOCKSOURCE_ADC12OSC,
	       ADC12_A_CLOCKSOURCE_SMCLK,
	       //ADC12_A_CLOCKDIVIDER_1
	       ADC12_A_CLOCKDIVIDER_32
	       );

  ADC12_A_enable(ADC12_A_BASE);

  /*
   * Base address of ADC12_A Module
   * For memory buffers 0-7 sample/hold for 128 clock cycles
   * For memory buffers 8-15 sample/hold for128 clock cycles (default)
   * Enable Multiple Sampling
   */
  ADC12_A_setupSamplingTimer(ADC12_A_BASE,
			     ADC12_A_CYCLEHOLD_128_CYCLES,
			     ADC12_A_CYCLEHOLD_128_CYCLES,
			     ADC12_A_MULTIPLESAMPLESENABLE);

  //Configure Memory Buffer
  /*
   * Base address of the ADC12_A Module
   * Configure memory buffer 0
   * Map input A0 to memory buffer 0
   * Vref+ = AVcc
   * Vref- = AVss
   * Memory buffer 0 is not the end of a sequence
   */
  ADC12_A_memoryConfigure(ADC12_A_BASE,
			  ADC12_A_MEMORY_0,
			  ADC12_A_INPUT_A0,
			  ADC12_A_VREFPOS_AVCC,
			  ADC12_A_VREFNEG_AVSS,
			  ADC12_A_NOTENDOFSEQUENCE);

  //Enable memory buffer 0 interrupt
  ADC12_A_clearInterrupt(ADC12_A_BASE,
			 ADC12IFG0);
  ADC12_A_enableInterrupt(ADC12_A_BASE,
			  ADC12IE0);

  //Enable/Start first sampling and conversion cycle
  /*
   * Base address of ADC12_A Module
   * Start the conversion into memory buffer 0
   * Use the repeated single-channel
   */
  ADC12_A_startConversion(ADC12_A_BASE,
			  ADC12_A_MEMORY_0,
			  ADC12_A_REPEATED_SINGLECHANNEL);

}

void usart_init(void) {
  //P3.4,5 = USCI_A0 TXD/RXD
  GPIO_setAsPeripheralModuleFunctionInputPin(
					     usart_port,
					     usart_rx_pin + usart_tx_pin
					     );

  if ( STATUS_FAIL 
       == USCI_A_UART_initAdvance(usart_base,
				  USCI_A_UART_CLOCKSOURCE_SMCLK,
				  usart_clock_prescale,
				  usart_mod_reg_1,
				  usart_mod_reg_2,
				  USCI_A_UART_NO_PARITY,
				  USCI_A_UART_LSB_FIRST,
				  USCI_A_UART_ONE_STOP_BIT,
				  USCI_A_UART_MODE,
				  usart_oversampling)){
    return;
  }

  //Enable UART module for operation
  USCI_A_UART_enable(usart_base);

  //Enable Receive Interrupt
  USCI_A_UART_clearInterruptFlag(usart_base,
				 USCI_A_UART_RECEIVE_INTERRUPT);
  //  USCI_A_UART_enableInterrupt(usart_base,
  //			      USCI_A_UART_RECEIVE_INTERRUPT);




}

#define usart_printf printf 

/* void usart_printf(const char *format, ...) { */

/*   va_list arg; */
/*   int done; */
/*   uint8_t ctrl, psctrl; */



/*   /\*pass arguments through to the system printf*\/ */
/*   va_start (arg, format); */
/*   done = vprintf (format, arg); */
/*   va_end (arg); */

/*   fflush(stdout); */
				 
/*   while(USCI_A_UART_queryStatusFlags 	(usart_base,USCI_A_UART_BUSY )); */
				 
		
/* } */


/**
   Function that writes one character to the selected USART.

   The special character \\n is forwarded as \\r to \a stream.
*/
/* int usart_putchar(char c, FILE *s) { */
/*   // Load data onto buffer */



/*   return 0; */
/* } */

int putchar(int s)
{
  USCI_A_UART_transmitData(usart_base,
			   (char) s);

  while(!USCI_A_UART_getInterruptStatus(usart_base, 
					USCI_A_UART_TRANSMIT_INTERRUPT_FLAG )
	); 


  return(s);
}

/* static FILE usart_out = FDEV_SETUP_STREAM( usart_putchar,  */
/* 					   NULL,  */
/* 					   _FDEV_SETUP_WRITE ); */

void rtc_init(void) {

}

void timer_init(void) {
 
        //Start timer
        TIMER_B_clearTimerInterruptFlag(TIMER_B0_BASE);
        TIMER_B_configureUpMode(   TIMER_B0_BASE,
                                   TIMER_B_CLOCKSOURCE_SMCLK,
                                   TIMER_B_CLOCKSOURCE_DIVIDER_32,
                                   time_out_value_ms,
                                   TIMER_B_TBIE_INTERRUPT_DISABLE,
                                   TIMER_B_CAPTURECOMPARE_INTERRUPT_ENABLE,
                                   TIMER_B_DO_CLEAR
                                   );

        TIMER_B_startCounter(
                TIMER_B0_BASE,
                TIMER_B_UP_MODE
                );



}

void lcd_init(void) {

}

void temperature_update(uint16_t *tmp, volatile uint16_t *tmp_buffer, int log_buffer_length) {
  int i;

  *tmp=0;

  for(i=0;i<(1<<log_buffer_length);i++) {
    *tmp+=tmp_buffer[i];
  }

  *tmp = *tmp >> log_buffer_length;
  
}



void main(void)
{
  unsigned int i;
  uint16_t temperature;
  //  stdout = &usart_out;
  //Stop Watchdog Timer
  WDT_A_hold(WDT_A_BASE);

  clocks_init();

  ports_init();
	
  adc_init();

  usart_init();

  memset_16((void *) temperature_buffer,0,temperature_buffer_size);



  //Enter LPM4, Enable interrupts
  //  __bis_SR_register(LPM4_bits + GIE);
  // __bis_SR_register(GIE);

        //Enter LPM0, enable interrupts
  //     __bis_SR_register(LPM0_bits + GIE);
  // __enable_interrupt();

  //For debugger
  __no_operation();

  usart_printf("Start measurements... \n");


  i=0;
  while(1) {
    if (0 == (i%(1<<12))){
      temperature_update(&temperature,temperature_buffer,log_temperature_buffer_size);
      usart_printf("\rTemperature: %10u",temperature);
      GPIO_toggleOutputOnPin(led_1_port,led_1_pin);
      i=0;
    }
    i++;
  }
  
  
}

#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=ADC12_VECTOR
__interrupt
#elif defined(__GNUC__)
__attribute__((interrupt(ADC12_VECTOR)))
//__attribute__((interrupt(TIMER1_A1_VECTOR)))
#endif
void ADC12ISR(void)
{
  static uint8_t index = 0;

  switch (ADC12IV) {
  case  ADC12IV_NONE: break;         //Vector  0:  No interrupt
  case  ADC12IV_ADC12OVIFG: break;         //Vector  2:  ADC overflow
  case  ADC12IV_ADC12TOVIFG: break;         //Vector  4:  ADC timing overflow
  case  ADC12IV_ADC12IFG0:                //Vector  6:  ADC12IFG0
    //Move results
    temperature_buffer[index] =
      ADC12_A_getResults(ADC12_A_BASE,
			 ADC12_A_MEMORY_0);

    //Increment results index, modulo;
    //Set Breakpoint1 here and watch results[]
    index++;

    if (index == temperature_buffer_size)
      index = 0;
  default: break;
  }
}



#if defined(__TI_COMPILER_VERSION__) || defined(__IAR_SYSTEMS_ICC__)
#pragma vector=TIMERB0_VECTOR
__interrupt
#elif defined(__GNUC__)
__attribute__((interrupt(TIMERB0_VECTOR)))
#endif
void TIMERB0_ISR(void)
{
  //currently, do nothing except stopping the timer
  TIMER_B_stop(TIMER_B0_BASE);
}

