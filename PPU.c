#include "CPU.c"
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_scancode.h>
#include <stdint.h>
//#include <stdio.h>
#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_keyboard.h>

register_file cpu;
cartridge cart;
uint8_t STAT_line = 0;
uint8_t prev_STAT_line = 0;
uint8_t fine_TIMA = 0;

SDL_Color palette[4];

uint8_t windowY = 0;
uint8_t prev_obj_pixel = 255;
uint8_t bg_over_win = 0;
uint8_t pixel_priorities[256];

static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
const bool *key_states = NULL;
static Uint64 last_time = 0;

Uint64 PrevFrame = 0;

typedef struct{
	SDL_Color colours[8];
	uint8_t bottom;
} tile_row;

typedef struct{
	uint8_t y;
	uint8_t x;
	uint8_t tile_index; // position of tile
	uint8_t flags;
} OAM_entry;

typedef struct{
	OAM_entry entries[10];
	uint8_t num;
} ObjStore;

typedef struct{
	uint16_t bg_base;
	uint16_t win_base;
	uint8_t addressing_mode;
	uint8_t window_enable;
	uint8_t LCD_enable;
	uint8_t bg_win_enable;
	uint8_t obj_size;
	uint8_t obj_enable;
} LCDC_attrs;

typedef struct{
	SDL_Color colours[16];
	uint8_t top;
	uint8_t bottom;
	uint8_t empty;
} pixel_queue;

pixel_queue obj_queue;
pixel_queue bg_queue;
uint8_t *SCX;
uint8_t *SCY;
uint8_t *WX;
uint8_t *WY;
uint8_t *LY;
uint8_t *LYC;
uint8_t *STAT;
uint8_t *LCDC;
uint8_t *DMA;
uint8_t *BGPALETTE;
uint8_t *OBJPALETTE0;
uint8_t *OBJPALETTE1;

#define WINDOW_WIDTH 160
#define WINDOW_HEIGHT 144

int GetColourIndex(int high, int low, uint8_t palette_map){
	int index = (high << 1) + low;
	int shift = index * 2;
	uint8_t shiftedMap = palette_map >> shift;
	int colourIndex = shiftedMap & 3;
	return colourIndex;
}

tile_row ConvertToPalette(uint8_t lsb, uint8_t msb, int obj){
	tile_row row;
	uint8_t palette_map;
	row.bottom = 0;
	uint8_t bitmask = 1;
	for (int i=0; i<8; i++){
		int bit_high = msb & bitmask;
		int bit_low = lsb & bitmask;
		lsb = lsb >> 1; msb = msb >> 1;
		if (obj == 1){ palette_map = *OBJPALETTE0;}
		else if (obj == 2){ palette_map = *OBJPALETTE1;}
		else{ palette_map = *BGPALETTE;}
		row.colours[7-i] = palette[GetColourIndex(bit_high, bit_low, palette_map)];
		if (obj&&!(bit_high|bit_low)){ row.colours[i].a = SDL_ALPHA_TRANSPARENT;}
	}
	return row;
}

void pushToQueue(pixel_queue *q, tile_row data){
	if(!q->empty && q->top == q->bottom) {return;}
	q->empty = 0;
	int space = (q->top<q->bottom)? q->bottom-q->top : 16-(q->top-q->bottom);
	if (space >= 8){
		for(int i=0; i<8; i++){
			if ((q->top+1)%16 != q->bottom && (q->bottom+15)%16 != q->top){
				q->colours[q->top] = data.colours[i];
				q->top = (q->top+1)%16;
			}
		}
	}
}

SDL_Color GetBottom(int bottom, pixel_queue *q){
	return q->colours[bottom];
}

SDL_Color PopQueue(pixel_queue *q, int x_pos){
	SDL_Color c;
	c.a = SDL_ALPHA_TRANSPARENT, c.r = 255; c.g = 1; c.b = 255;
	if (q->empty){ return c;}
	c = GetBottom(q->bottom, q);
	//c = q->colours[bottom];
	q->bottom = ((q->bottom)+1)%16;
	if(q->bottom == q->top) { q->empty = 1;}
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
	SDL_RenderPoint(renderer, x_pos, *LY);
	return c;
}

