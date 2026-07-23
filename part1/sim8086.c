
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#define BUF_SIZE 6

#define OPCODE_4MASK 0xF0
#define OPCODE_6MASK 0xFC
#define OPCODE_7MASK 0xFE
#define D_MASK 0x02
#define W_MASK 0x01
#define MOD_MASK 0xC0
#define REG_MASK 0x38
#define RM_MASK 0x07

//#define ARRAY_COUNT(arr) (sizeof(arr) / sizeof(*arr))

static const char *err_msg = "Issue parsing binary\n";
typedef unsigned char u8;

#if defined(_WIN32) || defined(_WIN64) || defined(__linux__)
	typedef unsigned long u32;
#elif defined(__APPLE__) || defined(__MACH__)
	typedef unsigned int u32;
#endif

struct Register
{
	char *name;
};

typedef enum EacOperandType
{
	EOT_REGISTER,
	EOT_DISPLACEMENT,
	EOT_DIRECT_ADDRESS,
	EOT_NULL
} EacOperandType;

typedef enum Op
{
	BYTE,
	WORD
} Op;

typedef enum Displacement
{
	D8,
	D16
} Displacement;

struct EacOperand
{
	EacOperandType type;
	union
	{
		struct Register reg;
		Displacement displacement;
	} data;

} EacOperand;

typedef enum Opcode
{
	OP_MOV_REG_MEM_TO_FROM_REG,
	OP_MOV_IMM_TO_REG_REM,
	OP_MOV_IMM_TO_REG,
	OP_MOV_MEM_TO_ACC,
	OP_MOV_ACC_TO_MEM
} Opcode;

/* Effective Address Calcs */
typedef enum EAC
{
	BX_SI,
	BX_DI,
	BP_SI,
	BP_DI,
	SI,
	DI,
	DA, /* Direct address */
	BX,
	BX_SI_8,
	BX_DI_8,
	BP_SI_8,
	BP_DI_8,
	SI_8,
	DI_8,
	BP_8,
	BX_8,
	BX_SI_16,
	BX_DI_16,
	BP_SI_16,
	BP_DI_16,
	SI_16,
	DI_16,
	BP_16,
	BX_16
} EAC;

static const Opcode opcodes[] = {
	[0x22]	 = OP_MOV_REG_MEM_TO_FROM_REG, 
	[0x63]   = OP_MOV_IMM_TO_REG_REM,
	[0x0B]	 = OP_MOV_IMM_TO_REG,
	[0x60]	 = OP_MOV_MEM_TO_ACC,
	[0x61]	 = OP_MOV_ACC_TO_MEM
};

/* See page 4-20 of 8086 manual for register/memory field encoding */
static const struct Register registers[2][8] = {
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

static const EAC eac_lookup[3][8] = {
	{
		[0x00] = BX_SI,
		[0x01] = BX_DI,
		[0x02] = BP_SI,
		[0x03] = BP_DI,
		[0x04] = SI,
		[0x05] = DI,
		[0x06] = DA,
		[0x07] = BX
	},
	{
		[0x00] = BX_SI_8,
		[0x01] = BX_DI_8,
		[0x02] = BP_SI_8,
		[0x03] = BP_DI_8,
		[0x04] = SI_8,
		[0x05] = DI_8,
		[0x06] = BP_8,
		[0x07] = BX_8
	},
	{
		[0x00] = BX_SI_16,
		[0x01] = BX_DI_16,
		[0x02] = BP_SI_16,
		[0x03] = BP_DI_16,
		[0x04] = SI_16,
		[0x05] = DI_16,
		[0x06] = BP_16,
		[0x07] = BX_16
	}
};

struct eac_calc_exp {
	struct EacOperand operand_1;
	struct EacOperand operand_2;
	struct EacOperand operand_3;
} eac_calc;

/* Using designated init for readibility only.
   Maps EAC to the operands of the calculation
   I don't like setting displacement to zero for EOT_NULL but the tag is what counts
*/
static const struct eac_calc_exp eac_calc_lookup[] = {
	[BX_SI]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][3] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][6] } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } }, 
	[BX_DI]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][3] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][7] } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[BP_SI]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][5] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][6] } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[BP_DI]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][5] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][7] } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[SI]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][6] } },
					.operand_2 = { .type = EOT_NULL, .data = { .displacement = 0 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[DI]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][7] } },
					.operand_2 = { .type = EOT_NULL, .data = { .displacement = 0 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[DA]		= { .operand_1 = { .type = EOT_DIRECT_ADDRESS, .data = { .displacement = 0 } },
					.operand_2 = { .type = EOT_NULL, .data = { .displacement = 0 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[BX]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][3] } },
					.operand_2 = { .type = EOT_NULL, .data = { .displacement = 0 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[BX_SI_8]	= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][3] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][6] } },
					.operand_3 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D8 } } },
	[BX_DI_8]	= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][3] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][7] } },
					.operand_3 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D8 } } },
	[BP_SI_8]	= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][5] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][6] } },
					.operand_3 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D8 } } },
	[BP_DI_8]	= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][5] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][7] } },
					.operand_3 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D8 } } },
	[SI_8]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][6] } },
					.operand_2 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D8 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[DI_8]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][7] } },
					.operand_2 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D8 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[BP_8]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][5] } },
					.operand_2 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D8 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[BX_8]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][3] } },
					.operand_2 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D8 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[BX_SI_16]	= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][3] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][6] } },
					.operand_3 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D16 } } },
	[BX_DI_16]	= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][3] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][7] } },
					.operand_3 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D16 } } },
	[BP_SI_16]	= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][5] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][6] } },
					.operand_3 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D16 } } },
	[BP_DI_16]	= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][5] } },
					.operand_2 = { .type = EOT_REGISTER, .data = { .reg = registers[1][7] } },
					.operand_3 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D16 } } },
	[SI_16]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][6] } },
					.operand_2 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D16 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[DI_16]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][7] } },
					.operand_2 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D16 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[BP_16]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][5] } },
					.operand_2 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D16 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } },
	[BX_16]		= { .operand_1 = { .type = EOT_REGISTER, .data = { .reg = registers[1][3] } },
					.operand_2 = { .type = EOT_DISPLACEMENT, .data = { .displacement = D16 } },
					.operand_3 = { .type = EOT_NULL, .data = { .displacement = 0 } } }
};

