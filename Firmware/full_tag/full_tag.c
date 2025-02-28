
#include "ch32fun.h"
#include <stdio.h>
#include <stdbool.h>

#include "lib_i2c.h"
#include "ch32v003_touch.h"

#define NTAG_ADDR 0x02

uint32_t *bank_addrs[4] = {(uint32_t*)0x08003000, (uint32_t*)0x08003400, (uint32_t*)0x08003800, (uint32_t*)0x08003c00};

int unlock_flash();

int read_bank_into_ntag(int bank);

int write_ntag_into_bank(int bank);

void read_and_dump_tag(void);

uint8_t get_active_bank(void);

void process_button_1(bool pressed);

void process_button_2(bool pressed);

void start_blinking(uint8_t long_count, uint8_t short_count);

void update_blinking();

#define TOUCH_THRESH 20

#define SHORT_PRESS_TIME (75*1500)
#define LONG_PRESS_TIME (1000*1500)

#define SHORT_BLINK_TIME (200*1500)
#define LONG_BLINK_TIME (750*1500)
#define BLINK_SPACE_TIME (1000*1500)
#define TIMEOUT (30000*1500)

uint8_t bank = 0;
uint8_t mode = 0;
uint8_t count = 0;

uint32_t last_interaction = 0;
uint32_t blink_start = 0;
bool is_blinking = false;

uint8_t blink_count = 0;
uint8_t target_long_blinks = 0;
uint8_t target_short_blinks = 0;

bool led_state = false;

bool button_1_pressed = false;
uint32_t button_1_press_start = 0;

bool button_2_pressed_last = false;
uint32_t button_2_press_start = 0;


int main()
{
	SystemInit();

	RCC->CFGR0 |= (12<<4); //slowdown for lower power

	// Enable GPIOD and ADC for capsense
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_ADC1;
	//PD6 output
	GPIOD->CFGLR &= ~(0xf<<(4*6));
	GPIOD->CFGLR |= ((GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*6));

	if(i2c_init(I2C_CLK_400KHZ) != I2C_OK) printf("Failed to init the I2C Bus\n");

	// Initialising I2C causes the pins to transition from LOW to HIGH.
	// Wait 100ms to allow the I2C Device to timeout and ignore the transition.
	// Otherwise, an extra 1-bit will be added to the next transmission
	Delay_Ms(100);
	// Turn on PD6 LED
	GPIOD->OUTDR = 1<<6;
	InitTouchADC();
	//bank = get_active_bank();
	mode = 0;
	while(1) {
		uint32_t current_time = (SysTick->CNT);
		if (current_time - last_interaction > TIMEOUT) {
			mode = 0;
			count = 0;
			//bank = get_active_bank();
			last_interaction = (SysTick->CNT);
		}

		uint32_t touchval1 = ReadTouchPin( GPIOD, 5, 5, 3);
		uint32_t touchval2 = ReadTouchPin( GPIOD, 4, 7, 3);

		bool button1 = touchval1 > TOUCH_THRESH;
		bool button2 = touchval2 > TOUCH_THRESH;

		//printf("Button 1 %d %d, button 2 %d %d\n", touchval1, button1, touchval2, button2);
	
		process_button_1(button1);  
		process_button_2(button2);  
		update_blinking();

		//printf("Mode: %d\n", mode);

		switch (mode) {
			case 0:
				if (!is_blinking) {
					start_blinking(0, (1));
				}
				break;
	
			case 1:
				if (!is_blinking) {
					start_blinking(1, (count+1));
				}
				break;
	
			case 2:
				if (!is_blinking) {
					start_blinking(2, (count+1));
				}
				break;
		}
	Delay_Us(500);
	}
}

int unlock_flash() {
	// Unkock flash - be aware you need extra stuff for the bootloader.
	FLASH->KEYR = FLASH_KEY1;
	FLASH->KEYR = FLASH_KEY2;

	// For unlocking programming, in general.
	FLASH->MODEKEYR = FLASH_KEY1;
	FLASH->MODEKEYR = FLASH_KEY2;

	// printf( "FLASH->CTLR = %08lx\n", FLASH->CTLR );
	if( FLASH->CTLR & 0x8080 ) 
	{
		printf( "Flash still locked\n" );
		return 1;
	}
	return 0;
}

