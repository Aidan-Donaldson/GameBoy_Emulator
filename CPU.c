#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include "memory.c"

typedef struct{
	uint8_t IR;
	uint8_t IE; // Interrupt Enable
	uint8_t acc;
	uint8_t flags;
	uint8_t GPR[6];
	uint16_t PC;
	uint16_t SP; // Stack Pointer
	uint8_t halted;
} register_file;

typedef struct{
	uint8_t ans;
	uint8_t carries[8];
} bitwise_arithmetic;

bitwise_arithmetic Add8Bits(uint8_t operand1, uint8_t operand2){
	bitwise_arithmetic results;
	results.ans = operand1 + operand2;
	for (int i=0; i<8; i++){
		uint8_t operand1_multiple = operand1 & 1;
		uint8_t operand2_multiple = operand2 & 1;
		uint8_t both_multiple = (operand1_multiple & operand2_multiple);
		uint8_t one_multiple = operand1_multiple | operand2_multiple;
		results.carries[i] = both_multiple;
		if (i > 0){
			results.carries[i] = 
				results.carries[i] | (results.carries[i-1] & one_multiple);
		}
		operand1 = operand1 >> 1;
		operand2 = operand2 >> 1;
	}
	return results;
}

bitwise_arithmetic Add8BitsCarry(uint8_t operand1, uint8_t operand2, uint8_t carry){
	bitwise_arithmetic results;
	results.ans = operand1 + operand2 + carry;
	for (int i=0; i<8; i++){
		uint8_t operand1_multiple = operand1 & 1;
		uint8_t operand2_multiple = operand2 & 1;
		uint8_t both_multiple = (operand1_multiple & operand2_multiple);
		uint8_t one_multiple = operand1_multiple | operand2_multiple;
		results.carries[i] = both_multiple;
		if (i > 0){
			results.carries[i] = 
				results.carries[i] | (results.carries[i-1] & one_multiple);
		} else if (one_multiple && carry){ results.carries[0] = 1;}
		operand1 = operand1 >> 1;
		operand2 = operand2 >> 1;
	}
	return results;
}

uint8_t lsb(uint16_t pair){
	uint8_t byte = pair & 0x00FF;
	return byte;
}

uint8_t msb(uint16_t pair){
	uint8_t byte = (pair & 0xFF00)>>8;
	return byte;
}

uint8_t bit(int index, uint8_t data){
	return (data & (1 << index)) >> index;
}

uint8_t* ConvertToRegister(register_file *file, uint8_t operand1){
	if (operand1 == 7){ return &file->acc;}
	else if (operand1 == 6){ return &file->flags;} // Should be [HL]
	else { return &file->GPR[operand1];}
}

uint8_t Circular8BitsLeft(register_file *file, uint8_t data){
	uint8_t top = bit(7, data);
	file->flags = top << 4;
	data = (data << 1) | top;
	return data;
}

uint8_t Circular8BitsRight(register_file *file, uint8_t data){
	uint8_t bottom = bit(0, data);
	file->flags = bottom << 4;
	data = (data >> 1) | bottom << 7;
	return data;
}

uint8_t Rotate8BitsLeft(register_file *file, uint8_t data){
	uint8_t top = bit(7, data);
	uint8_t carry = bit(4, file->flags);
	file->flags = top << 4;
	return (data << 1) | carry;
}

uint8_t Rotate8BitsRight(register_file *file, uint8_t data){
	uint8_t bottom = bit(0, data);
	uint8_t carry = bit(4, file->flags);
	file->flags = bottom << 4;
	return (data >> 1) | (carry << 7);
}

uint8_t Arithmetic8BitsLeft(register_file *file, uint8_t data){
	uint8_t top = bit(7, data);
	file->flags = top << 4;
	return data << 1;
}

uint8_t Arithmetic8BitsRight(register_file *file, uint8_t data){
	uint8_t bottom = bit(0, data);
	file->flags = bottom << 4;
	uint8_t top = bit(7, data);
	return (data >> 1) | top << 7;
	
}

uint8_t Logical8BitsRight(register_file *file, uint8_t data){
	uint8_t bottom = bit(0, data);
	data = data >> 1;
	file->flags = bottom << 4;
	if (data == 0) { file->flags |= 0b10000000;}
	return data;
}


uint8_t GetAdditionFlags(bitwise_arithmetic results){
	uint8_t temp_flags = 0b00000000;
	if (results.ans == 0){ temp_flags = temp_flags | 0b10000000;}
	if (results.carries[3]){ temp_flags = temp_flags | 0b00100000;}
	if (results.carries[7]){ temp_flags = temp_flags | 0b00010000;}
	return temp_flags;
}

uint8_t GetSubtractionFlags(uint8_t operand1, uint8_t operand2){
	uint8_t flags = 0b01000000;
	if (operand1 < operand2){
		flags |= 0b00010000;
	} else if (operand1 == operand2){
		flags |= 0b10000000;
	}
	if ((operand1 & 0x0F) < (operand2 & 0x0F)){
		flags |= 0b00100000;
	}
	return flags;
}

uint16_t ConcatenatePair(register_file *file, int operand1){
	if (operand1 == 3){
		return file->SP;
	}
	uint8_t msb = file->GPR[2*operand1];
	uint8_t lsb = file->GPR[2*operand1+1];
	uint16_t data = msb << 8 | lsb;
	return data;
}

void Write16BitReg(register_file *file, uint16_t data, int operand1){
	if (operand1 == 3){
		file->SP = data;
		return;
	}
	uint8_t lsb = data & 0x00FF;
	uint8_t msb = (data & 0xFF00) >> 8;
	file->GPR[2*operand1] = msb;
	file->GPR[2*operand1+1] = lsb;
}

int ConditionalCheck(register_file *file, uint8_t operand1){
	switch (operand1){
		case 0:
			if (!bit(7, file->flags)){ return 1;}
			break;
		case 1:
			if (bit(7, file->flags)){ return 1;}
			break;
		case 2:
			if (!bit(4, file->flags)){ return 1;}
			break;
		case 3:
			if (bit(4, file->flags)){ return 1;}
			break;
		default:
			return 0;
	}
	return 0;
} // can make this nicer


// -------------------- 8-bit loads -------------------------------

// opcode - 0b01xxxyyy -- two gprs
void Load8BitRegister(register_file *file, int operand1, int operand2){
	uint8_t *reg1 = ConvertToRegister(file, operand1);
	uint8_t *reg2 = ConvertToRegister(file, operand2);
	*reg1 = *reg2;
}

// opcode 0b00xxx110
void Load8BitImmediate(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	*reg = ReadMem(file->PC++);
}

// opcode 0b01xxx110 -- uses HL gpr pair (4-5) for 16-bit addressing
void Load8BitIndirectHL(register_file *file, int operand1){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t *reg = ConvertToRegister(file, operand1);
	*reg = ReadMem(address); 
}

// BELOW NEEDS TESTED

// opcode 0b01110xxx -- writes value to HL-soecified memeory 
void Load8BitToHL(register_file *file, int operand1){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t *reg = ConvertToRegister(file, operand1);
	WriteMem(address, *reg);
}

// opcode 0b00110110 -- Loads immediate data to HL-specified memory
void Load8bitImmediateHLDirect(register_file *file){
	uint8_t data = ReadMem(file->PC++);
	uint16_t address = ConcatenatePair(file, 2);
	WriteMem(address, data);
}

// opcode 0b00001010 -- Loads Accumulator with value from BC address
void LoadAccumulatorFromBC(register_file *file){
	uint16_t address = ConcatenatePair(file, 0);
	file->acc = ReadMem(address);
}

//opcode 0b000011010 -- Loads Accumulator with value from DE address
void LoadAccumulatorFromDE(register_file *file){
	uint16_t address = ConcatenatePair(file, 1);
	file->acc = ReadMem(address);
}

// opcode 0b00000010 -- Loads value from Accumulator to BC-specified address
void LoadAccumulatorToBC(register_file *file){
	uint16_t address = ConcatenatePair(file, 0);
	WriteMem(address, file->acc);
}

// opcode 0b00010010 -- Loads value from Accumulator to DE-specified address
void LoadAccumulatorToDE(register_file *file){
	uint16_t address = ConcatenatePair(file, 1);
	WriteMem(address, file->acc);
}

// opcode 0b11111010 -- Loads Accumulator to location specified by 16 bit operand
void LoadAccumulatorFromDirect(register_file *file){
	uint8_t lsb = ReadMem(file->PC++);
	uint8_t msb = ReadMem(file->PC++);
	uint16_t address = msb << 8 | lsb;
	uint8_t data = ReadMem(address);
	file->acc = data;
}

// opcode 0b11101010 -- Loads Accumulator to address specified by operand
void LoadAccumulatorToDirect(register_file *file){
	uint8_t lsb = ReadMem(file->PC++);
	uint8_t msb = ReadMem(file->PC++);
	uint16_t address = msb << 8 | lsb;
	WriteMem(address, file->acc);
}

// opcode 0b11110010 -- Loads Accumulator with high as 0xFF and low from C;
void LoadAccumulatorHighC(register_file *file){
	uint16_t addr = 0xFF00 | file->GPR[1];
	uint8_t data = ReadMem(addr);
	file->acc = data;
}