/* Parses the EAC expr */
void perform_eac(struct eac_calc_exp expr)
{
	//TODO (brett): implement		
}

//TODO (brett): we need a better way to check the opcode prefixes
Opcode extract_opcode(u8 byte) 
{

	Opcode		rv;
	u8			opcode;
	
	// check the 4 bit prefixes
	opcode = (byte & OPCODE_4MASK) >> 4;
	if (opcode <= 0x0B)
		rv = opcodes[opcode];

	if (rv != 0)
		return rv;

	// check the 6 bit prefixes
	opcode = (byte & OPCODE_6MASK) >> 2;	
	if (opcode <= 0x22)
		rv = opcodes[opcode];
	
	if (rv != 0)
		return rv;

	// check the 7 bit prefixes
	opcode = (byte & OPCODE_7MASK) >> 1;
	if (opcode <= 0x63)
		rv = opcodes[opcode];

	if (rv != 0)
		return rv;

	rv = opcodes[opcode];

	return rv;
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

/* Returns the Register struct */
struct Register get_register(u8 op, u8 reg)
{
	return registers[op][reg];
}

u8 parse_instruction(FILE *binary, u8 *buffer, Opcode opcode, long offset, long pos)
{
	u8				direction, op, rv;
	size_t			ret;
	struct Register src_rm, dest_rm;

	switch (opcode)
	{
		case OP_MOV_REG_MEM_TO_FROM_REG:
			
			direction = extract_direction(*buffer);
			op = extract_op(*buffer);

			ret = fread(buffer, 1, 1, binary); 
			if (ret != 1)
				return -1; /* TODO (brett): better error handling */
			
			u8 mode = extract_mode(*buffer);
			u8 reg = extract_reg(*buffer);
			u8 rm = extract_reg_mem(*buffer);

			switch (mode)
			{
				case 0x03:
					{
						struct Register src_rm = direction == 0 ? get_register(op, reg) : get_register(op, rm);
						struct Register dest_rm = direction == 0 ? get_register(op, rm) : get_register(op, reg);
						printf("mov %s, %s\n", dest_rm.name, src_rm.name);
					}
				case 0x00:
				case 0x01:
				case 0x02:
				default:
					return -1; /* TODO (brett): better error handling */
			}
		case OP_MOV_IMM_TO_REG_REM:
		case OP_MOV_IMM_TO_REG:
		case OP_MOV_MEM_TO_ACC:
		case OP_MOV_ACC_TO_MEM:
		default:
			return -1;
	}
}

u8 parse_binary(FILE* binary)
{
	u8 					buffer[BUF_SIZE];
	u8					rv;
	Opcode 				opcode;
	long				offset, pos;
	size_t				ret;

	for(;;)
	{
		// Get op
		ret = fread(buffer, 1, 1, binary);
		if (ret == 0)
			return 0;
		opcode = extract_opcode(*buffer);

		if ((rv = parse_instruction(binary, buffer, opcode, offset, pos)) == 0)
			return -1;
	}
	return 0;
}
int main(int argc, char **argv)
{
	if (argc != 2) {
		printf("Usage: sim8086 [binary]\n");
		exit(1);
	}
	FILE 	*binary;
	u8		rv;

	if ((binary = fopen(argv[1], "r")) == NULL)
	{
		printf("Unable to open file: %s\n", argv[1]);
		exit(1);
	}

	if ((rv = parse_binary(binary)) != 0)
	{
		write(2, err_msg, strlen(err_msg));
		exit(1); 
	}
	return 0;
}