int read_bank_into_ntag(int bank) {
	// printf("Writing data from bank %d into ntag \n", bank);
	// Extinguish LED to save power
	GPIOD->OUTDR &= ~(1<<6);
	
	for(int i=0; i<55; i++) {
		uint32_t *ptr = (uint32_t *) ((uint32_t)bank_addrs[bank] + (i<<4));
		// printf("Reading 1kb from bank %d (address) %lx \n", bank, (uint32_t) ptr);
		i2c_err_t i2c_stat = i2c_write(NTAG_ADDR, (i+1), (uint8_t*) ptr, 16);
		if(i2c_stat != I2C_OK) 
			printf("Error Using the I2C Bus\n");
		Delay_Us(160);
	}
	uint32_t *ptr = (uint32_t *) ((uint32_t)bank_addrs[bank] + (56<<4));
	i2c_err_t i2c_stat = i2c_write(NTAG_ADDR, (56), (uint8_t*) ptr, 8);
	if(i2c_stat != I2C_OK) 
		printf("Error Using the I2C Bus\n");
	Delay_Us(160);
	// printf("Transfer complete\n");

	uint32_t block[4] = {0, 0, 0, bank};
	i2c_stat = i2c_write(NTAG_ADDR, (64), (uint8_t*) block, 16);
	if(i2c_stat != I2C_OK) 
		printf("Error Using the I2C Bus\n");
	Delay_Us(160);
	// printf("Chosen bank written to tag\n");

	GPIOD->OUTDR |= (1<<6);


	return 0;
}

int write_ntag_into_bank(int bank) {
	// printf("Reading data from ntag and storing in flash at bank %d", bank);
	// There are 16 pages total to store in ch32 flash if we're reading 1kb out from chip, ch32 uses 64b pages
	// NTAG uses 16b blocks, so we need to do 4x ntag reads for each ch32 write

	if(unlock_flash())
		printf("Flash Locked!\n");

	// Extinguish LED to save power
	GPIOD->OUTDR &= ~(1<<6);

	int block_to_read = 1;
	for(int j=0; j<14; j++) {	
		uint32_t blocks[4][4] = {0};
		uint32_t * ptr = (uint32_t *) ((uint32_t)bank_addrs[bank] + (j<<6));

		// printf("Reading 4 blocks from %d to %d from ntag, writing to address %lx \n", block_to_read, block_to_read + 3, (uint32_t) ptr);

		for(int k=0; k<4; k++){
			i2c_read(NTAG_ADDR, block_to_read++, (uint8_t*) blocks[k], 16);
			printf("0x%lx, 0x%lx, 0x%lx, 0x%lx \n", blocks[k][0], blocks[k][1], blocks[k][2], blocks[k][3]);
		}

		// printf( "FLASH->CTLR = %08lx\n", FLASH->CTLR );

		//Erase Page
		FLASH->CTLR = CR_PAGE_ER;
		FLASH->ADDR = (intptr_t)ptr;
		FLASH->CTLR = CR_STRT_Set | CR_PAGE_ER;
		while( FLASH->STATR & FLASH_STATR_BSY );  // Takes about 3ms.

		//printf( "FLASH->STATR = %08lx cycles for page erase\n", FLASH->STATR;
		// printf( "Erase complete\n" );


		// printf( "Memory at %p: %08lx %08lx\n", ptr, ptr[0], ptr[1] );

		if( ptr[0] != 0xffffffff )
		{
			printf( "WARNING/FAILURE: Flash general erasure failed\n" );
		}


		// Clear buffer and prep for flashing.
		FLASH->CTLR = CR_PAGE_PG;  // synonym of FTPG.
		FLASH->CTLR = CR_BUF_RST | CR_PAGE_PG;
		FLASH->ADDR = (intptr_t)ptr;  // This can actually happen about anywhere toward the end here.


		// Note: It takes about 6 clock cycles for this to finish.
		while( FLASH->STATR & FLASH_STATR_BSY );  // No real need for this.
		//printf( "FLASH->STATR = %08lx -> %d cycles for buffer reset\n", FLASH->STATR, stop - start );


		int i;
		//start = SysTick->CNT;
		for( i = 0; i < 16; i++ )
		{
			// CH32V003 doen't have multiply, assumin no hardware divide too? this probably isn't fastest but ehh
			ptr[i] = blocks[i/4][i%4]; //Write to the memory
			FLASH->CTLR = CR_PAGE_PG | FLASH_CTLR_BUF_LOAD; // Load the buffer.
			while( FLASH->STATR & FLASH_STATR_BSY );  // Only needed if running from RAM.
		}
		//stop = SysTick->CNT;
		//printf( "Write: %d cycles for writing data in\n", stop - start );

		// Actually write the flash out. (Takes about 3ms)
		FLASH->CTLR = CR_PAGE_PG|CR_STRT_Set;

		//start = SysTick->CNT;
		while( FLASH->STATR & FLASH_STATR_BSY );
		//stop = SysTick->CNT;
		//printf( "FLASH->STATR = %08lx -> %d cycles for page write\n", FLASH->STATR, stop - start );
		// printf( "Memory at %p: %08lx %08lx\n", ptr, ptr[0], ptr[1] );
	}
	GPIOD->OUTDR |= (1<<6);
	return 0;
}

