
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#define BUF_SIZE 1

#define OPCODE_MASK 0xFC
#define D_MASK 0x02
#define W_MASK 0x01
#define MOD_MASK 0xC0
#define REG_MASK 0x38
#define RM_MASK 0x07

#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(*arr))

typedef unsigned char u8;

struct Register {
	char *name;
};

struct Opcode
{
	char *instruction;
	u8 trailing_bytes;
};

typedef enum Op {
	BYTE,
	WORD
} Op;

struct Register registers[2][8] = {
	/* byte */
	{
		[0x00] = { .name = "al" },
		[0x04] = { .name = "ah" },
		[0x01] = { .name = "cl" },
		[0x05] = { .name = "ch" }
	},
	/* word */
	{
		[0x00] = { .name = "ax" },
		[0x01] = { .name = "cx" },
		[0x02] = { .name = "dx" },
		[0x03] = { .name = "bx" },
		[0x04] = { .name = "sp" },
		[0x05] = { .name = "bp" },
		[0x06] = { .name = "si" },
		[0x07] = { .name = "di" },
	}
};

struct Opcode opcodes[] = {
	[0x22]	 = { .instruction = "mov", .trailing_bytes = 1 }
};

struct Opcode extract_opcode(u8 byte) 
{
	u8 opcode = (byte & OPCODE_MASK) >> 2;	
	return opcodes[opcode];
}

u8 extract_direction(u8 byte)
{
	return (byte & D_MASK) >> 1;
}

u8 extract_op(u8 byte)
{
	return byte & W_MASK;
}

u8 extract_mode(u8 byte)
{
	return (byte & MOD_MASK) >> 6;
}

u8 extract_reg(u8 byte)
{
	return (byte & REG_MASK) >> 3;
}

u8 extract_reg_mem(u8 byte)
{
	return byte & RM_MASK;
}

void emit_instruction(struct Opcode opcode, struct Register src, struct Register dest)
{
	printf("%s %s,%s\n", opcode.instruction, dest.name, src.name);
}

/* Returns the Register struct */
struct Register get_register(u8 op, u8 reg)
{
	return registers[op][reg];
}

void parse_binary(FILE* file)
{
	u8 					buffer[BUF_SIZE];
	u8					direction, op;
	struct Opcode 		opcode;
	struct Register 	src_reg, dest_reg;
	long				offset, pos;
	size_t				ret;

	for(;;)
	{
		// Get op
		ret = fread(buffer, BUF_SIZE, ARRAY_COUNT(buffer), file);
		if (ret != ARRAY_COUNT(buffer))
			return;
		opcode = extract_opcode(*buffer);
		direction = extract_direction(*buffer);
		op = extract_op(*buffer);

		// Read n following bytes
		ret = fread(buffer, opcode.trailing_bytes, ARRAY_COUNT(buffer), file);

		if (ret != ARRAY_COUNT(buffer))
			return;
	
		u8 mode = extract_mode(*buffer);
		u8 reg = extract_reg(*buffer);
		u8 rm = extract_reg_mem(*buffer);
		
		if (direction == 0) {
			src_reg = get_register(op, reg);
			dest_reg = get_register(op, rm);
		} else {
			src_reg = get_register(op, rm);
			dest_reg = get_register(op, reg);
		}

		emit_instruction(opcode, src_reg, dest_reg);
	}
}
int main(int argc, char **argv)
{
	if (argc != 2) {
		printf("Usage: sim8086 [binary]\n");
		exit(1);
	}
	FILE 	*binary;

	if ((binary = fopen(argv[1], "r")) == NULL)
	{
		printf("Unable to open file: %s\n", argv[1]);
		exit(1);
	}

	parse_binary(binary);
	return 0;
}
