/**
 * SPI Slave that receives large amounts of data and writes to main memory via DMA.
 *
 * It's unclear whether we can burst write across page boundaries. Because of this,
 * choose output addresses on 32 word (128byte) boundaries. Also, it is impossible to dma write
 * just one word, so always ensure that an even number of words are received.
 */

module spi_slave(
	input clk, 
	input reset,

	// Bus - used to read and write registers
	input [2:0] bus_addr, 
	input [31:0] bus_wdata,
	output reg [31:0] bus_rdata,

	input  wire bus_cyc,
	output wire bus_ack,
	input  wire bus_we,

	// Interface to qpimem_arb
	output qpimem_arb_do_write,
	input qpimem_arb_next_word,
	output reg [31:0] qpimem_arb_addr,
	output [31:0] qpimem_arb_wdata,

	// Signals from outside pins
	input SCK,
	input MOSI,
	output reg MISO,
	input CS    // Active low
	);

// Internal registers 

// Reg 0 - Control and status;
reg register_enable; // bit 0 (r/w)
wire transfer_in_progress; // bit 1 (ro)
reg register_dma_overflow; // bit 2 (ro) - renable to reset 
reg register_in_transaction; // bit 3 (ro)


// Reg 1 - dest addr in RAM for DMA
reg [31:0] register_dma_dest_addr;

// Reg 2 - number of 4 byte words received
reg [31:0] register_words_received; 

// chip select is active low
assign transfer_in_progress = !cs_out;

// Drive Bus
// todo: replace inputs with bus_cyc, bus_ack and bus_we
wire next_ack;
assign next_ack = bus_cyc & !ack;
reg ack;
always @(posedge clk) begin
	if (reset) begin
		ack <= 0;
	end else begin
		ack <= next_ack;
	end
end
assign bus_ack = ack;

// Register handling
always @(posedge clk) begin
	if (reset) begin
		register_dma_dest_addr <= 0;
		register_enable <= 0;
	end else begin
		if (bus_cyc) begin
			if (bus_we) begin
				case (bus_addr) 
					0: register_enable <= bus_wdata[0];
					1: register_dma_dest_addr <= bus_wdata;
					default: /*nop*/;
				endcase
			end else begin
				case (bus_addr) 
					0: bus_rdata <= {
							24'b0, 
							dma_flushing,
							dma_out_full,
							dma_out_empty,
							write_fifo_finished,
							register_in_transaction,
							register_dma_overflow, 
							transfer_in_progress, 
							register_enable
						};
					1: bus_rdata <= register_dma_dest_addr;
					2: bus_rdata <= register_words_received;
					3: bus_rdata <= dma_count;
					4: bus_rdata <= dma_out_diff;
					default: bus_rdata <= 0;
				endcase
			end
		end
	end
end


// Always output zeros to spi master
always @(posedge clk) begin
	if (reset) begin
		MISO <= 0;	
	end
end

// Have SPI inputs cross clock domain
wire cs_out;
wire cs_start;
wire cs_end;
spi_bit_fifo cs_fifo(
	.in_data(CS),
	.clk(clk),
	.reset(reset),
	.out_data(cs_out),
	.is_neg_edge(cs_start),
	.is_pos_edge(cs_end));

wire sck_edge;
spi_bit_fifo sck_fifo(
	.in_data(SCK),
	.clk(clk),
	.reset(reset | cs_start), // reset on cs
	.out_data(),
	.is_pos_edge(sck_edge),
	.is_neg_edge());

wire mosi_out;
spi_bit_fifo mosi_fifo(
	.in_data(MOSI),
	.clk(clk),
	.reset(reset | cs_start), // reset on cs
	.out_data(mosi_out),
	.is_pos_edge(),
	.is_neg_edge());

// Word input FIFO
reg [31:0] input_bits; // FIFO for word in - 31 bits + 1 bit for guard
reg [31:0] dma_data_out;
reg dma_data_out_strobe;
reg dma_data_out_flush;

wire dma_out_empty;
wire dma_out_full;
reg write_fifo_finished;

wire [5:0] dma_count;
wire dma_flushing;
wire [5:0] dma_out_diff;

// FIFO for DMA
spis_dma_write_fifo write_fifo(
	.clk(clk),
	.reset(reset | cs_start), // Reset on chip select
	.dma_addr(register_dma_dest_addr),

	// Data into fifo
	.dma_data_out(dma_data_out),
	.dma_data_out_strobe(dma_data_out_strobe),
	.flush(dma_data_out_flush),

	// Interface to qpimem_arb
	.qpimem_arb_do_write(qpimem_arb_do_write),
	.qpimem_arb_next_word(qpimem_arb_next_word),
	.qpimem_arb_addr(qpimem_arb_addr),
	.qpimem_arb_wdata(qpimem_arb_wdata),

	// Status
	.empty(dma_out_empty),
	.full(dma_out_full),
	.finished(write_fifo_finished),
	.dma_count(dma_count),
	.flushing(dma_flushing),
	.wr_diff(dma_out_diff)
	);

