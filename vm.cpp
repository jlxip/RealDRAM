// Uncomment this to enable -d and -s options.
//#define DEBUG

#include <iostream>
#include <fstream>
#include <map>
#include <sstream>

enum {
	EXIT_FINISHED,
	EXIT_NO_BINARY,
	EXIT_BAD_FILE,
	EXIT_FETCH_ERROR,
	EXIT_NO_SYMBOL,
	EXIT_BAD_SYMBOL
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

#define IS_OI(x)        (x & 0b00000010)
#define IS_DI(x)        (x & 0b00000001)


// VM state.
data_t memory[MEM_SIZE] = {0};
addr_t addr = 0;
bool flag = false;

cycle_t cycles = 0;
std::map<addr_t, cycle_t> expire;	// cycle at which the byte is lost.

#ifdef DEBUG
std::map<addr_t, std::string> symbols;	// Reversed symbols.
bool debug = false;
bool step = false;

bool loadSymbols(const char* path) {
	std::ifstream f(path);
	if(!f.good())
		return false;

	std::string lastLine;
	while(getline(f, lastLine)) {
		std::stringstream ss;
		ss << lastLine;

		std::string theName;
		addr_t theAddr;
		ss >> theName >> theAddr;
		symbols[theAddr] = theName;
	}

	f.close();
	return true;
}

void showRelativeSymbol() {
	std::cout << "[";

	// Closest symbol from below?
	auto it = symbols.lower_bound(addr);
	if(it == symbols.end()) {
		std::cout << "unknown+" << addr;
	} else if((*it).first == addr) {
		std::cout << (*it).second;
	} else if(it == symbols.begin()) {
		std::cout << "unknown+" << addr;
	} else {
		--it;
		std::cout << (*it).second << "+" << addr - (*it).first;
	}

	std::cout << "]";
}
#endif


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
		if((*it).second <= cycles) {
			// Expired!
			memory[a] = 0;
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
#ifdef DEBUG
		std::cout << "Usage: " << argv[0] << " <path to binary> [-d, --debug <symbols>] [-s, --step]" << std::endl;
#else
		std::cout << "Usage: " << argv[0] << " <path to binary>" << std::endl;
#endif
		return EXIT_NO_BINARY;
	}

#ifdef DEBUG
	// Arguments.
	for(int i=2; i<argc; ++i) {
		std::string thisArg(argv[i]);

		if(thisArg == "-d" || thisArg == "--debug") {
			debug = true;
			if(i+1 == argc) {
				std::cout << "-d option requires a symbol table (generated by rdrama-resolver)." << std::endl;
				return EXIT_NO_SYMBOL;
			}

			if(!loadSymbols(argv[++i]))
				return EXIT_BAD_SYMBOL;
		} else if(thisArg == "-s" || thisArg == "--step") {
			step = true;
		}
	}
#endif

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

#ifdef DEBUG
	if(step)
		while(execute())
			std::getchar();
	else while(execute());
#else
	while(execute());
#endif

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
#ifdef DEBUG
	if(debug) {
		showRelativeSymbol();
		std::cout << " Branch to 0x" << std::hex;
	}
#endif

	addr_t next = getAddr(naddr);

#ifdef DEBUG
	if(debug)
		std::cout << next;
#endif

	if(IS_INDIRECT(opcode)) {
		next = getAddr(next);
#ifdef DEBUG
		if(debug)
			std::cout << " (actually 0x" << next << ")";
#endif
	}

#ifdef DEBUG
	if(flag) {
		naddr = next;
		if(debug)
			std::cout << ". PERFORMED." << std::endl;
	} else if(debug) {
		std::cout << ". NOT PERFORMED." << std::endl;
	}
#else
	if(flag)
		naddr = next;
#endif

	++cycles;
}

void execute_nand(data_t opcode, addr_t& naddr) {
	addr_t orig = getAddr(naddr);
	addr_t dest = IS_ITSELF(opcode) ? orig : getAddr(naddr);

#ifdef DEBUG
	if(debug) {
		showRelativeSymbol();
		std::cout << " Nand from 0x" << std::hex << orig;
	}
#endif

	// Indirection bits.
	if(IS_OI(opcode)) {
		orig = getAddr(orig);
#ifdef DEBUG
		if(debug)
			std::cout << " (actually 0x" << std::hex << orig << ")";
#endif
	}

#ifdef DEBUG
	if(debug)
		std::cout << " to 0x" << dest;
#endif

#ifdef DEBUG
	if(IS_DI(opcode)) {
		dest = getAddr(dest);
		if(debug)
			std::cout << " (actually 0x" << std::hex << dest << "). ";
	} else if(debug) {
		std::cout << ". ";
	}
#else
	if(IS_DI(opcode))
		dest = getAddr(dest);
#endif

	// Special addresses.
	switch(dest) {
	case FLUSHR_ADDR:
		std::cin >> *(char*)&memory[IO_ADDR];
#ifdef DEBUG
		if(debug)
			std::cout << "Read " << (char)memory[IO_ADDR] << std::endl;
#endif
		flag = true;
		++cycles;
		return;
	case FLUSHW_ADDR:
		std::cout << (char)readMem(IO_ADDR);
#ifdef DEBUG
		if(debug)
			std::cout << "Write " << (char)memory[IO_ADDR] << std::endl;
#endif
		flag = true;
		++cycles;
		return;
	}

	if(IS_BYTE(opcode)) {
		// Byte nand.
#ifdef DEBUG
		if(debug)
			std::cout << "Whole byte. 0x" << std::hex << (int)readMem(orig) << " ~& 0x" << (int)readMem(dest) << " = 0x";
#endif

		memory[dest] = ~(readMem(orig) & readMem(dest));
		flag = memory[dest];	// No need to memRead.
		cycles += 8;
	} else {
		// Bit nand.
		uint8_t bit = BIT_NAND_HI(opcode) | BIT_NAND_MID(opcode) | BIT_NAND_LO(opcode);

#ifdef DEBUG
		if(debug)
			std::cout << "Bit " << (int)bit << ". 0x" << std::hex << (int)readMem(orig) << " b~& 0x" << (int)readMem(dest) << " = 0x";
#endif

		uint8_t mask = 1 << bit;

		data_t dorig = (readMem(orig) & mask) >> bit;
		data_t ddest = (readMem(dest) & mask) >> bit;

		flag = (~(dorig & ddest)) & 1;
		if(flag)
			memory[dest] |= (1 << bit);
		else
			memory[dest] &= ~(1 << bit);

		++cycles;
	}

	expire[dest] = cycles + MEMORY_LOSS;

#ifdef DEBUG
	if(debug)
		std::cout << (int)memory[dest] << std::endl;
#endif
}
