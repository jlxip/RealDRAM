#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <map>

enum {
	EXIT_FINISHED,
	EXIT_NO_BINARY,
	EXIT_BAD_FILE,
	EXIT_FETCH_ERROR
};

// Cycles until a bit or byte is lost.
#define MEMORY_LOSS 128

#define FLUSHR_ADDR 0xFFFD
#define FLUSHW_ADDR 0xFFFE
#define FINAL_ADDR  0xFFFF
#define IO_ADDR     0xFFFF

typedef unsigned char  uint8_t;
typedef unsigned char  data_t;
typedef unsigned short addr_t;
typedef unsigned long  cycle_t;

#define MEM_SIZE        0x10000
#define IS_NAND(x)      (x & 0b10000000)
#define IS_INDIRECT(x)  (x & 0b01000000)
#define IS_BYTE(x)      (x & 0b01000000)
#define IS_ITSELF(x)    (x & 0b00100000)
#define REACHED(x)      (x == FINAL_ADDR)

#define BIT_NAND_HI(x)  ((x & 0b00010000) >> 2)
#define BIT_NAND_MID(x) ((x & 0b00001000) >> 2)
#define BIT_NAND_LO(x)  ((x & 0b00000100) >> 2)


// VM state.
data_t memory[MEM_SIZE] = {0};
addr_t addr = 0;
bool flag = false;

struct Expiration {
	cycle_t cycle;	// Cycle at which the memory is lost.
	uint8_t bit;	// Bit to lose. 8 if it's the whole byte.

	Expiration() : cycle(0), bit(0) {}
	Expiration(cycle_t cycle, uint8_t bit) : cycle(cycle), bit(bit) {}
};

cycle_t cycles = 0;
std::map<addr_t, Expiration> expire;


// Auxiliary functions.
[[ noreturn ]] void fetchError() {
	std::cout << "[Fetch error at 0x" << std::hex << addr << "]" << std::endl;
	exit(EXIT_FETCH_ERROR);
}

data_t readMem(addr_t a) {
	// Expired?
	auto it = expire.find(a);
	if(it != expire.end()) {
		// Has been written.
		Expiration exp = (*it).second;
		if(exp.cycle <= cycles) {
			// Expired!
			if(exp.bit == 8) {
				// Whole byte.
				memory[a] = 0;
			} else {
				// bit-th bit.
				memory[a] &= ~(1 << exp.bit);
			}

			expire.erase(it);
		}
	}

	return memory[a];
}

inline data_t getData(addr_t& naddr) {
	if(REACHED(naddr))
		fetchError();
	return readMem(naddr++);
}

inline addr_t getAddr(addr_t& naddr) {
	addr_t lo = getData(naddr);
	addr_t hi = getData(naddr) << 8;
	return hi | lo;
}

// Regular functions.
bool execute();
void execute_branch(data_t opcode, addr_t& naddr);
void execute_nand(data_t opcode, addr_t& naddr);

int main(int argc, char* argv[]) {
	if(argc < 2) {
		std::cout << "Usage: " << argv[0] << " <path to binary>" << std::endl;
		return EXIT_NO_BINARY;
	}

	// Populate memory.
	std::ifstream input(argv[1]);

	if(!input.good()) {
		std::cout << "Could not open file " << argv[1] << std::endl;
		return EXIT_BAD_FILE;
	}

	input.seekg(0, std::ios::end);
	int length = input.tellg();
	input.seekg(0, std::ios::beg);

	addr_t count = std::min(MEM_SIZE, length);
	input.read((char*)memory, count);
	input.close();

	while(execute());

	std::cout << "[Exited]" << std::endl;
	return EXIT_FINISHED;
}

bool execute() {
	if(REACHED(addr))
		return false;

	addr_t naddr = addr;
	data_t opcode = getData(naddr);

	if(IS_NAND(opcode))
		execute_nand(opcode, naddr);
	else
		execute_branch(opcode, naddr);

	addr = naddr;
	return true;
}

void execute_branch(data_t opcode, addr_t& naddr) {
	unsigned char iterations = IS_INDIRECT(opcode) ? 2 : 1;
	while(iterations--)
		naddr = getAddr(naddr);

	++cycles;
}

void execute_nand(data_t opcode, addr_t& naddr) {
	addr_t orig = getAddr(naddr);
	addr_t dest = IS_ITSELF(opcode) ? orig : getAddr(naddr);

	// Special addresses.
	switch(dest) {
	case FLUSHR_ADDR:
		std::cin >> *(char*)&memory[IO_ADDR];
		flag = true;
		return;
	case FLUSHW_ADDR:
		std::cout << (char)readMem(IO_ADDR);
		flag = true;
		return;
	}

	if(IS_BYTE(opcode)) {
		// Byte nand.
		memory[dest] = ~(readMem(orig) & readMem(dest));
		flag = memory[dest];	// No need to memRead.
		cycles += 8;
		expire[dest] = Expiration(cycles + MEMORY_LOSS, 8);
	} else {
		// Bit nand.
		uint8_t bit = BIT_NAND_HI(opcode) | BIT_NAND_MID(opcode) | BIT_NAND_LO(opcode);
		uint8_t mask = 1 << bit;

		data_t dorig = (readMem(orig) & mask) >> bit;
		data_t ddest = (readMem(dest) & mask) >> bit;

		flag = ~(dorig & ddest);
		if(flag)
			memory[dest] |= (1 << bit);
		else
			memory[dest] &= ~(1 << bit);

		++cycles;
		expire[dest] = Expiration(cycles + MEMORY_LOSS, bit);
	}
}