LCDC_attrs GetLCDC(){
	LCDC_attrs attrs;
	attrs.bg_base = bit(3, *LCDC & 0b1000) ? 0x1C00 : 0x1800; 
	attrs.win_base = bit(6, *LCDC & 0b1000000) ? 0x1C00: 0x1800;
	attrs.addressing_mode= bit(4, *LCDC);
	attrs.window_enable = bit(5, *LCDC);
	attrs.LCD_enable = bit(7, *LCDC);
	attrs.obj_size = bit(2, *LCDC);
	attrs.obj_enable = bit(1, *LCDC);
	attrs.bg_win_enable = bit(0, *LCDC);
	return attrs;
}

tile_row FetchRow(LCDC_attrs attrs, uint8_t x, uint8_t y, int win){
	uint16_t y_offset = (y/8) *32;
	uint16_t tile_address;
	uint8_t tile_line = y & 7;
	uint8_t tile_ID;
	uint8_t lsb, msb;
	if(win){
		tile_ID = VRAM[attrs.win_base+x+y_offset];
	} else {
		tile_ID = VRAM[attrs.bg_base+x+y_offset];
	}

	if (!attrs.addressing_mode){
		if (tile_ID < 128){ tile_address = 0x1000+tile_ID*16+tile_line*2;}
		else { tile_address = 0x800+(tile_ID-128)*16+tile_line*2;} 
	} else { 
		tile_address = tile_ID*16+tile_line*2;
	}

	lsb = VRAM[tile_address++];
	msb = VRAM[tile_address];

	tile_row row = ConvertToPalette(lsb, msb, 0);

	return row;
}

tile_row FetchObjRow(OAM_entry object){
	uint16_t index = object.tile_index*16;
	uint16_t row_address = index+((*LY+16)-object.y)*2;
	uint8_t lsb, msb;
	lsb = VRAM[row_address++];
	msb = VRAM[row_address];

	tile_row row = ConvertToPalette(lsb, msb, ((object.flags & 16)>>4)+1);
	return row;
}

ObjStore OAMScan(LCDC_attrs attrs){
	int selected = 0;
	uint8_t range = 8+8*attrs.obj_size;
	ObjStore objs;
	objs.num = 0;
	for(int i=0; (i<0x9F) && (selected<10); i=i+4){
		int obj_y = OAM[i];
		if ((*LY+16 >= obj_y) && (*LY+16< obj_y + range)){
			OAM_entry entry;
			entry.y = obj_y;
			entry.x = OAM[i+1];
			entry.tile_index = OAM[i+2];
			entry.flags = OAM[i+3];
			objs.entries[objs.num] = entry;
			objs.num++;
			selected++;
		}
	}
	return objs;
}

void pushToObjQueue(tile_row row, int px, uint8_t obj_px){
	int y = 0;
	for (int i=obj_queue.bottom; i!=obj_queue.top && y<8; i=(i+1)%16, y++){
		uint8_t pixel_priority = pixel_priorities[px+y];
		if (obj_px < pixel_priority){
			obj_queue.colours[i] = row.colours[y];
			if(row.colours[y].a == SDL_ALPHA_TRANSPARENT){
				continue;
			}
			pixel_priorities[px+y] = obj_px;
		}
	}

	while(y<8 && (obj_queue.top != obj_queue.bottom || obj_queue.empty)){
		obj_queue.colours[obj_queue.top] = row.colours[y++];
		obj_queue.top = (obj_queue.top+1)%16;
		if(row.colours[y].a == SDL_ALPHA_TRANSPARENT){
			continue;
		}
		pixel_priorities[px+y] = obj_px;
	}
	obj_queue.empty = 0;
}

void ReplaceOAMFIFO(tile_row row, int x, OAM_entry obj){
	uint8_t obj_pixel;
	for (int i=0; i<8; i++){
		if (row.colours[i].a == SDL_ALPHA_OPAQUE){
			obj_pixel = x+i;
			break;
		}
	}
	bg_over_win = bit(7, obj.flags); // should be checked while mixing
	pushToObjQueue(row, x, obj_pixel);
}

void ObjectFetch(LCDC_attrs attrs, int x, ObjStore objs){
	if (!(bit(1, *LCDC))) { return;}
	for(int i=0; i<objs.num; i++){
		OAM_entry obj = objs.entries[i];
		if (obj.x == x){
			tile_row row = FetchObjRow(obj);
			if (bit(5, obj.flags)){
				for (int x=0; x<4; x++){
					SDL_Color temp = row.colours[i];
					row.colours[i] = row.colours[7-i];
				}
			}
			ReplaceOAMFIFO(row, x, obj);
		}
	}
}

