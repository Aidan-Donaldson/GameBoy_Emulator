#include <stdint.h>
#include "cartridge.c"

cartridge cart;
uint8_t VRAM[8192];
uint8_t WRAM[8192];
uint8_t OAM[160];
uint8_t HRAM[127];
uint8_t Unuseable[96];
uint8_t IO[128];
uint8_t interrupt_enable;
uint64_t M_cycle = 0;
uint64_t prev_M_cycle = 0;


uint8_t* ConvertAddress(uint16_t address){
	uint8_t *mem_start;
	if (address < 0x4000){
		mem_start = cart.active_ROM_bottom->data;
	} else if (address < 0x8000){
		address -= 0x4000;
		mem_start = cart.active_ROM->data;
	} else if (address < 0xA000){
		address -= 0x8000;
		mem_start = VRAM;
	} else if (address < 0xC000){
		address -= 0xA000;
		mem_start = cart.active_RAM->data;
	} else if (address < 0xE000){
		address -= 0xC000;
		mem_start = WRAM;
	} else if (address < 0xFE00){
		address -= 0xE000;
		mem_start = WRAM;
		// Prohibted -- Echo RAM
	} else if (address < 0xFEA0){
		address -= 0xFE00;
		mem_start = OAM;
	} else if (address < 0xFF00){
		address -= 0xFEA0;
		mem_start = Unuseable;
		// Prohibted Area -- some stuff happens (TODO look at what I should do with memstart here and above)
	} else if (address < 0xFF80){
		address -= 0xFF00;
		mem_start = IO;
	} else if (address < 0xFFFF){
		address -= 0xFF80;
		mem_start = HRAM;
	} else if (address == 0xFFFF){
		address = 0;
		mem_start = &interrupt_enable;
	}
	return mem_start + address;
}

void DMATransfer(uint8_t data){
	uint16_t source_addr = data * 0x100;
	for (int i=0; i<160; i++){
		uint8_t *addr = ConvertAddress(source_addr+i);
		OAM[i] = *addr;
	}
}

void WriteMem(uint16_t addr, uint8_t data){
	M_cycle++;
	if (addr < 0x2000){
		WriteEnableRAM(data, &cart);
	} else if (addr < 0x4000){
		WriteBankNumberROM(data, &cart);
	} else if (addr < 0x6000){
		WriteBankRAMROM(data, &cart);
	} else if (addr < 0x8000){
		WriteBankingMode(data, &cart);
	} else {
		uint8_t *new_addr = ConvertAddress(addr);
		*new_addr = data;
		if (new_addr == &IO[0x46]){
			DMATransfer(data);
		}
		if (new_addr == &IO[0x02] && data == 81){
			printf("%c", IO[0x01]);
			fflush(stdout);
		}
		if (new_addr == &IO[0x01]){
		//	printf("%c", data);
		//	fflush(stdout);
		}
		if (new_addr == &IO[0x04]){
			*new_addr = 0; // reset DIV -- need to add STOP stuff
		}
	}
	if (addr < 0x8000) { SetActiveBanks(&cart);}
}

uint8_t ReadMem(uint16_t addr){
	M_cycle++;
	uint8_t *new_addr = ConvertAddress(addr);
	return *new_addr;
}
