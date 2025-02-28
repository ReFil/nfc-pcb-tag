
#include "ch32fun.h"
#include <stdio.h>
#include "ch32v003_touch.h"
#include "lib_i2c.h"


#define NTAG_ADDR 0x02

uint32_t bongus[4][4] = {{0x0, 0x0, 0x0, 0x0}, 
{0x0, 0x0, 0x0, 0x0}, 
	{0x0, 0x0, 0x0, 0x0}, 
	{0x0, 0x0, 0x0, 0xff}, };

void read_and_dump_tag();


void i2c_scan_callback(const uint8_t addr)
{
	printf("Address: 0x%02X Responded.\n", addr);
}


int main()
{
	SystemInit();

	RCC->CFGR0 |= (12<<4); //slowdown for lower power

	// Enable GPIOD and ADC for capsense
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_ADC1;


	if(i2c_init(I2C_CLK_400KHZ) != I2C_OK) printf("Failed to init the I2C Bus\n");

	// Initialising I2C causes the pins to transition from LOW to HIGH.
	// Wait 100ms to allow the I2C Device to timeout and ignore the transition.
	// Otherwise, an extra 1-bit will be added to the next transmission
	Delay_Ms(100);
	i2c_scan(i2c_scan_callback);
	InitTouchADC();
	//Wait until a capsense button is pressed, then dump 1kb of data from ntag to debug printf
	while(1) {
		uint32_t touchval = ReadTouchPin( GPIOD, 5, 5, 3);
		if(touchval > 50) {
			printf("Button pressed, reading ntag, writing then reading again\n");
			uint32_t block[4] = {0};
			i2c_err_t i2c_stat = i2c_read(NTAG_ADDR, 56, (uint8_t*) block, 16);
			if(i2c_stat != I2C_OK) printf("Error Using the I2C Bus\n");
			for(int j=0; j<4; j++)
				printf("0x%lx, ", block[j]);
			
			block[2] = 0x0;
			block[3] = 0xff000000;
			i2c_stat = i2c_write(NTAG_ADDR, 56, (uint8_t*) block, 16);
			if(i2c_stat != I2C_OK) 
				printf("Error Using the I2C Bus\n");
			Delay_Ms(5);
		}
	}

}


void read_and_dump_tag(void) {
	for(int i=1; i<56; i++){
		uint32_t block[4] = {0};
		i2c_err_t i2c_stat = i2c_read(NTAG_ADDR, i, (uint8_t*) block, 16);
		if(i2c_stat != I2C_OK) printf("Error Using the I2C Bus\n");
		for(int j=0; j<4; j++)
			printf("0x%lx, ", block[j]);
		printf("\n");
	}
	// i=56 is a special case, 8 bytes of user data, 8 bytes of config
	uint32_t block[2] = {0};
		i2c_err_t i2c_stat = i2c_read(NTAG_ADDR, 56, (uint8_t*) block, 8);
		if(i2c_stat != I2C_OK) printf("Error Using the I2C Bus\n");
		for(int j=0; j<2; j++)
			printf("0x%lx, ", block[j]);
		printf("\n");

}