always @(posedge clk) begin
	if (reset | !register_enable) begin
		// In reset or disabled - same thing
		input_bits <= {1'b1, 31'b0}; // set guard bit;
		dma_data_out <= 0;
		dma_data_out_strobe <= 0;
		dma_data_out_flush <= 0;
		register_in_transaction <= 0;
		register_words_received <= 0;
		register_dma_overflow <= 0;
	end else begin
		// Set strobes default off 
		dma_data_out_strobe <= 0;
		dma_data_out_flush <= 0;

		if (cs_start) begin
			// Chip select just now selected
			register_in_transaction <= 1;
		end else if (!register_in_transaction) begin
			// Stop processing if not in transaction
		end else if (transfer_in_progress) begin
			// Check for clock edge
			if (sck_edge) begin
				if (input_bits[0]) begin
					// We see the guard bit, meaning mosi_out has the last
					// bit of input
					if (!dma_out_full) begin
						dma_data_out <= {mosi_out, input_bits[31:1]};
						dma_data_out_strobe <= 1;
						register_words_received <= register_words_received + 1;
						input_bits <= {1'b1, 31'b0}; // set guard bit;
					end else begin
						// err... dma was full, so signal error condition
						register_dma_overflow <= 1;
					end
				end else begin 
					// Don't see the guard bit - keep shifting
					input_bits <= {mosi_out, input_bits[31:1]};
				end
			end
		end else begin
			// Transaction ending
			dma_data_out_flush <= cs_end; // Just strobe once
			if (write_fifo_finished) begin
				register_in_transaction <= 0;
			end
		end
	end
end
endmodule


// Writing FIFO:
// To use, set dma_addr, then reset
// Write data_out and single-cycling strobe
// When all data written, raise flush for one cycle
// Wait for finished. Reset to use again.
module spis_dma_write_fifo #(
	parameter integer FIFO_WORDS = 64
)(
	input wire clk,
	input wire reset,

	// Start address for dma	
	input wire [31:0] dma_addr,

	// Data into fifo
	input wire [31:0] dma_data_out,
	input wire dma_data_out_strobe,
	input wire flush,

	// Interface to qpimem_arb
	output reg qpimem_arb_do_write,
	input wire qpimem_arb_next_word,
	output reg [31:0] qpimem_arb_addr,
	output wire [31:0] qpimem_arb_wdata,

	// Status
	output wire empty,
	output wire full,
	output reg finished,

	// Words remaining to write
	output reg [$clog2(FIFO_WORDS)-1:0] dma_count,
	// Are we flushing?
	output reg flushing,
	// Bytes in FIFO
	output wire [$clog2(FIFO_WORDS)-1:0] wr_diff
);

localparam PART_WRITE_SIZE = FIFO_WORDS / 2;

reg [31:0] ram [0:FIFO_WORDS-1];
reg [$clog2(FIFO_WORDS)-1:0] w_ptr;
reg [$clog2(FIFO_WORDS)-1:0] r_ptr;
assign wr_diff = w_ptr - r_ptr;


// TODO: This method of determining full means we can only use 
// 511 of the 512 words. Could do something more clever here.
assign empty = (wr_diff == 0);
assign full = (wr_diff == (FIFO_WORDS - 1));

// Data to write always sent to qpimem_arb, but will not be
// written until do_write is detected
assign qpimem_arb_wdata = ram[r_ptr];

// Reset logic
always @(posedge clk) begin
	if (reset) begin
		w_ptr <= 0;
		r_ptr <= 0;
		qpimem_arb_addr <= dma_addr;
		flushing <= 0;
		finished <= 0;
	end else begin
		// Incoming data - wite to buffer buffer
		if (dma_data_out_strobe) begin
			// Caller should not be trying to write if full, but just in case
			if (!full) begin
				ram[w_ptr] <= dma_data_out;
				w_ptr <= w_ptr + 1;
			end
		end

		// Outgoing data - only do if not finished
		if (!finished) begin
			// Normal running mode
			if (dma_count == 0) begin
				// No DMA running - determine whether we should start
				if (flushing) begin
					// Have finished a flush - we're done
					finished <= 1;
				end else if (flush) begin
					// Start a flush
					dma_count <= wr_diff; // which may be zero or one
					//  BUG: if dma starts with count == 1, it will fail
					flushing <= 1;
				end else if (wr_diff >= PART_WRITE_SIZE) begin
					dma_count <= PART_WRITE_SIZE;
				end			
			end else if (dma_count == 1) begin
				// DMA about to finish - hold write low one more cycle, then claim finished
				if (qpimem_arb_next_word) begin
					r_ptr <= r_ptr + 1;
					qpimem_arb_addr <= qpimem_arb_addr + 4;
					dma_count <= 0;
					qpimem_arb_do_write <= 0; // Just to be sure
				end
			end else /* (dma_count >= 2) */ begin
				// DMA running - raise do_write signal
				qpimem_arb_do_write <= 1; // may be overridden below

				// On next_word, put next word on bus
				if (qpimem_arb_next_word) begin
					r_ptr <= r_ptr + 1;
					qpimem_arb_addr <= qpimem_arb_addr + 4;
					dma_count <= dma_count - 1;

					// If about to write last word, then lower write signal
					if (dma_count == 2) begin
						qpimem_arb_do_write <= 0;
					end
				end
			end
		end 
	end
end 
endmodule


// A bit-wise FIFO that handles metatstability and edge detection
module spi_bit_fifo #(
	parameter integer BITS=4
) (
	// Incoming data
	input in_data,

	// Output clock
	input clk,
	input reset,
	output out_data,
	output is_neg_edge,
	output is_pos_edge
	);

// FIFO: bits added to MSB and taken from LSB
// Current data in position 1
// Previous-to-current data in position 0
reg [BITS-1:0] fifo;

// Assign output from FIFO
assign out_data = fifo[1]; 
assign is_pos_edge = fifo[1] & !fifo[0];
assign is_neg_edge = !fifo[1] & fifo[0];

// Shift data through FIFO
always @(posedge clk) begin
	if (reset) begin
		fifo <= 0;
	end else begin
		fifo <= {in_data, fifo[BITS-1:1]};
	end
end
endmodule