// opcode 0b11100010 -- Loads from Accumulator to 0xFFcc
void LoadFromAccumulatorHighC(register_file *file){
	uint16_t addr = 0xFF00 | file->GPR[1];
	WriteMem(addr, file->acc);
}

// opcode 0b11110000 -- Loads Accumulator with 0xFF00 + immediate address 
void LoadAccumulatorHighDirect(register_file *file){
	uint16_t address = 0xFF00 | ReadMem(file->PC++);
	file->acc = ReadMem(address);
}

// opcode 0b11100000 -- Loads 0xFF00 + immediate with Accumulator
void LoadFromAccumulatorHighDirect(register_file *file){
	uint16_t address = 0xFF00 | ReadMem(file->PC++);
	WriteMem(address, file->acc);	
}

// opcode 0b00111010 -- Loads Accumulator with adresss specified by HL which is then decremented
void LoadAccumulatorDirectHLDecrement(register_file *file){
	uint16_t HL = ConcatenatePair(file, 2);
	Write16BitReg(file, HL-1, 2);
	file->acc = ReadMem(HL);
}

// opcode 0b00110010 -- Loads Accumulator to Address specified by HL which is then decremented
void LoadFromAccumulatorDirectHLDecrement(register_file *file){
	uint16_t HL = ConcatenatePair(file, 2);
	Write16BitReg(file, HL-1, 2);
	WriteMem(HL, file->acc);
}

// opcode 0b00101010 -- Load Accumulator with data specified by HL which is then incremented
void LoadAccumulatorDirectHLIncrement(register_file *file){
	uint16_t HL = ConcatenatePair(file, 2);
	Write16BitReg(file, HL+1, 2);
	file->acc = ReadMem(HL);
}

// opcode 0b00100010 -- Load Accumulator to Address specified by HL which is then incremented
void LoadFromAccumulatorDirectHLIncrement(register_file *file){
	uint16_t HL = ConcatenatePair(file, 2);
	Write16BitReg(file, HL+1, 2);
	WriteMem(HL, file->acc);
}

// --------------------- 16-bit loads ----------------------

// opcode 0b00xx0001 -- Load to selected 16 bit register, the immediate 16 bit data
void Load16BitImmediate(register_file *file, int operand1){
	uint8_t lsb = ReadMem(file->PC++);
	uint8_t msb = ReadMem(file->PC++);
	uint16_t data = msb << 8 | lsb;
	Write16BitReg(file, data, operand1);
}

// opcode 0b00001000 -- Load to address specified by operand data from Stack Pointer
void Load16FromSPDirect(register_file *file){
	uint8_t low = ReadMem(file->PC++);
	uint8_t high = ReadMem(file->PC++);
	uint16_t address = high << 8 | low;
	WriteMem(address++, lsb(file->SP));
	WriteMem(address, msb(file->SP));
}

// opcode 0b11111001 -- Load value from HL pair to Stack Pointer
void LoadSPFromHL(register_file *file){
	uint16_t HL = ConcatenatePair(file, 2);
	file->SP = HL;
}


// opcode 0b11xx0101 -- Push to stack memory data from 16-bit register
void Push16BitToStack(register_file *file, int operand1){
	uint8_t lsb, msb;
	if (operand1 < 3){ // Stack uses AF instead of SP
		msb = file->GPR[2*operand1];
		lsb = file->GPR[2*operand1+1];
	}
	else {
		msb = file->acc;
		lsb = file->flags;
	}
	WriteMem(--file->SP, msb);
	WriteMem(--file->SP, lsb);
	M_cycle++;
}

// opcode 0b11xx0001 -- Pops to 16-bit register data from stack memory
void PopTo16Bit(register_file *file, int operand1){
	uint8_t lsb = ReadMem(file->SP++);
	uint8_t msb = ReadMem(file->SP++);
	uint16_t data = msb << 8 | lsb;

	if (operand1 < 3){ Write16BitReg(file, data, operand1);} // Stack uses AF instead of SP
	else {
		file->acc = msb;
		file->flags = lsb & 0xF0; // Flags lower nibble hard-wired to 0
	}
}

// opcode 0b11111000 -- Loads to HL sum of signed 8-bit e and SP
void LoadSumHL(register_file *file){
	uint8_t e = ReadMem(file->PC++);
	uint8_t e_sign = bit(7, e);
	bitwise_arithmetic results = Add8Bits(lsb(file->SP), e);
	file->GPR[5] = results.ans;
	file->flags = 0b00000000; // Done manually as Z flag always 0. ZNHCuuuu
	if (results.carries[3]){ file->flags = file->flags | 0b00100000;}
	if (results.carries[7]){ file->flags = file->flags | 0b00010000;}
	uint8_t sign_extension;
	if (e_sign == 1){ sign_extension = 0xFF;} else{sign_extension = 0x00;}
	file->GPR[4] = msb(file->SP) + sign_extension + results.carries[7];
	M_cycle++;
}

// -------------------- 8-bit arithmetic and logic -----------------

// opcode 0b10000xxx -- Adds register to Accumulator
void AddRegister(register_file *file, uint8_t operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	bitwise_arithmetic results = Add8Bits(file->acc, *reg);
	file->acc = results.ans;
	file->flags = GetAdditionFlags(results);

}


// opcode 0b10000110 -- Adds HL Direct data to Accumulator
void AddHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	bitwise_arithmetic results = Add8Bits(file->acc, data);
	file->acc = results.ans;
	file->flags = GetAdditionFlags(results);
}

// opcode 0b11000110 -- Adds immediate value to Accumulator
void AddImmediate(register_file *file){
	static uint8_t x = 0;
	uint8_t data = ReadMem(file->PC++);
	bitwise_arithmetic results = Add8Bits(file->acc, data);
	file->acc = results.ans;
	file->flags = GetAdditionFlags(results);
}

// opcode 0b10001xxx -- Adds register with carry to Accumulator;
void AddRegisterCarry(register_file *file, int operand1){
	uint8_t carry = bit(4, file->flags);
	bitwise_arithmetic results1 = Add8Bits(file->acc, *ConvertToRegister(file, operand1));
	bitwise_arithmetic results2 = Add8Bits(results1.ans, carry);
	file->acc = results2.ans;
	uint8_t flags1 = GetAdditionFlags(results1);
	uint8_t flags2 = GetAdditionFlags(results2);
	file-> flags = (flags1 & 0b01110000) | flags2;
}

// opcode 0b10001110 -- Adds data from HL address and carry flag to Accumulator
void AddHLDirectCarry(register_file *file){
	uint8_t carry = bit(4, file->flags);
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	bitwise_arithmetic results1 = Add8Bits(file->acc, data);
	bitwise_arithmetic results2 = Add8Bits(results1.ans, carry);
	file->acc = results2.ans;
	uint8_t flags1 = GetAdditionFlags(results1);
	uint8_t flags2 = GetAdditionFlags(results2);
	file-> flags = (flags1 & 0b01110000) | flags2;
}

// opcode 0b11001110 -- Adds immediate and carry flag to Accumulator 
void AddImmediateCarry(register_file *file){
	uint8_t carry = bit(4, file->flags); // could change next
	AddImmediate(file);
	file->flags = file->flags & 0b01110000;
	bitwise_arithmetic results = Add8Bits(file->acc, carry);
	file->acc = results.ans;
	file->flags |= GetAdditionFlags(results);
}

// opcode 0b10010xxx -- Subtracts register value from Accumulator
void SubtractRegister(register_file *file, int operand1){
	uint8_t data = *ConvertToRegister(file, operand1);
	file->flags = GetSubtractionFlags(file->acc, data);
	file->acc -= data;
}

// opcode 0b10010110 -- Similarly to the addition
void SubtractHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	file->flags = GetSubtractionFlags(file->acc, data);
	file->acc -= data;
}

// opcode 0b11010110 -- Subtract immediate data from Accumulator
void SubtractImmediate(register_file *file){
	uint8_t data = ReadMem(file->PC++);
	file->flags = GetSubtractionFlags(file->acc, data);
	file->acc -= data;
}

void DoSubtractCarry(register_file *file, uint8_t data, uint8_t carry){
	uint8_t temp_flags = 0b01000000;
	uint8_t result = file->acc - data - carry;
	data = ~data;
	bitwise_arithmetic results = Add8BitsCarry(file->acc, data, (1-carry));
	for (int i=0; i<8; i++){ results.carries[i] = (1-results.carries[i]);}
	uint8_t flags = GetAdditionFlags(results);
	file->flags = temp_flags | flags;
	file->acc = result;

}

// opcode 0b10011xxx
void SubtractCarryRegister(register_file *file, int operand1){	
	uint8_t carry = bit(4, file->flags);
	uint8_t data = *ConvertToRegister(file, operand1);
	DoSubtractCarry(file, data, carry);
}

// opcode 0b10011110
void SubtractHLDirectCarry(register_file *file){
	uint8_t data = ReadMem(ConcatenatePair(file, 2));
	uint8_t carry = bit(4, file->flags);
	DoSubtractCarry(file, data, carry);
}

// opcode 0b11011110
void SubtractImmediateCarry(register_file *file){
	uint8_t data = ReadMem(file->PC++);
	uint8_t carry = bit(4, file->flags);
	DoSubtractCarry(file, data, carry);
}

