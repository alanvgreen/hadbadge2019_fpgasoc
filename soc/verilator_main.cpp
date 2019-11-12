/*
 * Copyright 2019 Jeroen Domburg <jeroen@spritesmods.com>
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include "Vsoc.h"
#include <verilated.h>
#include <verilated_vcd_c.h>
#include <verilated_fst_c.h>
#include "psram_emu.hpp"
#include "uart_emu.hpp"
#include "uart_emu_gdb.hpp"
#include "video/video_renderer.hpp"
#include "video/lcd_renderer.hpp"

////////////////////////////////////////

uint64_t spis_next_pos;
uint32_t spis_word_val;
uint32_t bit_counter;
enum spis_state_t { NOT_STARTED, WAITING_TO_START, SELECTED, SENDING, STOPPING, STOPPED } spis_state;

uint32_t genio_in;

void set_mosi(bool input) {
	if (input) {
		genio_in |= 2;
	} else {
		genio_in &= 0xfffd;
	}
}
void set_sck(bool input) {
	if (input) {
		genio_in |= 1;
	} else {
		genio_in &= 0xfffe;
	}
}
bool is_sck_high() {
	return genio_in & 1;
}


bool manywords(int word, uint32_t* out) {
	*out = (uint32_t) word;
	return word >= 320;
}

typedef bool (*nextwordfn_t)(int word, uint32_t* out);
struct spis_txn_t {
	uint64_t cs_start; // timestamp
	uint64_t cs_holdoff; // ns
	nextwordfn_t nextwordfn;
	uint64_t cs_holdon;
};


#define SPIS_HALF_TICK 31

spis_txn_t spis_txn = { 20 * 100 * 1000, 50, manywords, 50};

bool get_next_bit(bool *input) {
	bool done = false;
	int bit_num = bit_counter & 0x1f;
	if (bit_num == 0) {
		done = spis_txn.nextwordfn(bit_counter >> 5, &spis_word_val);
	} 
	*input = !!(spis_word_val & (1 << bit_num));
	bit_counter++;
	return done;
}

uint32_t spis_get(uint64_t pos) {
	if (pos < spis_next_pos) {
		return genio_in;
	}
	bool input;
	switch (spis_state) {
		case NOT_STARTED:
			set_sck(true);
			genio_in |= 1 << 27; // set cs high
			spis_next_pos += spis_txn.cs_start;
			spis_state = WAITING_TO_START;
			break;
		case WAITING_TO_START:
			genio_in &= ~(1<<27);  // set cs low
			spis_next_pos += spis_txn.cs_holdoff;
			spis_state = SELECTED;
			break;
		case SELECTED:		
			bit_counter = 0;
			get_next_bit(&input);
			set_mosi(input);
			set_sck(false);
			spis_next_pos += SPIS_HALF_TICK;
			spis_state = SENDING;
			break;
		case SENDING:
			if (is_sck_high()) {
				// clock high - get data ready and set clock low
				bool done = get_next_bit(&input);
				if (done) {
					spis_next_pos += spis_txn.cs_holdon;
					spis_state = STOPPING;
				} else {
					set_mosi(input);
					set_sck(false);
					spis_next_pos += SPIS_HALF_TICK;
				}
			} else {
				set_sck(true);
				spis_next_pos += SPIS_HALF_TICK;
			}
			break;
		case STOPPING:
			// Actually, go again
			genio_in |= 1 << 27; // set cs high
			spis_next_pos += spis_txn.cs_start;
			spis_state = WAITING_TO_START;

			//spis_next_pos = 1e9;
			//spis_state = STOPPED;
			break;
	}

	return genio_in;
}


/////////////////////////////////////////

int uart_get(int ts) {
	return 1;
}

int do_abort=0;
#define TAGMEM0 soc__DOT__qpimem_cache__DOT__genblk0__BRA__1__KET____DOT__tagdata__DOT__mem
#define TAGMEM1 soc__DOT__qpimem_cache__DOT__genblk1__BRA__1__KET____DOT__tagdata__DOT__mem

uint64_t ts=0;
uint64_t tracepos=0;

double sc_time_stamp() {
	return ts;
}

int main(int argc, char **argv) {
	// Initialize Verilators variables
	Verilated::commandArgs(argc, argv);
	Verilated::traceEverOn(true);

	// Create an instance of our module under test
	Vsoc *tb = new Vsoc;
	//Create trace
#if VERILATOR_USE_VCD
	VerilatedVcdC *trace = new VerilatedVcdC;
	tb->trace(trace, 3);
	trace->open("soctrace.vcd");
#else
	VerilatedFstC *trace = new VerilatedFstC;
	tb->trace(trace, 99);
	trace->open("soctrace.fst");
#endif

	tb->btn=0xff; //no buttons pressed
	int do_trace=1;

	Psram_emu psrama=Psram_emu(8*1024*1024);
	Psram_emu psramb=Psram_emu(8*1024*1024);
	psrama.force_qpi(); psramb.force_qpi();
	//ToDo: load elfs so we can mark ro sections as read-only
	psrama.load_file_interleaved("boot/rom.bin", 0, false, false);
	psramb.load_file_interleaved("boot/rom.bin", 0, false, true);

	psrama.load_file_interleaved("ipl/ipl.bin", 0x2000, false, false);
	psramb.load_file_interleaved("ipl/ipl.bin", 0x2000, false, true);

	Uart_emu uart=Uart_emu(64);
//	Uart_emu_gdb uart=Uart_emu_gdb(64);
//	Uart_emu uart=Uart_emu(416);

	Video_renderer *vid=new Video_renderer(false);
	Lcd_renderer *lcd=new Lcd_renderer();
//	Lcd_renderer *lcd=NULL;

	int oldled=0;
	int fetch_next=0;
	int next_line=0;
	int next_field=0;
	int pixel_clk=0;
	int clkint=0;
	int abort_timer=0;
	tb->rst = 1;

	while(ts < 2 * 1024 * 1024) {
		ts++;
		clkint+=123;
		tb->clkint=(clkint&0x100)?1:0;

		//if (do_trace) tracepos++;
		tracepos = ts;

		if (do_abort) {
			//Continue for a bit after abort has been signalled.
			abort_timer++;
			if (abort_timer==32) break;
		}
		tb->uart_rx=uart_get(ts*21);
		int rx;

		if (ts > 10)
			tb->rst = 0;
		
		tb->uart_rx = rx;
		tb->irda_rx = tb->irda_tx;
		tb->flash_sin = ts & 0xf;

		pixel_clk = !pixel_clk;
		tb->vid_pixelclk=pixel_clk?1:0;
		tb->adc4=tb->adcrefout?0:1;

		for (int c=0; c<4; c++)
		{
			int v;

			do_abort |= psrama.eval(tb->psrama_sclk, tb->psrama_nce,
					tb->soc__DOT__qspi_phy_psrama_I__DOT__spi_io_or,
					tb->soc__DOT__qspi_phy_psrama_I__DOT__spi_io_tr,
					&v);
			tb->soc__DOT__qspi_phy_psrama_I__DOT__spi_io_ir = v;

			do_abort |= psramb.eval(tb->psramb_sclk, tb->psramb_nce,
					tb->soc__DOT__qspi_phy_psramb_I__DOT__spi_io_or,
					tb->soc__DOT__qspi_phy_psramb_I__DOT__spi_io_tr,
					&v);
			tb->soc__DOT__qspi_phy_psramb_I__DOT__spi_io_ir = v;

			uart.eval(tb->clk48m, tb->uart_tx, &rx);

			tb->clk48m = (c >> 1) & 1;
			tb->clk96m = (c     ) & 1;

			tb->genio_in = spis_get(ts*20 + c*5 - 2);
			if (do_trace) trace->dump(tracepos*20 + c*5 - 2);

			tb->eval();

			if (do_trace) trace->dump(tracepos*20 + c*5);
		}

		do_trace = tb->trace_en;

		if (vid && pixel_clk) {
			vid->next_pixel(tb->vid_red, tb->vid_green, tb->vid_blue, &fetch_next, &next_line, &next_field);
			tb->vid_fetch_next=fetch_next;
			tb->vid_next_line=next_line;
			tb->vid_next_field=next_field;
		}
		if (lcd) lcd->update(tb->lcd_db, tb->lcd_wr, tb->lcd_rd, tb->lcd_rs);
		if (oldled != tb->led) {
			oldled=tb->led;
			printf("LEDs: 0x%X\n", oldled);
			if (tb->led==0x2a) do_abort=1;
			//if (oldled == 0x3A) do_trace=1;
		}
/*
		if (tb->soc__DOT__cpu__DOT__reg_pc==0x400060f4) {
			do_trace=1;
			printf("Trace start\n");
		}
		if (tb->soc__DOT__cpu__DOT__reg_pc==0x400002d4) {
			do_trace=0;
			printf("Trace stop\n");
		}
*/
//		printf("%X\n", tb->soc__DOT__cpu__DOT__reg_pc);
	};
//	printf("Verilator sim exited, pc 0x%08X\n", tb->soc__DOT__cpu__DOT__reg_pc);
	trace->flush();

	trace->close();
	exit(EXIT_SUCCESS);
}
