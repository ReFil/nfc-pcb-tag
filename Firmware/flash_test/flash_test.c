
#include "ch32fun.h"
#include <stdio.h>

uint32_t *bank_1_addr = (uint32_t*)0x08003000;
uint32_t *bank_2_addr = (uint32_t*)0x08003400;
uint32_t *bank_3_addr = (uint32_t*)0x08003800;
uint32_t *bank_4_addr = (uint32_t*)0x08003c00;

int unlock_flash();

int read_bank_into_ntag(int bank);

int write_ntag_into_bank(int bank);



int main()
{
	int start;
	int stop;
	int testok = 1;

	SystemInit();

	RCC->CFGR0 |= (12<<4); //slowdown for lower power

	Delay_Ms(100);

	printf( "Starting\n" );

	if(unlock_flash())
		printf("Flash Locked!\n");

	uint32_t * ptr = (uint32_t*)0x08003000;
	printf( "Memory at: %08lx: %08lx %08lx\n", (uint32_t)ptr, ptr[0], ptr[1] );

	printf( "FLASH->CTLR = %08lx\n", FLASH->CTLR );

	//Erase Page
	FLASH->CTLR = CR_PAGE_ER;
	FLASH->ADDR = (intptr_t)ptr;
	FLASH->CTLR = CR_STRT_Set | CR_PAGE_ER;
	start = SysTick->CNT;
	while( FLASH->STATR & FLASH_STATR_BSY );  // Takes about 3ms.
	stop = SysTick->CNT;

	printf( "FLASH->STATR = %08lx -> %d cycles for page erase\n", FLASH->STATR, stop - start );
	printf( "Erase complete\n" );


	printf( "Memory at %p: %08lx %08lx\n", ptr, ptr[0], ptr[1] );

	if( ptr[0] != 0xffffffff )
	{
		printf( "WARNING/FAILURE: Flash general erasure failed\n" );
		testok = 0;
	}


	// Clear buffer and prep for flashing.
	FLASH->CTLR = CR_PAGE_PG;  // synonym of FTPG.
	FLASH->CTLR = CR_BUF_RST | CR_PAGE_PG;
	FLASH->ADDR = (intptr_t)ptr;  // This can actually happen about anywhere toward the end here.


	// Note: It takes about 6 clock cycles for this to finish.
	start = SysTick->CNT;
	while( FLASH->STATR & FLASH_STATR_BSY );  // No real need for this.
	stop = SysTick->CNT;
	printf( "FLASH->STATR = %08lx -> %d cycles for buffer reset\n", FLASH->STATR, stop - start );


	int i;
	start = SysTick->CNT;
	for( i = 0; i < 16; i++ )
	{
		ptr[i] = 0xabcd1234 + i; //Write to the memory
		FLASH->CTLR = CR_PAGE_PG | FLASH_CTLR_BUF_LOAD; // Load the buffer.
		while( FLASH->STATR & FLASH_STATR_BSY );  // Only needed if running from RAM.
	}
	stop = SysTick->CNT;
	printf( "Write: %d cycles for writing data in\n", stop - start );

	// Actually write the flash out. (Takes about 3ms)
	FLASH->CTLR = CR_PAGE_PG|CR_STRT_Set;

	start = SysTick->CNT;
	while( FLASH->STATR & FLASH_STATR_BSY );
	stop = SysTick->CNT;
	printf( "FLASH->STATR = %08lx -> %d cycles for page write\n", FLASH->STATR, stop - start );

	printf( "FLASH->STATR = %08lx\n", FLASH->STATR );

	printf( "Memory at: %08lx: %08lx %08lx\n", (uint32_t)ptr, ptr[0], ptr[1] );


	if( ptr[0] != 0xabcd1234 )
	{
		printf( "WARNING/FAILURE: Flash general erasure failed\n" );
		testok = 0;
	}

	for( i = 0; i < 16; i++ )
		printf( "%08lx ", ptr[i] );
	printf( "\n\nTest results: %s\n", testok?"PASS":"FAIL" );
	while(1);
}

int unlock_flash() {
	// Unkock flash - be aware you need extra stuff for the bootloader.
	FLASH->KEYR = FLASH_KEY1;
	FLASH->KEYR = FLASH_KEY2;

	// For unlocking programming, in general.
	FLASH->MODEKEYR = FLASH_KEY1;
	FLASH->MODEKEYR = FLASH_KEY2;

	printf( "FLASH->CTLR = %08lx\n", FLASH->CTLR );
	if( FLASH->CTLR & 0x8080 ) 
	{
		printf( "Flash still locked\n" );
		return 1;
	}
	return 0;
}

int read_bank_into_ntag(int bank) {
	printf("Writing data from bank %d into ntag", bank);
	return 0;
}

int write_ntag_into_bank(int bank) {
	printf("Reading data from ntag and storing in flash at bank %d", bank);
	// There are 16 pages total to store in ch32 flash if we're reading 1kb out from chip, ch32 uses 64b pages
	// NTAG uses 16b blocks, so we need to do 4x ntag reads for each ch32 write

	return 0;
}