// opcode 0b10111xxx -- Compares Register with Accumulator via subtraction
void CompareRegister(register_file *file, int operand1){
	uint8_t reg = *ConvertToRegister(file, operand1);
	file->flags = GetSubtractionFlags(file->acc, reg);
}

// opcode 0b10111110
void CompareDirectHL(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t operand2 = ReadMem(address);
	file->flags = GetSubtractionFlags(file->acc, operand2);
}

// opcode 0b11111110
void CompareImmediate(register_file *file){
	static int b = 0;
	uint8_t operand2 = ReadMem(file->PC++);
	file->flags = GetSubtractionFlags(file->acc, operand2);
}

// opcode 0b00xxx100 -- Increments data in selected register
void IncrementRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	uint8_t temp_flags = file->flags & 0b00010000;
	bitwise_arithmetic results = Add8Bits(*reg, 1);
	*reg = results.ans;
	uint8_t new_flags = GetAdditionFlags(results) & 0b11100000;
	file->flags = temp_flags | new_flags;
}

//opcode 0b00110100
void IncrementDirectHL(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	uint8_t temp_flags = file->flags & 0b00010000;
	bitwise_arithmetic results = Add8Bits(data, 1);
	WriteMem(address, results.ans);
	uint8_t new_flags = GetAdditionFlags(results) & 0b11100000;
	file->flags = temp_flags | new_flags;
}

// opcode 0b00xxx101
void DecrementRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	uint8_t temp_flags = file->flags & 0b00010000;
	uint8_t new_flags = GetSubtractionFlags(*reg, 1) & 0b11100000;
	(*reg)--;
	file->flags = temp_flags | new_flags;
}

// opcode 0b00110101
void DecrementDirectHL(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	uint8_t temp_flags = file->flags & 0b00010000;
	uint8_t results = data - 1;
	WriteMem(address, results);
	uint8_t new_flags = GetSubtractionFlags(data, 1) & 0b11100000;
	file->flags = temp_flags | new_flags;
}

// opcode 0b10100xxx -- ANDs register value with on Accumulator
void AndRegister(register_file *file, int operand1){
	uint8_t data = *ConvertToRegister(file, operand1);
	file->acc &= data;
	if (file->acc == 0) { file->flags = 0b10100000;}
	else { file->flags = 0b00100000;}
}

// opcode 0b10100110
void AndDirectHL(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	file->acc &= data;
	if (file->acc == 0) { file->flags = 0b10100000;}
	else { file->flags = 0b00100000;}
}

// opcode 0b11100110
void AndImmediate(register_file *file){
	uint8_t data = ReadMem(file->PC++);
	file->acc &= data;
	if (file->acc == 0) { file->flags = 0b10100000;}
	else { file->flags = 0b00100000;}
}

// opcode 0b10110xxx -- ORs register value with on Accumulator
void OrRegister(register_file *file, int operand1){
	uint8_t data = *ConvertToRegister(file, operand1);
	file->acc |= data;
	if (file->acc == 0) { file->flags = 0b10000000;}
	else { file->flags = 0b00000000;}
}

// opcode 0b10110110
void OrDirectHL(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	file->acc |= data;
	if (file->acc == 0) { file->flags = 0b10000000;}
	else { file->flags = 0b00000000;}
}

// opcode 0b11110110
void OrImmediate(register_file *file){
	uint8_t data = ReadMem(file->PC++);
	file->acc |= data;
	if (file->acc == 0) { file->flags = 0b10000000;}
	else { file->flags = 0b00000000;}
}

void XorData(register_file *file, uint8_t data){
	file->acc ^= data;
	if (file->acc == 0) { file->flags = 0b10000000;}
	else { file->flags = 0b00000000;}
}

// opcode 0b10101xxx -- XORs register value on Accumulator
void XorRegister(register_file *file, int operand1){
	uint8_t data = *ConvertToRegister(file, operand1);
	XorData(file, data);
}

// opcode 0b10101110
void XorDirectHL(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	XorData(file, data);
}

// opcode 0b11101110
void XorImmediate(register_file *file){
	uint8_t data = ReadMem(file->PC++);
	XorData(file, data);
}

// opcode 0b00111111 -- Flips carry flag and clears NH (Z same)
void ComplementCarry(register_file *file){
	uint8_t carry = bit(4, file->flags);
	if (carry == 1){ file->flags &= 0b10000000;}
	else {file->flags &= 0b10000000; file->flags |= 0b00010000;}
}

// opcode 0b00110111 -- Sets carry flag and clears NH
void SetCarry(register_file *file){
	file->flags &= 0b10000000; 
	file->flags |= 0b00010000;
}

// opcode 0b00100111 -- Adjusts Accumulator value to BCD representation
void DecimalAdjustAccumulator(register_file *file){
	uint8_t offset = 0x00;
	uint8_t acc = file->acc;
	uint8_t half_carry = bit(5, file->flags);
	uint8_t carry = bit(4, file->flags);
	uint8_t subtracted = bit(6, file->flags);
	uint8_t carry_flag = 0;

	if (((acc & 0x0F) > 0x09 && subtracted == 0) || half_carry){
		offset |= 0x06;
	}
	if ((acc > 0x99 && subtracted == 0) || carry == 1){
		offset |= 0x60;
		carry_flag = 1 << 4;
	}

	if (subtracted == 1){
		file->acc -= offset;
	} else {
		file->acc += offset;
	}

	
	file->flags &= 0b01000000; file->flags |= carry_flag;
	if (file->acc == 0){ file->flags |= 0b10000000;}
}

// opcode 0b00101111 -- Flips Accumulator and sets NH flags 
void ComplementAccumulator(register_file *file){
	file->acc = ~file->acc;
	file->flags &= 0b10010000;
	file->flags |= 0b01100000;
}

// --------------- 16-bit arithmetic --------------------

// opcode 0b00xx0011
void Increment16BitRegister(register_file *file, int operand1){
	uint16_t data = ConcatenatePair(file, operand1);
	Write16BitReg(file, data+1, operand1);
	M_cycle++;
}

// opcode 0b00xx1011
void Decrement16BitRegister(register_file *file, int operand1){
	uint16_t data = ConcatenatePair(file, operand1);
	Write16BitReg(file, data-1, operand1);
	M_cycle++;
}

// opcode 0b00xx1001 -- Adds to HL the 16 bit register
void AddHLRegister(register_file *file, int operand1){
	uint8_t zero_flag = file->flags & 0b10000000;
	uint16_t data = ConcatenatePair(file, operand1);
	uint16_t HL = ConcatenatePair(file, 2);
	uint8_t H = msb(HL); uint8_t L = lsb(HL);
	uint8_t high = msb(data); uint8_t low = lsb(data);
	bitwise_arithmetic low_result = Add8Bits(low, L);
	file->GPR[5] = low_result.ans;
	uint8_t carry = low_result.carries[7];

	bitwise_arithmetic high_result = Add8BitsCarry(high, H, carry);
	uint8_t temp_flags = GetAdditionFlags(high_result);
	file->GPR[4] = high_result.ans;
	file->flags = zero_flag | (temp_flags & 0b01110000); 
	M_cycle++;
}

// opcode 0b11101000 -- Adds signed 8-bit immediate data to Stack Pointer
void AddToStackPointer(register_file *file){
	uint8_t e = ReadMem(file->PC++);
	uint8_t e_sign = bit(7, e);
	bitwise_arithmetic results = Add8Bits(lsb(file->SP), e);
	uint8_t low = results.ans;
	file->flags = GetAdditionFlags(results) & 0b01110000;
	uint8_t sign_extension;
	if (e_sign == 1){ sign_extension = 0xFF;} else{ sign_extension=0x00;}
	uint8_t high;
	high = msb(file->SP) + sign_extension + bit(4,file->flags);
	file->SP = high << 8 | low;	
	M_cycle += 1;
}

// ---------------- Rotations and Shifts -------------------

// opcode 0b00000111 -- Rotates Accumulator circularly to the left
void CircularLeftAccumulator(register_file *file){
	file->acc = Circular8BitsLeft(file, file->acc);	
}

// opcode 0b00001111
void CircularRightAccumulator(register_file *file){
	file->acc = Circular8BitsRight(file, file->acc);
}


// opcode 0b00010111 -- Rotates using carry bit
void RotateLeftAccumulator(register_file *file){
	file->acc = Rotate8BitsLeft(file, file->acc);
}

// opcode 0b00011111
void RotateRightAccumulator(register_file *file){
	file->acc = Rotate8BitsRight(file, file->acc);
}

// opcode 0b00000xxx (CB)
void CircularLeftRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	*reg = Circular8BitsLeft(file, *reg);
	if (*reg == 0){ file->flags |= 0b10000000;}
}

// opcode 0b00000110 (CB)
void CircularLeftHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	data = Circular8BitsLeft(file, data);
	if (data == 0){ file->flags |= 0b10000000;}
	WriteMem(address, data);
}

// opcode 0b00001xxx (CB)
void CircularRightRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	*reg = Circular8BitsRight(file, *reg);
	if (*reg == 0){ file->flags |= 0b10000000;}
}

// opcode 0b00001110 (CB)
void CircularRightHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	data = Circular8BitsRight(file, data);
	if (data == 0){ file->flags |= 0b10000000;}
	WriteMem(address, data);
}

// opcode 0b00010xxx (CB)
void RotateLeftRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	*reg = Rotate8BitsLeft(file, *reg);
	if (*reg == 0){ file->flags |= 0b10000000;}
}