/*void NotInWindow(LCDC_attrs attrs, uint8_t x, uint8_t fetchY, ObjStore objs){
	uint8_t fetchX;
	fetchX = (*SCX/8 + x/8) & 0x1F;
	fetchY = (*LY + *SCY) & 0xFF;
	tile_row row_data = FetchRow(attrs, fetchX, fetchY, 0);
	if (bg_queue.top == 0){ pushToQueue(&bg_queue, row_data);}
	ObjectFetch(attrs, x, objs);
	PopQueue(&bg_queue, x-7); // -7 because PPU starts at 7


}

void InWindow(LCDC_attrs attrs, uint8_t x, ObjStore objs, int win){
	uint8_t fetchY = windowY;
	if(!window) {
		windowY++;
		pixel_queue win_queue;
		bg_queue = win_queue;
		bg_queue.top = 0;
	}
	uint8_t fetchX = x/8;	
	tile_row row_data = FetchRow(attrs, fetchX, fetchY, 1);
	if (bg_queue.top == 0){ pushToQueue(&bg_queue, row_data);}
	ObjectFetch(attrs, x, objs);
	PopQueue(&bg_queue, x-7); // -7 because PPU starts at 7
}

void Mode3(LCDC_attrs attrs, ObjStore objs){
	uint8_t fetchX, fetchY;
	tile_row row_data;
	bg_queue.top = 0;
	obj_queue.top = 0;
	uint8_t window = 0;

	// initial fetch
	row_data = FetchRow(attrs, (*SCX)/8, *SCY + *LY, window);
	pushToQueue(&bg_queue, row_data);
	for (int i=0; i<((*SCX)&7); i++){
		PopQueue(&bg_queue, 255);
	}

	if (*LY >= *WY){
		for (int x=7; x<167; x++){
			if (x >= *WX){
				InWindow(attrs, x, objs, window);
				window = 1;
			} else {
				window = 0;
				fetchY = (*LY + *SCY) & 0xFF;
				NotInWindow(attrs, x, fetchY, objs);
			}	
		}
	} else{
		fetchY = (*LY + *SCY) & 0xFF;
		for (int x=7; x<167; x++){
			NotInWindow(attrs, x, fetchY, objs);
		}
	}
}*/

void MixFIFO(int x, LCDC_attrs attrs){
	if (!attrs.bg_win_enable){
		PopQueue(&obj_queue, x-7);
		SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
		SDL_RenderPoint(renderer, x-7, *LY);
		return;
	}
	if(bg_over_win){
		pixel_queue temp = bg_queue;
		SDL_Color c = PopQueue(&temp, x-7);
		SDL_Color d = palette[GetColourIndex(0, 0, *BGPALETTE)];
		if(c.a == d.a && c.r == d.r && c.g == d.g && c.b == d.b){
			PopQueue(&bg_queue, x-7);
			PopQueue(&obj_queue, x-7);	
		} else {
			PopQueue(&obj_queue, x-7);
			PopQueue(&bg_queue, x-7);
		}
	} else {
		PopQueue(&bg_queue, x-7);
		PopQueue(&obj_queue, x-7);
	}
}

void Mode3(LCDC_attrs attrs, ObjStore objs){
	uint8_t fetchX, fetchY;
	tile_row row_data;
	uint8_t window = 0;

	// initial fetch
	row_data = FetchRow(attrs, (*SCX)/8, *SCY + *LY, window);
	pushToQueue(&bg_queue, row_data);
	for (int i=0; i<((*SCX)&7); i++){
		PopQueue(&bg_queue, 255);
	}

	for (int i=0; i<7; i++){
		ObjectFetch(attrs, i, objs);
		PopQueue(&obj_queue, 255);
	}

	if ((*LY >= *WY) && attrs.window_enable){ // add to attrs?
		for (int x=7; x<167; x++){
			if (x >= *WX){
				fetchY = windowY;
				if(!window) {
					window = 1;
					windowY++;
					pixel_queue win_queue;
					bg_queue = win_queue;
					bg_queue.top = 0;
					bg_queue.empty = 1;
					bg_queue.bottom = 0;
				}
				fetchX = x/8;
			} else {
				fetchX = (*SCX/8 + x/8) & 0x1F;
				fetchY = (*LY + *SCY) & 0xFF;
			}
			row_data = FetchRow(attrs, fetchX, fetchY, window);
			
			if (bg_queue.empty){ pushToQueue(&bg_queue, row_data);}
			ObjectFetch(attrs, x, objs);
			MixFIFO(x, attrs);
			//PopQueue(&bg_queue, x-7); // -7 because PPU starts at 7
		}
	} else{
		fetchY = (*LY + *SCY) & 0xFF;
		for (int x=7; x<167; x++){
			fetchX = (*SCX/8 + x/8) & 0x1F;
			row_data = FetchRow(attrs, fetchX, fetchY, window);
			
			if (bg_queue.empty){ pushToQueue(&bg_queue, row_data);}
			ObjectFetch(attrs, x, objs);
			MixFIFO(x, attrs);
			//PopQueue(&bg_queue, x-7);
		}
	}
}