void read_and_dump_tag(void) {
	for(int i=1; i<56; i++){
		uint32_t block[4] = {0};
		i2c_err_t i2c_stat = i2c_read(NTAG_ADDR, i, (uint8_t*) block, 16);
		if(i2c_stat != I2C_OK) printf("Error Using the I2C Bus\n");
		for(int j=0; j<4; j++)
			printf("0x%lx, ", block[j]);
		printf("%d, \n", i);
	}
	// i=56 is a special case, 8 bytes of user data, 8 bytes of config
	uint32_t block[4] = {0};
	i2c_err_t i2c_stat = i2c_read(NTAG_ADDR, 56, (uint8_t*) block, 16);
	if(i2c_stat != I2C_OK) printf("Error Using the I2C Bus\n");
	for(int j=0; j<4; j++)
		printf("0x%lx, ", block[j]);

}


uint8_t get_active_bank(void) {
	uint32_t block[4] = {0};
	i2c_err_t i2c_stat = i2c_read(NTAG_ADDR, 64, (uint8_t*) block, 16);
	if(i2c_stat != I2C_OK) printf("Error Using the I2C Bus\n");
	for(int j=0; j<4; j++)
		printf("0x%lx, ", block[j]);
	return (uint8_t) block[3];
}

void process_button_1(bool pressed){
    if (pressed) {  
        if (!button_1_pressed) {  
			// If falling edge detected reset the counter timer
            button_1_pressed = true;
            button_1_press_start = (SysTick->CNT);
        }
    } else if (button_1_pressed) {  
		// If the button was released 
        uint32_t press_duration = (SysTick->CNT) - button_1_press_start;
        button_1_pressed = false;
        if (press_duration >= SHORT_PRESS_TIME && press_duration < LONG_PRESS_TIME) {
            if (mode == 1) {
				// printf("Med press detected, reading");
                read_bank_into_ntag(count);
				mode = 0;
				//bank = get_active_bank();
            } else if (mode == 2) {
				// printf("Med pres detected, writing");
                write_ntag_into_bank(count);
				mode = 0;
				//bank = get_active_bank();
            }
        }
		else if (press_duration > LONG_PRESS_TIME) {
			button_1_pressed = false;
			// printf("Long press \n");
			// Increment the mode
			mode = (mode + 1)%3;
			count = 0; // Reset count when switching modes
			last_interaction = (SysTick->CNT);
		}
        last_interaction = (SysTick->CNT);
    }
}

void process_button_2(bool pressed){

	if (mode == 0) return;  // Ignore Button 2 in Mode 0

	if (button_2_pressed_last && !pressed) {  // Detect button being lifted
		count = (count + 1)%4;
		last_interaction = (SysTick->CNT);

		// printf("Button 2 pressed\n");
	}
	button_2_pressed_last = pressed;
}

void start_blinking(uint8_t long_count, uint8_t short_count){
	if((SysTick->CNT)- blink_start >= BLINK_SPACE_TIME){
		target_long_blinks = long_count * 2;  
		target_short_blinks = short_count * 2;  
		blink_start = (SysTick->CNT);
		is_blinking = true;
		led_state = false;
		GPIOD->OUTDR &= ~(1<<6);
		// printf("Blink start, long %d, short %d", target_long_blinks, target_short_blinks);
	}
}

void update_blinking() {
	
    if (!is_blinking) {
		return;
	};

    uint32_t current_time = (SysTick->CNT);
	if(target_long_blinks){
		if (!led_state && (current_time - blink_start >= LONG_BLINK_TIME)) {
			led_state = true;
			GPIOD->OUTDR |= (1<<6);
			blink_start = current_time;
			target_long_blinks--;
			// printf("Long blink on");
		} 
		else if (led_state && (current_time - blink_start >= LONG_BLINK_TIME)) {
			led_state = false;
			GPIOD->OUTDR &= ~(1<<6);
			blink_start = current_time;
			target_long_blinks--;
			// printf("Long blink off");
		}
	} else if(target_short_blinks) {
		if (!led_state && (current_time - blink_start >= SHORT_BLINK_TIME)) {
			led_state = true;
			GPIOD->OUTDR |= (1<<6);
			blink_start = current_time;
			target_short_blinks--;
			// printf("short blink on");
		} 
		else if (led_state && (current_time - blink_start >= SHORT_BLINK_TIME)) {
			led_state = false;
			GPIOD->OUTDR &= ~(1<<6);
			blink_start = current_time;
			target_short_blinks--;
			// printf("short blink off");
		}
	}

    if ((target_long_blinks == 0) && (target_short_blinks == 0) && !led_state) {
		// printf("Targer blinks reached\n");
        is_blinking = false;
		blink_start = current_time;
    }
}