// opcode 0b00010110 (CB)
void RotateLeftHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	data = Rotate8BitsLeft(file, data);
	if (data == 0){ file->flags |= 0b10000000;}
	WriteMem(address, data);
}


// opcode 0b00011xxx (CB)
void RotateRightRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	*reg = Rotate8BitsRight(file, *reg);
	if (*reg == 0){ file->flags |= 0b10000000;}
}

// opcode 0b00011110 (CB)
void RotateRightHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	data = Rotate8BitsRight(file, data);
	if (data == 0){ file->flags |= 0b10000000;}
	WriteMem(address, data);
}

// opcode 0b00100xxx (CB)
void ArithmeticLeftRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	*reg = Arithmetic8BitsLeft(file, *reg);
	if (*reg == 0){ file->flags |= 0b10000000;}
}

// opcode 0b00100110 (CB)
void ArithmeticLeftHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	data = Arithmetic8BitsLeft(file, data);
	if (data == 0){ file->flags |= 0b10000000;}
	WriteMem(address, data);
}

// opcode 0b00101xxx (CB)
void ArithmeticRightRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	*reg = Arithmetic8BitsRight(file, *reg);
	if (*reg == 0){ file->flags |= 0b10000000;}
}

// opcode 0b00101110 (CB)
void ArithmeticRightHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	data = Arithmetic8BitsRight(file, data);
	if (data == 0){ file->flags |= 0b10000000;}
	WriteMem(address, data);
}

// opcode 0b00110xxx
void SwapNibblesRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	uint8_t high = *reg & 0xF0;
	uint8_t low = *reg & 0x0F;
	*reg = (low << 4) | (high >> 4);
	if (*reg == 0){ file->flags = 0b10000000;}
	else { file->flags = 0x00;}
}

// opcode 0b00110110 (CB)
void SwapNibblesHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	uint8_t high = data & 0xF0;
	uint8_t low = data & 0x0F;
	data = (low << 4) | (high >> 4);
	WriteMem(address, data);
	if (data == 0){ file->flags = 0b10000000;}
	else { file->flags = 0x00;}
}

// opcode 0b00111xxx (CB)
void LogicalRightRegister(register_file *file, int operand1){
	uint8_t *reg = ConvertToRegister(file, operand1);
	*reg = Logical8BitsRight(file, *reg);
}

// opcode 0b00111110 (CB)
void LogicalRightHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	data = Logical8BitsRight(file, data);
	WriteMem(address, data);
}

// opcode 0b01xxxxxx (CB) -- Tests zeroness of bit b of register r
void TestBitRegister(register_file *file, int operand1, int operand2){
	uint8_t *reg = ConvertToRegister(file, operand2);
	uint8_t val = bit(operand1, *reg);
	file->flags &= 0b00010000;
	file->flags |= 0b00100000;
	if (val == 0){ file->flags |= 0b10000000;}
}

// opcode 0b01xxx110 (CB)
void TestBitHLDirect(register_file *file, int operand1){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	uint8_t val = bit(operand1, data);
	file->flags &= 0b00010000;
	file->flags |= 0b00100000;
	if (val == 0){ file->flags |= 0b10000000;}
}


// opcode 0b10xxxxxx (CB) -- Resets bit of reg to 0
void ResetBitRegister(register_file *file, int operand1, int operand2){
	uint8_t *reg = ConvertToRegister(file, operand2);
	uint8_t bitmask = ~(1 << operand1);
	*reg &= bitmask;
}

// opcode 0b10xxx110 (CB)
void ResetBitHLDirect(register_file *file, int operand1){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	uint8_t bitmask = ~(1 << operand1);
	data &= bitmask;
	WriteMem(address, data);
}

// opcode 0b11xxxxxx (CB) -- Sets bit b or reg r to 1
void SetBitRegister(register_file *file, int operand1, int operand2){
	uint8_t *reg = ConvertToRegister(file, operand2);
	uint8_t bitmask = 1 << operand1;
	*reg |= bitmask;
}

// opcode 0b11xxx110 (CB)
void SetBitHLDirect(register_file *file, int operand1){
	uint16_t address = ConcatenatePair(file, 2);
	uint8_t data = ReadMem(address);
	uint8_t bitmask = 1 << operand1;
	data |= bitmask;
	WriteMem(address, data);
}

// ------------------- Control Flow Instructions -------------------

// opcode 0b11000011 -- Jump to address specfied by immediate 16 bit operand
void Jump(register_file *file){
	uint8_t low = ReadMem(file->PC++);
	uint8_t high = ReadMem(file->PC++);
	uint16_t address = low | high << 8;
	file->PC = address;
}

// opcode 0b11101001
void JumpHLDirect(register_file *file){
	uint16_t address = ConcatenatePair(file, 2);
	file->PC = address;
}

// opcode 0b110xx010 -- Jumps to immediate depending on condition cc
void JumpConditional(register_file *file, int operand1){
	uint8_t low = ReadMem(file->PC++);
	uint8_t high = ReadMem(file->PC++);
	uint16_t address = low | high << 8;
	if (ConditionalCheck(file, operand1)) {
		file->PC = address;
		M_cycle++;
	}
}

// opcode 0b00011000 -- Jumps to the relative address specified by operand e
void RelativeJump(register_file *file){
	uint8_t e = ReadMem(file->PC++);
	uint8_t sign = bit(7, e);
	bitwise_arithmetic low = Add8Bits(lsb(file->PC), e);
	uint8_t correction;
	if (low.carries[7] && !sign){ correction = 1;}
	else if (!low.carries[7] && sign){ correction = -1;}
	else{ correction = 0;}
	uint8_t high = msb(file->PC) + correction;
	file->PC = high << 8 | low.ans;
}

// opcode 0b001xx000
void RelativeJumpConditional(register_file *file, int operand1){
	uint8_t e = ReadMem(file->PC++);
	uint8_t sign = bit(7, e);
	bitwise_arithmetic low = Add8Bits(lsb(file->PC), e);
	uint8_t correction;
	if (low.carries[7] && !sign){ correction = 1;}
	else if (!low.carries[7] && sign){ correction = -1;}
	else{ correction = 0;}
	uint8_t high = msb(file->PC) + correction;
	if (ConditionalCheck(file, operand1)) { 
		file->PC = high << 8 | low.ans;
		M_cycle++;
	}
}

// opcode 0b11001101 -- Calls function at 16 bit address
void Call(register_file *file){
	uint8_t low = ReadMem(file->PC++);
	uint8_t high = ReadMem(file->PC++);
	uint16_t address = high << 8 | low;
	WriteMem(--file->SP, msb(file->PC));
	WriteMem(--file->SP, lsb(file->PC));
	file->PC = address;
	M_cycle += 2;
}

// opcode 0b110xx100
void CallConditional(register_file *file, int operand1){
	uint8_t low = ReadMem(file->PC++);
	uint8_t high = ReadMem(file->PC++);
	uint16_t address = high << 8 | low;
	
	if (ConditionalCheck(file, operand1)){
		WriteMem(--file->SP, msb(file->PC));
		WriteMem(--file->SP, lsb(file->PC));
		file->PC = address;
		M_cycle += 1;
	}
}

// opcode 0b11001001 -- Returns from function
void Return(register_file *file){
	uint8_t low = ReadMem(file->SP++);
	uint8_t high = ReadMem(file->SP++);
	uint16_t address = high << 8 | low;
	file->PC = address;	
	M_cycle++;
}

// opcode 0b110xx000
void ReturnConditional(register_file *file, int operand1){
	uint8_t low;
	uint8_t high;
	M_cycle++;	
	if (ConditionalCheck(file, operand1)){
		low = ReadMem(file->SP++);
		high = ReadMem(file->SP++);
		uint16_t address = high << 8 | low;
		file->PC = address;
		M_cycle++;
	}
	
}


// opcode 0b11110011 -- Disables interrupt handing and cancels scheduled EI
void DisableInterrupts(register_file *file){
	file->IE = 0;
}

// opcode 0b111110111 -- Schedules handling to be enabled after next M-cycle
void EnableInterrupts(register_file *file){
	file->IE = 1; // Fetching of next cycle should maybe happen first.
}

// opcode 0b11011001 -- Returns from function and enables interrupts
void ReturnInterrupt(register_file *file){
	Return(file);
	EnableInterrupts(file);
}

// opcode 0b11xxx111 -- Call/Restart function defined by opcode
void Restart(register_file *file, int operand1){
	WriteMem(--file->SP, msb(file->PC));
	WriteMem(--file->SP, lsb(file->PC));
	uint16_t new_address = operand1 * 8;	
	file->PC = new_address;
	M_cycle++;
}

// ----------------- Miscellaneous Instructions -----------------------

void Halt(register_file *file){
	file->halted = 1;
}

void Stop(register_file *file){
	//file->halted = 1;
	file->PC++;
	//TODO
}

// opcode 0b00000000 -- Does nothing but adds M delay and increments PC
void NoOperation(register_file *file){
	return;
}

