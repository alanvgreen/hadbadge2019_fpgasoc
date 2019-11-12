#include <stdlib.h>
#include "Vspistest.h"
#include <verilated.h>
#include <verilated_vcd_c.h>
#include "../psram_emu.hpp"

uint64_t ts=0;

Vspistest *tb;
Psram_emu *psram;
VerilatedVcdC *trace;

double sc_time_stamp() {
	return ts;
}

#define CHECK(x) do { if (!(x)) printf("%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); } while(0)

#define SPIS_HALF_TICK 25
bool ten_words(int word, uint32_t* out) {
	*out = (uint32_t) word;
	return word >= 10;
}

bool manywords(int word, uint32_t* out) {
	*out = (uint32_t) word;
	return word >= 1024;
}

typedef bool (*nextwordfn_t)(int word, uint32_t* out);
struct spis_txn_t {
	uint64_t cs_start; // timestamp
	uint64_t cs_holdoff; // ns
	nextwordfn_t nextwordfn;
	uint64_t cs_holdon;
};

spis_txn_t spis_txn { 522, 50, manywords, 50};
enum spis_state_t { NOT_STARTED, SELECTED, SENDING, STOPPING, STOPPED } spis_state;
uint64_t spis_next_event;
uint32_t spis_word_val;
int spis_next_bit;

void init_spis() {
	spis_next_event = spis_txn.cs_start;
	tb->spis_ncs = 1;
}

bool get_next_bit(bool *out) {
	bool done = false;
	int bit = spis_next_bit & 0x1f;
	if (bit == 0) {
		done = spis_txn.nextwordfn(spis_next_bit >> 5, &spis_word_val);
	} 
	*out = !!(spis_word_val & (1 << bit));
	spis_next_bit++;
	return done;
}

void process_spis() {
	ts = spis_next_event;

	bool bit;

	if (spis_state == NOT_STARTED) {
		tb->spis_ncs = 0;
		spis_next_event += spis_txn.cs_holdoff;
		spis_state = SELECTED;
	} else if (spis_state == SELECTED) {
		spis_next_bit = 0;
		get_next_bit(&bit);
		tb->spis_mosi = bit;
		spis_next_event += SPIS_HALF_TICK;
		spis_state = SENDING;
	} else if (spis_state == SENDING) {
		// Assuming clock low idle, and bits valid for pos edge
		if (tb->spis_clk) {
			// If clock is high, change to next 
			bool bit;
			bool done = get_next_bit(&bit);
			tb->spis_clk = 0;
			tb->spis_mosi = bit;
			if (done) {
				spis_next_event += spis_txn.cs_holdon;
				spis_state = STOPPING;
			} else {
				spis_next_event += SPIS_HALF_TICK;
			}
		} else {
			// Hold data steady for transition
			tb->spis_clk = 1;
			spis_next_event += SPIS_HALF_TICK;
		}
	} else if (spis_state == STOPPING) {
		tb->spis_ncs = 1;
		spis_next_event = 1e8;
		spis_state = STOPPED;
	}
	tb->eval();
	trace->dump(ts);
}


#define SPIS_HALF_TICK 25
int spis_last = 0;

int halfclk(int advance, int clk) {
	int sin;
	uint64_t next = ts + advance;

	if (next >= spis_next_event) {
		process_spis();
	}

	ts = next;	
	int abort = psram->eval(tb->spi_clk, tb->spi_ncs, tb->spi_sout, tb->spi_oe, &sin);
	tb->clk = clk;
	tb->eval();
	tb->spi_sin=sin;
	trace->dump(ts);
	return abort;
}


//doclk advances time by 21 units
int doclk() {
	int abort = halfclk(10, 0);
	abort |= halfclk(11, 1);
	return abort;
}

void do_write(int addr, int data) {
	tb->addr=addr;
	tb->wdata=data;
	tb->select=1;
	tb->wstrb=0xf;
	do {
		doclk();
	} while (tb->bus_ack==0);
	tb->select=0;
	tb->wstrb=0;
	doclk();
}

int do_read(int addr) {
	int ret;
	tb->addr=addr;
	tb->select=1;
	do {
		doclk();
	} while (tb->bus_ack==0);
	tb->select=0;
	ret=tb->rdata;
	doclk();
	return ret;
}

int main(int argc, char **argv) {
	// Initialize Verilators variables
	Verilated::commandArgs(argc, argv);
	Verilated::traceEverOn(true);

	tb = new Vspistest;
	trace = new VerilatedVcdC;
	tb->trace(trace, 99);
	trace->open("spistest.vcd");
	psram = new Psram_emu(8*1024*1024);
	psram->force_qpi();

	init_spis();
	tb->rst = 1;
	doclk();
	tb->rst=0;
	doclk();
	doclk();
	doclk();
	doclk();

	// Enable unit, set input addr to 0x10000
	do_write(0x0, 0x1);
	do_write(0x4, 0x40010000);

	while (ts < 2 * 1024 * 1024) {
		doclk();
	}

	// Dump memory to file
	FILE *dump = fopen("dump.bin","wb");  // w for write, b for binary
	fwrite(psram->get_mem(),8*1024*1024,1,dump); 
	fclose(dump);


/*

	do_read(0x20000); //dummy because ?????
	do_write(0x10001, 0xdeadbeef);
	do_write(0x10002, 0xcafebabe);

	CHECK(do_read(0x10001)==0xdeadbeef);
	CHECK(do_read(0x10002)==0xcafebabe);

    do_flush(0, 64);


	printf("Writing...\n");
	srand(0);
	for (int i=0; i<1000; i++) {
		int a=rand()&0x1fffff;
		int d=rand()&0xffffffff;
		do_write(a, d);
	}

	printf("Reading...\n");
	srand(0);
	for (int i=0; i<1000; i++) {
		int a=rand()&0x1fffff;
		int d=rand()&0xffffffff;
		int td=do_read(a);
		if (td!=d) printf("It %d, addr %d: wrote %x read %x\n", i, a, d, td);
	}

*/

	trace->flush();

	trace->close();
	exit(EXIT_SUCCESS);
}