int SetSTAT(uint8_t ppu_mode){
	static uint8_t prev = 2;
	static uint8_t returned = 1;
	uint8_t ret_val = 0;

	*STAT &= 0b11111000;
	*STAT |= ppu_mode;
	if(*LY == *LYC) { *STAT |= 0b00000100;}

	if (returned){
		uint8_t STAT_line = 0;
		if (*LY == *LYC){
			if(*STAT & 0b01000000){
				STAT_line = 1;
				prev = ppu_mode;
				returned = 0;
			}
		}
		if (ppu_mode < 3){
			if (*STAT & (1 << (3 + ppu_mode))){
				STAT_line = 1;
				prev = ppu_mode;
				returned = 0;
			}
		}

		if(STAT_line > prev_STAT_line){
			IO[0xF] |= 0b00000010;
			ret_val = 1;
		}

		prev_STAT_line = STAT_line;
	}

	if (ppu_mode == prev) {returned = 1;}

	return ret_val;
}

void RenderLine(){
	bg_queue.top = 0; bg_queue.bottom = 0; bg_queue.empty = 1;
	obj_queue.top = 0; obj_queue.bottom = 0; obj_queue.empty = 1;
	LCDC_attrs attrs = GetLCDC();

	if (!attrs.LCD_enable){
		SetSTAT(0);
		SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
		SDL_RenderClear(renderer); 
		return;
	}

	if (*LY > 143){
		// vblank
		if (*LY == 144){
			IO[0xF] |= 1;
		}
		if(SetSTAT(1)) {return;}
		(*LY)++;
		
		if (*LY==154){
			*LY=0;
			windowY = 0;
		}
		return;
	}
	
	for (int i=0; i<255; i++){ pixel_priorities[i] = 255;}
	pixel_priorities[255] = 255;

	if(SetSTAT(2)){return;}
	ObjStore objs = OAMScan(attrs);
	if(SetSTAT(3)){return;}
	Mode3(attrs, objs);
	if(SetSTAT(0)){return;}
	(*LY)++;
}