// opcode 0xCB -- Treats next opcode differently
void ExtendedInstructionSet(register_file *file){
	uint8_t opcode = ReadMem(file->PC++);
	switch (opcode){
		case 0x00:
			CircularLeftRegister(file, 0);
			break;
		case 0x01:
			CircularLeftRegister(file, 1);
			break;
		case 0x02:
			CircularLeftRegister(file, 2);
			break;
		case 0x03:
			CircularLeftRegister(file, 3);
			break;
		case 0x04:
			CircularLeftRegister(file, 4);
			break;
		case 0x05:
			CircularLeftRegister(file, 5);
			break;
		case 0x06:
			CircularLeftHLDirect(file);
			break;
		case 0x07:
			CircularLeftRegister(file, 7);
			break;
		case 0x08:
			CircularRightRegister(file, 0);
			break;
		case 0x09:
			CircularRightRegister(file, 1);
			break;
		case 0x0A:
			CircularRightRegister(file, 2);
			break;
		case 0x0B:
			CircularRightRegister(file, 3);
			break;
		case 0x0C:
			CircularRightRegister(file, 4);
			break;
		case 0x0D:
			CircularRightRegister(file, 5);
			break;
		case 0x0E:
			CircularRightHLDirect(file);
			break;
		case 0x0F:
			CircularRightRegister(file, 7);
			break;
		case 0x10:
			RotateLeftRegister(file, 0);
			break;
		case 0x11:
			RotateLeftRegister(file, 1);
			break;
		case 0x12:
			RotateLeftRegister(file, 2);
			break;
		case 0x13:
			RotateLeftRegister(file, 3);
			break;
		case 0x14:
			RotateLeftRegister(file, 4);
			break;
		case 0x15:
			RotateLeftRegister(file, 5);
			break;
		case 0x16:
			RotateLeftHLDirect(file);
			break;
		case 0x17:
			RotateLeftRegister(file, 7);
			break;
		case 0x18:
			RotateRightRegister(file, 0);
			break;
		case 0x19:
			RotateRightRegister(file, 1);
			break;
		case 0x1A:
			RotateRightRegister(file, 2);
			break;
		case 0x1B:
			RotateRightRegister(file, 3);
			break;
		case 0x1C:
			RotateRightRegister(file, 4);
			break;
		case 0x1D:
			RotateRightRegister(file, 5);
			break;
		case 0x1E:
			RotateRightHLDirect(file);
			break;
		case 0x1F:
			RotateRightRegister(file, 7);
			break;
		case 0x20:
			ArithmeticLeftRegister(file, 0);
			break;
		case 0x21:
			ArithmeticLeftRegister(file, 1);
			break;
		case 0x22:
			ArithmeticLeftRegister(file, 2);
			break;
		case 0x23:
			ArithmeticLeftRegister(file, 3);
			break;
		case 0x24:
			ArithmeticLeftRegister(file, 4);
			break;
		case 0x25:
			ArithmeticLeftRegister(file, 5);
			break;
		case 0x26:
			ArithmeticLeftHLDirect(file);
			break;
		case 0x27:
			ArithmeticLeftRegister(file, 7);
			break;
		case 0x28:
			ArithmeticRightRegister(file, 0);
			break;
		case 0x29:
			ArithmeticRightRegister(file, 1);
			break;
		case 0x2A:
			ArithmeticRightRegister(file, 2);
			break;
		case 0x2B:
			ArithmeticRightRegister(file, 3);
			break;
		case 0x2C:
			ArithmeticRightRegister(file, 4);
			break;
		case 0x2D:
			ArithmeticRightRegister(file, 5);
			break;
		case 0x2E:
			ArithmeticRightHLDirect(file);
			break;
		case 0x2F:
			ArithmeticRightRegister(file, 7);
			break;
		case 0x30:
			SwapNibblesRegister(file, 0);
			break;
		case 0x31:
			SwapNibblesRegister(file, 1);
			break;
		case 0x32:
			SwapNibblesRegister(file, 2);
			break;
		case 0x33:
			SwapNibblesRegister(file, 3);
			break;
		case 0x34:
			SwapNibblesRegister(file, 4);
			break;
		case 0x35:
			SwapNibblesRegister(file, 5);
			break;
		case 0x36:
			SwapNibblesHLDirect(file);
			break;
		case 0x37:
			SwapNibblesRegister(file, 7);
			break;
		case 0x38:
			LogicalRightRegister(file, 0);
			break;
		case 0x39:
			LogicalRightRegister(file, 1);
			break;
		case 0x3A:
			LogicalRightRegister(file, 2);
			break;
		case 0x3B:
			LogicalRightRegister(file, 3);
			break;
		case 0x3C:
			LogicalRightRegister(file, 4);
			break;
		case 0x3D:
			LogicalRightRegister(file, 5);
			break;
		case 0x3E:
			LogicalRightHLDirect(file);
			break;
		case 0x3F:
			LogicalRightRegister(file, 7);
			break;
		case 0x40:
			TestBitRegister(file, 0, 0);
			break;
		case 0x41:
			TestBitRegister(file, 0, 1);
			break;
		case 0x42:
			TestBitRegister(file, 0, 2);
			break;
		case 0x43:
			TestBitRegister(file, 0, 3);
			break;
		case 0x44:
			TestBitRegister(file, 0, 4);
			break;
		case 0x45:
			TestBitRegister(file, 0, 5);
			break;
		case 0x46:
			TestBitHLDirect(file, 0);
			break;
		case 0x47:
			TestBitRegister(file, 0, 7);
			break;
		case 0x48:
			TestBitRegister(file, 1, 0);
			break;
		case 0x49:
			TestBitRegister(file, 1, 1);
			break;
		case 0x4A:
			TestBitRegister(file, 1, 2);
			break;
		case 0x4B:
			TestBitRegister(file, 1, 3);
			break;
		case 0x4C:
			TestBitRegister(file, 1, 4);
			break;
		case 0x4D:
			TestBitRegister(file, 1, 5);
			break;
		case 0x4E:
			TestBitHLDirect(file, 1);
			break;
		case 0x4F:
			TestBitRegister(file, 1, 7);
			break;
		case 0x50:
			TestBitRegister(file, 2, 0);
			break;
		case 0x51:
			TestBitRegister(file, 2, 1);
			break;
		case 0x52:
			TestBitRegister(file, 2, 2);
			break;
		case 0x53:
			TestBitRegister(file, 2, 3);
			break;
		case 0x54:
			TestBitRegister(file, 2, 4);
			break;
		case 0x55:
			TestBitRegister(file, 2, 5);
			break;
		case 0x56:
			TestBitHLDirect(file, 2);
			break;
		case 0x57:
			TestBitRegister(file, 2, 7);
			break;
		case 0x58:
			TestBitRegister(file, 3, 0);
			break;
		case 0x59:
			TestBitRegister(file, 3, 1);
			break;
		case 0x5A:
			TestBitRegister(file, 3, 2);
			break;
		case 0x5B:
			TestBitRegister(file, 3, 3);
			break;
		case 0x5C:
			TestBitRegister(file, 3, 4);
			break;
		case 0x5D:
			TestBitRegister(file, 3, 5);
			break;
		case 0x5E:
			TestBitHLDirect(file, 3);
			break;
		case 0x5F:
			TestBitRegister(file, 3, 7);
			break;
		case 0x60:
			TestBitRegister(file, 4, 0);
			break;
		case 0x61:
			TestBitRegister(file, 4, 1);
			break;
		case 0x62:
			TestBitRegister(file, 4, 2);
			break;
		case 0x63:
			TestBitRegister(file, 4, 3);
			break;
		case 0x64:
			TestBitRegister(file, 4, 4);
			break;
		case 0x65:
			TestBitRegister(file, 4, 5);
			break;
		case 0x66:
			TestBitHLDirect(file, 4);
			break;
		case 0x67:
			TestBitRegister(file, 4, 7);
			break;
		case 0x68:
			TestBitRegister(file, 5, 0);
			break;
		case 0x69:
			TestBitRegister(file, 5, 1);
			break;
		case 0x6A:
			TestBitRegister(file, 5, 2);
			break;
		case 0x6B:
			TestBitRegister(file, 5, 3);
			break;
		case 0x6C:
			TestBitRegister(file, 5, 4);
			break;
		case 0x6D:
			TestBitRegister(file, 5, 5);
			break;
		case 0x6E:
			TestBitHLDirect(file, 5);
			break;
		case 0x6F:
			TestBitRegister(file, 5, 7);
			break;
		case 0x70:
			TestBitRegister(file, 6, 0);
			break;
		case 0x71:
			TestBitRegister(file, 6, 1);
			break;
		case 0x72:
			TestBitRegister(file, 6, 2);
			break;
		case 0x73:
			TestBitRegister(file, 6, 3);
			break;
		case 0x74:
			TestBitRegister(file, 6, 4);
			break;
		case 0x75:
			TestBitRegister(file, 6, 5);
			break;
		case 0x76:
			TestBitHLDirect(file, 6);
			break;
		case 0x77:
			TestBitRegister(file, 6, 7);
			break;
		case 0x78:
			TestBitRegister(file, 7, 0);
			break;
		case 0x79:
			TestBitRegister(file, 7, 1);
			break;
		case 0x7A:
			TestBitRegister(file, 7, 2);
			break;
		case 0x7B:
			TestBitRegister(file, 7, 3);
			break;
		case 0x7C:
			TestBitRegister(file, 7, 4);
			break;
		case 0x7D:
			TestBitRegister(file, 7, 5);
			break;
		case 0x7E:
			TestBitHLDirect(file, 7);
			break;
		case 0x7F:
			TestBitRegister(file, 7, 7);
			break;
		case 0x80:
			ResetBitRegister(file, 0, 0);
			break;
		case 0x81:
			ResetBitRegister(file, 0, 1);
			break;
		case 0x82:
			ResetBitRegister(file, 0, 2);
			break;
		case 0x83:
			ResetBitRegister(file, 0, 3);
			break;
		case 0x84:
			ResetBitRegister(file, 0, 4);
			break;
		case 0x85:
			ResetBitRegister(file, 0, 5);
			break;
		case 0x86:
			ResetBitHLDirect(file, 0);
			break;
		case 0x87:
			ResetBitRegister(file, 0, 7);
			break;
		case 0x88:
			ResetBitRegister(file, 1, 0);
			break;
		case 0x89:
			ResetBitRegister(file, 1, 1);
			break;
		case 0x8A:
			ResetBitRegister(file, 1, 2);
			break;
		case 0x8B:
			ResetBitRegister(file, 1, 3);
			break;
		case 0x8C:
			ResetBitRegister(file, 1, 4);
			break;
		case 0x8D:
			ResetBitRegister(file, 1, 5);
			break;
		case 0x8E:
			ResetBitHLDirect(file, 1);
			break;
		case 0x8F:
			ResetBitRegister(file, 1, 7);
			break;
		case 0x90:
			ResetBitRegister(file, 2, 0);
			break;
		case 0x91:
			ResetBitRegister(file, 2, 1);
			break;
		case 0x92:
			ResetBitRegister(file, 2, 2);
			break;
		case 0x93:
			ResetBitRegister(file, 2, 3);
			break;
		case 0x94:
			ResetBitRegister(file, 2, 4);
			break;
		case 0x95:
			ResetBitRegister(file, 2, 5);
			break;
		case 0x96:
			ResetBitHLDirect(file, 2);
			break;
		case 0x97:
			ResetBitRegister(file, 2, 7);
			break;
		case 0x98:
			ResetBitRegister(file, 3, 0);
			break;
		case 0x99:
			ResetBitRegister(file, 3, 1);
			break;
		case 0x9A:
			ResetBitRegister(file, 3, 2);
			break;
		case 0x9B:
			ResetBitRegister(file, 3, 3);
			break;
		case 0x9C:
			ResetBitRegister(file, 3, 4);
			break;
		case 0x9D:
			ResetBitRegister(file, 3, 5);
			break;
		case 0x9E:
			ResetBitHLDirect(file, 3);
			break;
		case 0x9F:
			ResetBitRegister(file, 3, 7);
			break;
		case 0xA0:
			ResetBitRegister(file, 4, 0);
			break;
		case 0xA1:
			ResetBitRegister(file, 4, 1);
			break;
		case 0xA2:
			ResetBitRegister(file, 4, 2);
			break;
		case 0xA3:
			ResetBitRegister(file, 4, 3);
			break;
		case 0xA4:
			ResetBitRegister(file, 4, 4);
			break;
		case 0xA5:
			ResetBitRegister(file, 4, 5);
			break;
		case 0xA6:
			ResetBitHLDirect(file, 4);
			break;
		case 0xA7:
			ResetBitRegister(file, 4, 7);
			break;
		case 0xA8:
			ResetBitRegister(file, 5, 0);
			break;
		case 0xA9:
			ResetBitRegister(file, 5, 1);
			break;
		case 0xAA:
			ResetBitRegister(file, 5, 2);
			break;
		case 0xAB:
			ResetBitRegister(file, 5, 3);
			break;
		case 0xAC:
			ResetBitRegister(file, 5, 4);
			break;
		case 0xAD:
			ResetBitRegister(file, 5, 5);
			break;
		case 0xAE:
			ResetBitHLDirect(file, 5);
			break;
		case 0xAF:
			ResetBitRegister(file, 5, 7);
			break;
		case 0xB0:
			ResetBitRegister(file, 6, 0);
			break;
		case 0xB1:
			ResetBitRegister(file, 6, 1);
			break;
		case 0xB2:
			ResetBitRegister(file, 6, 2);
			break;
		case 0xB3:
			ResetBitRegister(file, 6, 3);
			break;
		case 0xB4:
			ResetBitRegister(file, 6, 4);
			break;
		case 0xB5:
			ResetBitRegister(file, 6, 5);
			break;
		case 0xB6:
			ResetBitHLDirect(file, 6);
			break;
		case 0xB7:
			ResetBitRegister(file, 6, 7);
			break;
		case 0xB8:
			ResetBitRegister(file, 7, 0);
			break;
		case 0xB9:
			ResetBitRegister(file, 7, 1);
			break;
		case 0xBA:
			ResetBitRegister(file, 7, 2);
			break;
		case 0xBB:
			ResetBitRegister(file, 7, 3);
			break;
		case 0xBC:
			ResetBitRegister(file, 7, 4);
			break;
		case 0xBD:
			ResetBitRegister(file, 7, 5);
			break;
		case 0xBE:
			ResetBitHLDirect(file, 7);
			break;
		case 0xBF:
			ResetBitRegister(file, 7, 7);
			break;
		case 0xC0:
			SetBitRegister(file, 0, 0);
			break;
		case 0xC1:
			SetBitRegister(file, 0, 1);
			break;
		case 0xC2:
			SetBitRegister(file, 0, 2);
			break;
		case 0xC3:
			SetBitRegister(file, 0, 3);
			break;
		case 0xC4:
			SetBitRegister(file, 0, 4);
			break;
		case 0xC5:
			SetBitRegister(file, 0, 5);
			break;
		case 0xC6:
			SetBitHLDirect(file, 0);
			break;
		case 0xC7:
			SetBitRegister(file, 0, 7);
			break;
		case 0xC8:
			SetBitRegister(file, 1, 0);
			break;
		case 0xC9:
			SetBitRegister(file, 1, 1);
			break;
		case 0xCA:
			SetBitRegister(file, 1, 2);
			break;
		case 0xCB:
			SetBitRegister(file, 1, 3);
			break;
		case 0xCC:
			SetBitRegister(file, 1, 4);
			break;
		case 0xCD:
			SetBitRegister(file, 1, 5);
			break;
		case 0xCE:
			SetBitHLDirect(file, 1);
			break;
		case 0xCF:
			SetBitRegister(file, 1, 7);
			break;
		case 0xD0:
			SetBitRegister(file, 2, 0);
			break;
		case 0xD1:
			SetBitRegister(file, 2, 1);
			break;
		case 0xD2:
			SetBitRegister(file, 2, 2);
			break;
		case 0xD3:
			SetBitRegister(file, 2, 3);
			break;
		case 0xD4:
			SetBitRegister(file, 2, 4);
			break;
		case 0xD5:
			SetBitRegister(file, 2, 5);
			break;
		case 0xD6:
			SetBitHLDirect(file, 2);
			break;
		case 0xD7:
			SetBitRegister(file, 2, 7);
			break;
		case 0xD8:
			SetBitRegister(file, 3, 0);
			break;
		case 0xD9:
			SetBitRegister(file, 3, 1);
			break;
		case 0xDA:
			SetBitRegister(file, 3, 2);
			break;
		case 0xDB:
			SetBitRegister(file, 3, 3);
			break;
		case 0xDC:
			SetBitRegister(file, 3, 4);
			break;
		case 0xDD:
			SetBitRegister(file, 3, 5);
			break;
		case 0xDE:
			SetBitHLDirect(file, 3);
			break;
		case 0xDF:
			SetBitRegister(file, 3, 7);
			break;
		case 0xE0:
			SetBitRegister(file, 4, 0);
			break;
		case 0xE1:
			SetBitRegister(file, 4, 1);
			break;
		case 0xE2:
			SetBitRegister(file, 4, 2);
			break;
		case 0xE3:
			SetBitRegister(file, 4, 3);
			break;
		case 0xE4:
			SetBitRegister(file, 4, 4);
			break;
		case 0xE5:
			SetBitRegister(file, 4, 5);
			break;
		case 0xE6:
			SetBitHLDirect(file, 4);
			break;
		case 0xE7:
			SetBitRegister(file, 4, 7);
			break;
		case 0xE8:
			SetBitRegister(file, 5, 0);
			break;
		case 0xE9:
			SetBitRegister(file, 5, 1);
			break;
		case 0xEA:
			SetBitRegister(file, 5, 2);
			break;
		case 0xEB:
			SetBitRegister(file, 5, 3);
			break;
		case 0xEC:
			SetBitRegister(file, 5, 4);
			break;
		case 0xED:
			SetBitRegister(file, 5, 5);
			break;
		case 0xEE:
			SetBitHLDirect(file, 5);
			break;
		case 0xEF:
			SetBitRegister(file, 5, 7);
			break;
		case 0xF0:
			SetBitRegister(file, 6, 0);
			break;
		case 0xF1:
			SetBitRegister(file, 6, 1);
			break;
		case 0xF2:
			SetBitRegister(file, 6, 2);
			break;
		case 0xF3:
			SetBitRegister(file, 6, 3);
			break;
		case 0xF4:
			SetBitRegister(file, 6, 4);
			break;
		case 0xF5:
			SetBitRegister(file, 6, 5);
			break;
		case 0xF6:
			SetBitHLDirect(file, 6);
			break;
		case 0xF7:
			SetBitRegister(file, 6, 7);
			break;
		case 0xF8:
			SetBitRegister(file, 7, 0);
			break;
		case 0xF9:
			SetBitRegister(file, 7, 1);
			break;
		case 0xFA:
			SetBitRegister(file, 7, 2);
			break;
		case 0xFB:
			SetBitRegister(file, 7, 3);
			break;
		case 0xFC:
			SetBitRegister(file, 7, 4);
			break;
		case 0xFD:
			SetBitRegister(file, 7, 5);
			break;
		case 0xFE:
			SetBitHLDirect(file, 7);
			break;
		case 0xFF:
			SetBitRegister(file, 7, 7);
			break;
		default:
			break;
	}
	// TODO Can definitely do this more programatically
}

