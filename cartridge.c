#include <stdint.h>
#include <stdio.h>

typedef struct {
	uint8_t data[0x4000];
} ROM_bank;

typedef struct {
	uint8_t data[0x2000];
} RAM_bank;

typedef struct {
	uint8_t bits_needed_for_ROM_banks;
	uint8_t single_RAM;
	uint8_t RAM_enable;
	uint8_t ROM_bank_num;
	uint8_t RAM_ROM_bank_num;
	uint8_t banking_mode;
	ROM_bank *active_ROM_bottom;
	ROM_bank *active_ROM;
	RAM_bank *active_RAM;
	ROM_bank ROM_banks[128];
	RAM_bank RAM_banks[4];
	RAM_bank no_RAM;
} cartridge;


void WriteEnableRAM(uint8_t data, cartridge *cart){
	if ((data & 0xF) == 0xA){
		cart->RAM_enable = 1;
	} else {
		cart->RAM_enable = 0;
	}
}

void WriteBankNumberROM(uint8_t data, cartridge *cart){
	data = data & 0b00011111;
	if (data == 0){ data = 1;}
	cart->ROM_bank_num = data;
}

void WriteBankRAMROM(uint8_t data, cartridge *cart){
	//printf("\nWriting RAMROM bank: %d\n", data);
	data = data & 0b00000011;
	cart->RAM_ROM_bank_num = data;
}

void WriteBankingMode(uint8_t data, cartridge *cart){
	cart->banking_mode = data & 0b00000001;
}

void SetActiveBanks(cartridge *cart){
	uint8_t upper_ROM;
	uint8_t ROM_mask = ((1 << cart->bits_needed_for_ROM_banks)-1);
	if (cart->banking_mode){
		uint8_t ROMRAM = cart->RAM_ROM_bank_num;
		uint8_t bottom_ROM = (ROMRAM << 5) & ROM_mask;
		upper_ROM = ((ROMRAM << 5) | cart->ROM_bank_num) & ROM_mask;
		
		cart->active_ROM_bottom = &cart->ROM_banks[bottom_ROM];
		cart->active_ROM = &cart->ROM_banks[upper_ROM];
		if(!cart->single_RAM){
			cart->active_RAM = &cart->RAM_banks[cart->RAM_ROM_bank_num];
		} else{
			cart->active_RAM = &cart->RAM_banks[0];
		}
	} else {
		cart->active_ROM_bottom = &cart->ROM_banks[0];
		cart->active_RAM = &cart->RAM_banks[0];
		upper_ROM = (cart->ROM_bank_num + (cart->RAM_ROM_bank_num << 5)) & ROM_mask;
		cart->active_ROM = &cart->ROM_banks[upper_ROM];
	}	
	if (!cart->RAM_enable){
		cart->active_RAM = &cart->no_RAM;
	}
}