void BootSequence(){
	interrupt_enable = 0;
	cpu.PC = 0x100;
	cpu.SP = 0xFFFE;
	cpu.acc = 0x01;
	cpu.flags = 0xB0;
	cpu.GPR[0] = 0; cpu.GPR[1] = 0x13; cpu.GPR[2] = 0;
	cpu.GPR[3] = 0xD8; cpu.GPR[4] = 0x01; cpu.GPR[5] = 0x4D;
	*LCDC = 0x91;
	*STAT = 0x85;
	cpu.IE = 1;
	cpu.halted = 0;
	M_cycle = 0;

	RAM_bank no_ram;
	for (int i=0; i<0x2000; i++){ no_ram.data[i] = 255;}
	cart.no_RAM = no_ram;
	cart.bits_needed_for_ROM_banks = 2;
	cart.single_RAM = 1;
	WriteBankNumberROM(0, &cart);
	WriteBankingMode(0, &cart);
	WriteBankRAMROM(0, &cart);
	WriteEnableRAM(0, &cart);
	SetActiveBanks(&cart);

	FILE *ptr;
	uint8_t buffer[0x4000];
	ptr = fopen("cpu_instrs.gb", "rb");
	for (int i=0; i<128; i++){
		fread(buffer, sizeof(buffer), 1, ptr);
		for (int x=0; x<0x4000; x++) { cart.ROM_banks[i].data[x] = buffer[x];} // better way??
	}
	fclose(ptr);
	printf("\n\n");
}

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
	SDL_SetAppMetadata("Example Renderer Points", "1.0", "com.example.renderer-points");

	if (!SDL_Init(SDL_INIT_VIDEO)) {
		SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        	return SDL_APP_FAILURE;
	}

	if (!SDL_CreateWindowAndRenderer("examples/renderer/points", WINDOW_WIDTH, WINDOW_HEIGHT, SDL_WINDOW_RESIZABLE, &window, &renderer)) {
		SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        	return SDL_APP_FAILURE;
	}
	SDL_SetRenderLogicalPresentation(renderer, WINDOW_WIDTH, WINDOW_HEIGHT, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	uint8_t data = 0;
	for (int i=0; i < 0x1800; i++){
		if (i % 2 == 0){
			data = 0b01011001;
			if(i%4==0){
				data = 0b00110101;
			}
		} else {
			data = 0b00110101;
			if(i%3==0){
				data = 0b01011001;
			}
		}
		VRAM[i] = data;
	}
	for (int i=0; i<16; i++) {VRAM[i] = 0b11111111;}
	for (int i=0x1800; i<0x2000; i++){
		if (i % 3){ VRAM[i] = 0;} else{ VRAM[i] = 2;}
	}


	for(int i=0; i<0x9F; i++){ OAM[i] = 0;}
	OAM[4] = 21;
	OAM[5] = 140;
	OAM[6] = 0;
	OAM[7] = 0b00000000;
	OAM[0] = 110;
	OAM[1] = 8;
	OAM[2] = 1;
	OAM[3] = 0b00000000;

	for (int i=0; i<128; i++){
		IO[i] = 0;
	}
	
	IO[0x0] = 0b00101111;	

	DMA = &IO[0x46];
	LCDC = &IO[0x40];
	STAT = &IO[0x41];
	SCY = &IO[0x42];
	SCX = &IO[0x43];
	LY = &IO[0x44];
	LYC = &IO[0x45];
	BGPALETTE = &IO[0x47];
	OBJPALETTE0 = &IO[0x48];
	OBJPALETTE1 = &IO[0x49];
	WY = &IO[0x4A];
	WX = &IO[0x4B];

	*LCDC = 0b10110111;
	//*LYC = 24;
	*WX = 0;
	*WY = 0;
	*SCX = 0;
	*SCY = 0;
	*BGPALETTE = 0b11100100;
	*OBJPALETTE1 = 0b11100100;
	*OBJPALETTE0 = 0b10110001;
	SDL_Color colour;
	colour.a = SDL_ALPHA_OPAQUE;
	colour.r = 200;
	colour.g = 240;
	colour.b = 250;
	palette[0] = colour;
	colour.r = 250;
	colour.g = 200;
	colour.b = 220;
	palette[1] = colour;
	colour.r = 150;
	colour.g = 100;
	colour.b = 120;
	palette[2] = colour;
	colour.r = 100;
	colour.g = 140;
	colour.b = 150;
	palette[3] = colour;
	BootSequence();

	key_states = SDL_GetKeyboardState(NULL);


	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	last_time = SDL_GetTicks();

	return SDL_APP_CONTINUE;  /* carry on with the program! */
}

void JoypadInput(){
	uint8_t joypad = 0b00001111;
	uint8_t temp = IO[0x0]; 
	if (IO[0x0] <= 0b00001111) {
		IO[0x0] = 0b00001111;
	} else{
		if (bit(5, IO[0x0])){ // detect buttons
			joypad |= 0b00100000;
			if (key_states[SDL_SCANCODE_W]){ joypad &= 0b00111011;}
			if (key_states[SDL_SCANCODE_S]){ joypad &= 0b00110111;}
			if (key_states[SDL_SCANCODE_A]){ joypad &= 0b00111101;}
			if (key_states[SDL_SCANCODE_D]){ joypad &= 0b00111110;}
		}
		if (bit(4, IO[0x0])) { // detect d-pad
			joypad |= 0b00010000;
			if (key_states[SDL_SCANCODE_J]){ joypad &= 0b00111110;}
			if (key_states[SDL_SCANCODE_K]){ joypad &= 0b00111101;}
			if (key_states[SDL_SCANCODE_F]){ joypad &= 0b00110111;}
			if (key_states[SDL_SCANCODE_G]){ joypad &= 0b00111011;}
		}
		IO[0x0] = joypad;
	}
	if (IO[0x0] < temp) { IO[0xF] |= 0b00010000;}
}