void DecodeInstruction(register_file *file, uint8_t data){
	switch (data){
		case 0x00:
			NoOperation(file);
			break;
		case 0x01:
			Load16BitImmediate(file, 0);
			break;
		case 0x02:
			LoadAccumulatorToBC(file);
			break;
		case 0x03:
			Increment16BitRegister(file, 0);
			break;
		case 0x04:
			IncrementRegister(file, 0);
			break;
		case 0x05:
			DecrementRegister(file, 0);
			break;
		case 0x06:
			Load8BitImmediate(file, 0);
			break;
		case 0x07:
			CircularLeftAccumulator(file);
			break;
		case 0x08:
			Load16FromSPDirect(file);
			break;
		case 0x09:
			AddHLRegister(file, 0);
			break;
		case 0x0A:
			LoadAccumulatorFromBC(file);
			break;
		case 0x0B:
			Decrement16BitRegister(file, 0);
			break;
		case 0x0C:
			IncrementRegister(file, 1);
			break;
		case 0x0D:
			DecrementRegister(file, 1);
			break;
		case 0x0E:
			Load8BitImmediate(file, 1);
			break;
		case 0x0F:
			CircularRightAccumulator(file);
			break;
		case 0x10:
			Stop(file);
			break;
		case 0x11:
			Load16BitImmediate(file, 1);
			break;
		case 0x12:
			LoadAccumulatorToDE(file);
			break;
		case 0x13:
			Increment16BitRegister(file, 1);
			break;
		case 0x14:
			IncrementRegister(file, 2);
			break;
		case 0x15:
			DecrementRegister(file, 2);
			break;
		case 0x16:
			Load8BitImmediate(file, 2);
			break;
		case 0x17:
			RotateLeftAccumulator(file);
			break;
		case 0x18:
			RelativeJump(file);
			break;
		case 0x19:
			AddHLRegister(file, 1);
			break;
		case 0x1A:
			LoadAccumulatorFromDE(file);
			break;
		case 0x1B:
			Decrement16BitRegister(file, 1);
			break;
		case 0x1C:
			IncrementRegister(file, 3);
			break;
		case 0x1D:
			DecrementRegister(file, 3);
			break;
		case 0x1E:
			Load8BitImmediate(file, 3);
			break;
		case 0x1F:
			RotateRightAccumulator(file);
			break;
		case 0x20:
			RelativeJumpConditional(file, 0);
			break;
		case 0x21:
			Load16BitImmediate(file, 2);
			break;
		case 0x22:
			LoadFromAccumulatorDirectHLIncrement(file);
			break;
		case 0x23:
			Increment16BitRegister(file, 2);
			break;
		case 0x24:
			IncrementRegister(file, 4);
			break;
		case 0x25:
			DecrementRegister(file, 4);
			break;
		case 0x26:
			Load8BitImmediate(file, 4);
			break;
		case 0x27:
			DecimalAdjustAccumulator(file);
			break;
		case 0x28:
			RelativeJumpConditional(file, 1);
			break;
		case 0x29:
			AddHLRegister(file, 2);
			break;
		case 0x2A:
			LoadAccumulatorDirectHLIncrement(file);
			break;
		case 0x2B:
			Decrement16BitRegister(file, 2);
			break;
		case 0x2C:
			IncrementRegister(file, 5);
			break;
		case 0x2D:
			DecrementRegister(file, 5);
			break;
		case 0x2E:
			Load8BitImmediate(file, 5);
			break;
		case 0x2F:
			ComplementAccumulator(file);
			break;
		case 0x30:
			RelativeJumpConditional(file, 2);
			break;
		case 0x31:
			Load16BitImmediate(file, 3);
			break;
		case 0x32:
			LoadFromAccumulatorDirectHLDecrement(file);
			break;
		case 0x33:
			Increment16BitRegister(file, 3);
			break;
		case 0x34:
			IncrementDirectHL(file);
			break;
		case 0x35:
			DecrementDirectHL(file);
			break;
		case 0x36:
			Load8bitImmediateHLDirect(file);
			break;
		case 0x37:
			SetCarry(file);
			break;
		case 0x38:
			RelativeJumpConditional(file, 3);
			break;
		case 0x39:
			AddHLRegister(file, 3);
			break;
		case 0x3A:
			LoadAccumulatorDirectHLDecrement(file);
			break;
		case 0x3B:
			Decrement16BitRegister(file, 3);
			break;
		case 0x3C:
			IncrementRegister(file, 7);
			break;
		case 0x3D:
			DecrementRegister(file, 7);
			break;
		case 0x3E:
			Load8BitImmediate(file, 7);
			break;
		case 0x3F:
			ComplementCarry(file);
			break;
		case 0x40:
			Load8BitRegister(file, 0, 0);
			break;
		case 0x41:
			Load8BitRegister(file, 0, 1);
			break;
		case 0x42:
			Load8BitRegister(file, 0, 2);
			break;
		case 0x43:
			Load8BitRegister(file, 0, 3);
			break;
		case 0x44:
			Load8BitRegister(file, 0, 4);
			break;
		case 0x45:
			Load8BitRegister(file, 0, 5);
			break;
		case 0x46:
			Load8BitIndirectHL(file, 0);
			break;
		case 0x47:
			Load8BitRegister(file, 0, 7);
			break;
		case 0x48:
			Load8BitRegister(file, 1, 0);
			break;
		case 0x49:
			Load8BitRegister(file, 1, 1);
			break;
		case 0x4A:
			Load8BitRegister(file, 1, 2);
			break;
		case 0x4B:
			Load8BitRegister(file, 1, 3);
			break;
		case 0x4C:
			Load8BitRegister(file, 1, 4);
			break;
		case 0x4D:
			Load8BitRegister(file, 1, 5);
			break;
		case 0x4E:
			Load8BitIndirectHL(file, 1);
			break;
		case 0x4F:
			Load8BitRegister(file, 1, 7);
			break;
		case 0x50:
			Load8BitRegister(file, 2, 0);
			break;
		case 0x51:
			Load8BitRegister(file, 2, 1);
			break;
		case 0x52:
			Load8BitRegister(file, 2, 2);
			break;
		case 0x53:
			Load8BitRegister(file, 2, 3);
			break;
		case 0x54:
			Load8BitRegister(file, 2, 4);
			break;
		case 0x55:
			Load8BitRegister(file, 2, 5);
			break;
		case 0x56:
			Load8BitIndirectHL(file, 2);
			break;
		case 0x57:
			Load8BitRegister(file, 2, 7);
			break;
		case 0x58:
			Load8BitRegister(file, 3, 0);
			break;
		case 0x59:
			Load8BitRegister(file, 3, 1);
			break;
		case 0x5A:
			Load8BitRegister(file, 3, 2);
			break;
		case 0x5B:
			Load8BitRegister(file, 3, 3);
			break;
		case 0x5C:
			Load8BitRegister(file, 3, 4);
			break;
		case 0x5D:
			Load8BitRegister(file, 3, 5);
			break;
		case 0x5E:
			Load8BitIndirectHL(file, 3);
			break;
		case 0x5F:
			Load8BitRegister(file, 3, 7);
			break;
		case 0x60:
			Load8BitRegister(file, 4, 0);
			break;
		case 0x61:
			Load8BitRegister(file, 4, 1);
			break;
		case 0x62:
			Load8BitRegister(file, 4, 2);
			break;
		case 0x63:
			Load8BitRegister(file, 4, 3);
			break;
		case 0x64:
			Load8BitRegister(file, 4, 4);
			break;
		case 0x65:
			Load8BitRegister(file, 4, 5);
			break;
		case 0x66:
			Load8BitIndirectHL(file, 4);
			break;
		case 0x67:
			Load8BitRegister(file, 4, 7);
			break;
		case 0x68:
			Load8BitRegister(file, 5, 0);
			break;
		case 0x69:
			Load8BitRegister(file, 5, 1);
			break;
		case 0x6A:
			Load8BitRegister(file, 5, 2);
			break;
		case 0x6B:
			Load8BitRegister(file, 5, 3);
			break;
		case 0x6C:
			Load8BitRegister(file, 5, 4);
			break;
		case 0x6D:
			Load8BitRegister(file, 5, 5);
			break;
		case 0x6E:
			Load8BitIndirectHL(file, 5);
			break;
		case 0x6F:
			Load8BitRegister(file, 5, 7);
			break;
		case 0x70:
			Load8BitToHL(file, 0);
			break;
		case 0x71:
			Load8BitToHL(file, 1);
			break;
		case 0x72:
			Load8BitToHL(file, 2);
			break;
		case 0x73:
			Load8BitToHL(file, 3);
			break;
		case 0x74:
			Load8BitToHL(file, 4);
			break;
		case 0x75:
			Load8BitToHL(file, 5);
			break;
		case 0x76:
			Halt(file);
			break;
		case 0x77:
			Load8BitToHL(file, 7);
			break;
		case 0x78:
			Load8BitRegister(file, 7, 0);
			break;
		case 0x79:
			Load8BitRegister(file, 7, 1);
			break;
		case 0x7A:
			Load8BitRegister(file, 7, 2);
			break;
		case 0x7B:
			Load8BitRegister(file, 7, 3);
			break;
		case 0x7C:
			Load8BitRegister(file, 7, 4);
			break;
		case 0x7D:
			Load8BitRegister(file, 7, 5);
			break;
		case 0x7E:
			Load8BitIndirectHL(file, 7);
			break;
		case 0x7F:
			Load8BitRegister(file, 7, 7);
			break;
		case 0x80:
			AddRegister(file, 0);
			break;
		case 0x81:
			AddRegister(file, 1);
			break;
		case 0x82:
			AddRegister(file, 2);
			break;
		case 0x83:
			AddRegister(file, 3);
			break;
		case 0x84:
			AddRegister(file, 4);
			break;
		case 0x85:
			AddRegister(file, 5);
			break;
		case 0x86:
			AddHLDirect(file);
			break;
		case 0x87:
			AddRegister(file, 7);
			break;
		case 0x88:
			AddRegisterCarry(file, 0);
			break;
		case 0x89:
			AddRegisterCarry(file, 1);
			break;
		case 0x8A:
			AddRegisterCarry(file, 2);
			break;
		case 0x8B:
			AddRegisterCarry(file, 3);
			break;
		case 0x8C:
			AddRegisterCarry(file, 4);
			break;
		case 0x8D:
			AddRegisterCarry(file, 5);
			break;
		case 0x8E:
			AddHLDirectCarry(file);
			break;
		case 0x8F:
			AddRegisterCarry(file, 7);
			break;
		case 0x90:
			SubtractRegister(file, 0);
			break;
		case 0x91:
			SubtractRegister(file, 1);
			break;
		case 0x92:
			SubtractRegister(file, 2);
			break;
		case 0x93:
			SubtractRegister(file, 3);
			break;
		case 0x94:
			SubtractRegister(file, 4);
			break;
		case 0x95:
			SubtractRegister(file, 5);
			break;
		case 0x96:
			SubtractHLDirect(file);
			break;
		case 0x97:
			SubtractRegister(file, 7);
			break;
		case 0x98:
			SubtractCarryRegister(file, 0);
			break;
		case 0x99:
			SubtractCarryRegister(file, 1);
			break;
		case 0x9A:
			SubtractCarryRegister(file, 2);
			break;
		case 0x9B:
			SubtractCarryRegister(file, 3);
			break;
		case 0x9C:
			SubtractCarryRegister(file, 4);
			break;
		case 0x9D:
			SubtractCarryRegister(file, 5);
			break;
		case 0x9E:
			SubtractHLDirectCarry(file);
			break;
		case 0x9F:
			SubtractCarryRegister(file, 7);
			break;
		case 0xA0:
			AndRegister(file, 0);
			break;
		case 0xA1:
			AndRegister(file, 1);
			break;
		case 0xA2:
			AndRegister(file, 2);
			break;
		case 0xA3:
			AndRegister(file, 3);
			break;
		case 0xA4:
			AndRegister(file, 4);
			break;
		case 0xA5:
			AndRegister(file, 5);
			break;
		case 0xA6:
			AndDirectHL(file);
			break;
		case 0xA7:
			AndRegister(file, 7);
			break;
		case 0xA8:
			XorRegister(file, 0);
			break;
		case 0xA9:
			XorRegister(file, 1);
			break;
		case 0xAA:
			XorRegister(file, 2);
			break;
		case 0xAB:
			XorRegister(file, 3);
			break;
		case 0xAC:
			XorRegister(file, 4);
			break;
		case 0xAD:
			XorRegister(file, 5);
			break;
		case 0xAE:
			XorDirectHL(file);
			break;
		case 0xAF:
			XorRegister(file, 7);
			break;
		case 0xB0:
			OrRegister(file, 0);
			break;
		case 0xB1:
			OrRegister(file, 1);
			break;
		case 0xB2:
			OrRegister(file, 2);
			break;
		case 0xB3:
			OrRegister(file, 3);
			break;
		case 0xB4:
			OrRegister(file, 4);
			break;
		case 0xB5:
			OrRegister(file, 5);
			break;
		case 0xB6:
			OrDirectHL(file);
			break;
		case 0xB7:
			OrRegister(file, 7);
			break;
		case 0xB8:
			CompareRegister(file, 0);
			break;
		case 0xB9:
			CompareRegister(file, 1);
			break;
		case 0xBA:
			CompareRegister(file, 2);
			break;
		case 0xBB:
			CompareRegister(file, 3);
			break;
		case 0xBC:
			CompareRegister(file, 4);
			break;
		case 0xBD:
			CompareRegister(file, 5);
			break;
		case 0xBE:
			CompareDirectHL(file);
			break;
		case 0xBF:
			CompareRegister(file, 7);
			break;
		case 0xC0:
			ReturnConditional(file, 0);
			break;
		case 0xC1:
			PopTo16Bit(file, 0);
			break;
		case 0xC2:
			JumpConditional(file, 0);
			break;
		case 0xC3:
			Jump(file);
			break;
		case 0xC4:
			CallConditional(file, 0);
			break;
		case 0xC5:
			Push16BitToStack(file, 0);
			break;
		case 0xC6:
			AddImmediate(file);
			break;
		case 0xC7:
			Restart(file, 0);	
			break;
		case 0xC8:
			ReturnConditional(file, 1);
			break;
		case 0xC9:
			Return(file);
			break;
		case 0xCA:
			JumpConditional(file, 1);
			break;
		case 0xCB:
			ExtendedInstructionSet(file);
			break;
		case 0xCC:
			CallConditional(file, 1);
			break;
		case 0xCD:
			Call(file);
			break;
		case 0xCE:
			AddImmediateCarry(file);
			break;
		case 0xCF:
			Restart(file, 1);
			break;
		case 0xD0:
			ReturnConditional(file, 2);
			break;
		case 0xD1:
			PopTo16Bit(file, 1);
			break;
		case 0xD2:
			JumpConditional(file, 2);
			break;
		case 0xD3:
			// NOTHING
			break;
		case 0xD4:
			CallConditional(file, 2);
			break;
		case 0xD5:
			Push16BitToStack(file, 1);
			break;
		case 0xD6:
			SubtractImmediate(file);
			break;
		case 0xD7:
			Restart(file, 2);
			break;
		case 0xD8:
			ReturnConditional(file, 3);
			break;
		case 0xD9:
			ReturnInterrupt(file);
			break;
		case 0xDA:
			JumpConditional(file, 3);
			break;
		case 0xDB:
			// NOTHING
			break;
		case 0xDC:
			CallConditional(file, 3);
			break;
		case 0xDD:
			// NOTHING
			break;
		case 0xDE:
			SubtractImmediateCarry(file);
			break;
		case 0xDF:
			Restart(file, 3);
			break;
		case 0xE0:
			LoadFromAccumulatorHighDirect(file);
			break;
		case 0xE1:
			PopTo16Bit(file, 2);
			break;
		case 0xE2:
			LoadFromAccumulatorHighC(file);
			break;
		case 0xE3:
			// NOTHING
			break;
		case 0xE4:
			// NOTHING
			break;
		case 0xE5:
			Push16BitToStack(file, 2);
			break;
		case 0xE6:
			AndImmediate(file);
			break;
		case 0xE7:
			Restart(file, 4);
			break;
		case 0xE8:
			AddToStackPointer(file);
			break;
		case 0xE9:
			JumpHLDirect(file);
			break;
		case 0xEA:
			LoadAccumulatorToDirect(file);			
			break;
		case 0xEB:
			// Nothing
			break;
		case 0xEC:
			// Nothing
			break;
		case 0xED:
			// Nothing
			break;
		case 0xEE:
			XorImmediate(file);
			break;
		case 0xEF:
			Restart(file, 5);
			break;
		case 0xF0:
			LoadAccumulatorHighDirect(file);
			break;
		case 0xF1:
			PopTo16Bit(file, 3);
			break;
		case 0xF2:
			LoadAccumulatorHighC(file);
			break;
		case 0xF3:
			DisableInterrupts(file);
			break;
		case 0xF4:
			// Nothing
			break;
		case 0xF5:
			Push16BitToStack(file, 3);
			break;
		case 0xF6:
			OrImmediate(file);
			break;
		case 0xF7:
			Restart(file, 6);
			break;
		case 0xF8:
			LoadSumHL(file);
			break;
		case 0xF9:
			LoadSPFromHL(file);
			break;
		case 0xFA:
			LoadAccumulatorFromDirect(file);
			break;
		case 0xFB:
			EnableInterrupts(file);
			break;
		case 0xFC:
			// Nothing
			break;
		case 0xFD:
			// Nothing
			break;
		case 0xFE:
			CompareImmediate(file);
			break;
		case 0xFF:
			Restart(file, 7);
			break;
		default:
			break;
	}
}