void ISR(register_file *cpu, uint8_t interrupts){
	cpu->IE = 0;
	uint8_t mask = 1;
	for(uint8_t mask_shift = 1; mask_shift <= 5; mask_shift++){
		if (interrupts & mask){
			IO[0xF] = IO[0xF] & ~mask;
			WriteMem(--cpu->SP, msb(cpu->PC));
			WriteMem(--cpu->SP, lsb(cpu->PC));
			cpu->PC = 0x38 + 0x8 * mask_shift;
			M_cycle += 3;
			return;
		}
			
		mask = mask << 1;
	}
}


void FetchCycle(register_file *cpu){
	prev_M_cycle = M_cycle;
	uint8_t interrupts = interrupt_enable & IO[0xF];
	if (interrupts) { cpu->halted = 0;}
	if (cpu->halted) { M_cycle++; return;}
	if(interrupts && cpu->IE){ // check for requested interrupts
		//printf("Pre-interrupt instr: %x", cpu->IR);
		ISR(cpu, interrupts);	
		return;
	}
	cpu->IR = ReadMem(cpu->PC++);
	DecodeInstruction(cpu, cpu->IR);
}

void ManageTimers(){
	if ((M_cycle&63)<=5 && (prev_M_cycle&63)>57){
		IO[0x4]++; // extra info regarding this register on docs.
	}
	if (bit(2, IO[0x7])){
		fine_TIMA += M_cycle - prev_M_cycle;
		uint8_t curr_TIMA = IO[0x5];
		uint8_t clock_select = IO[0x7] & 0b11;
		switch (clock_select){
			case 0:
				if ((M_cycle&255)<=5 && (prev_M_cycle&255)>249){ IO[0x5]++;}
				break;
			case 1:
				while (fine_TIMA >= 4){
					IO[0x5]++;
					fine_TIMA -= 4;
				}
				break;
			case 2:
				if ((M_cycle&15)<=5 && (prev_M_cycle&15)>9){ IO[0x5]++;}
				break;
			case 3: if ((M_cycle&63)<=5 && (prev_M_cycle&63)>57) {IO[0x5]++;}
				break;
			}
		if(IO[0x5] == 0 && curr_TIMA == 255){ 
			IO[0x5] = IO[0x6];
			IO[0xF] |= 0b00000100; // timer interrupt request
		}
	}
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT) {
        return SDL_APP_SUCCESS;  /* end the program, reporting success to the OS. */
    }
    return SDL_APP_CONTINUE;  /* carry on with the program! */
}

//uint8_t test = 0;
// This function runs once per frame
SDL_AppResult SDL_AppIterate(void *appstate)
{
	JoypadInput();
	const Uint64 now = SDL_GetTicks();
	const float elapsed = ((float) (now - last_time));
	if (elapsed >= 1.0f/59.6f*1000.0f){
		//(*SCX) = (*SCX) + 1; //seems to affect sprites at y tile when > 0
		(*SCY) = (*SCY) + 0;
		(*WX) = ((*WX) + 0)%160;
		//(*WY) = ((*WY) + 1)%144;
		//if (!(IO[0x0] & 0b00000001)){ OAM[1] = OAM[1] + 1;}
		//if (!(IO[0x0] & 0b00001000)){ OAM[0] = OAM[0] + 1;}
		//if (!(IO[0x0] & 0b00000010)){ OAM[1] = OAM[1] - 1;}
		//if (!(IO[0x0] & 0b00000100)){ OAM[0] = OAM[0] - 1;}
		//test = !test;
		SDL_RenderPresent(renderer);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
		SDL_RenderClear(renderer);  // start with blank canvas
		Uint64 temp = SDL_GetTicks();
		PrevFrame = temp;
		last_time = now;
	} else{
		ManageTimers();	
		FetchCycle(&cpu);
		//RenderLine();
		if (M_cycle%114<=5 && prev_M_cycle%114 > 107){
			RenderLine();
		}
	}
	return SDL_APP_CONTINUE;
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    /* SDL will clean up the window/renderer for us. */
}
