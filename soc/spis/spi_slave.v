/**
 * SPI Slave that receives large amounts of data and writes to main memory via DMA.
 *
 * - all transfers are 32 bit words
 * - does not output any data on SPI bus
 */

module spi_slave(
	input clk, 
	input reset,

	// Bus - used to read and write registers
	input [2:0] register_num, 
	input [31:0] data_in,
	output [31:0] data_out,
	input wen,
	input ren,
	output ready,

	// Interface to qpimem_arb
	output qpimem_arb_do_write,
	input qpimem_arb_next_word,
	output [31:0] qpimem_arb_addr,
	output [31:0] qpimem_arb_wdata,
	input qpimem_arb_ack,

	// Signals from outside pins
	input SCK,
	input MOSI,
	output MISO,
	input CS    // Active low
	);

// Internal registers 
// Reg 1 - dest addr in RAM for DMA
reg [31:0] register_dma_dest_addr;

// Reg 0 - Control and status;
reg register_enable; // bit 0 (r/w)
wire transfer_in_progress; // bit 1 (ro)
reg register_dma_overflow; // bit 2 (r/w) - write 0 to reset

// chip select is active low
assign transfer_in_progress = !cs_out;

assign ready = 1;

// Register handling
always @(posedge clk) begin
	if (reset) begin
		register_dma_dest_addr <= 0;
		register_enable <= 0;
		register_dma_overflow <= 0;
	end else begin
		if (wen) begin
			case (register_num) 
				0: begin 
					register_enable <= data_in[0];
					register_dma_overflow <= data_in[2];
				end
				1: register_dma_dest_addr <= data_in;
				default: /*nop*/;
			endcase
		end
		if (ren) begin
			case (register_num) 
				0: data_out <= {
						29'b0, 
						register_dma_overflow, 
						transfer_in_progress, 
						register_enable
					};
				1: data_out <= register_dma_dest_addr;
				default: data_out <= 0;
			endcase
		end
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
	.reset(cs_start), // reset on cs
	.out_data(),
	.is_pos_edge(sck_edge),
	.is_neg_edge());

wire mosi_out;
spi_bit_fifo mosi_fifo(
	.in_data(MOSI),
	.clk(clk),
	.reset(cs_start), // reset on cs
	.out_data(mosi_out),
	.is_pos_edge(),
	.is_neg_edge());

// Always output zeros to spi master
assign MISO = 0;

// Word input FIFO
reg [31:0] input_bits; // FIFO for word in - 31 bits + 1 bit for guard
reg [31:0] dma_data_out;
reg dma_data_out_strobe;
wire dma_out_full;

// FIFO for DMA
spis_dma_write_fifo write_fifo(
	.clk(clk),
	.reset(reset | cs_start), // Reset on chip select
	.dma_addr(register_dma_dest_addr),

	// Data into fifo
	.dma_data_out(dma_data_out),
	.dma_data_out_strobe(dma_data_out_strobe),

	// Interface to qpimem_arb
	.qpimem_arb_do_write(qpimem_arb_do_write),
	.qpimem_arb_next_word(qpimem_arb_next_word),
	.qpimem_arb_addr(qpimem_arb_addr),
	.qpimem_arb_wdata(qpimem_arb_wdata),
	.qpimem_arb_ack(qpimem_arb_ack),

	// Status
	.empty(),
	.full(dma_out_full)
	);

always @(posedge clk) begin
	if (reset | !register_enable) begin
		// In reset or disabled - same thing
		input_bits <= {1'b1, 31'b0}; // set guard bit;
		dma_data_out <= 0;
		dma_data_out_strobe <= 0;
	end else begin
		// Transfer in progress means CS is low
		if (transfer_in_progress & !register_dma_overflow) begin
			// Reset strobe
			dma_data_out_strobe <= 0;

			// Check for clock edge
			if (sck_edge) begin
				if (input_bits[0]) begin
					// We see the guard bit, meaning mosi_out has the last
					// bit of input
					if (!dma_out_full) begin
						dma_data_out <= {mosi_out, input_bits[31:1]};
						dma_data_out_strobe <= 1;
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
			// CS is not active or else we have overflowed
			input_bits <= {1'b1, 31'b0}; // set guard bit;
			dma_data_out <= 0;
			dma_data_out_strobe <= 0;
		end
	end
end
endmodule

module spis_dma_write_fifo #(
	parameter integer FIFO_WORDS = 512
)(
	input clk,
	input reset,

	// Start address for dma	
	input [31:0] dma_addr,

	// Data into fifo
	input [31:0] dma_data_out,
	input dma_data_out_strobe,

	// Interface to qpimem_arb
	output reg qpimem_arb_do_write,
	input qpimem_arb_next_word,
	output reg [31:0] qpimem_arb_addr,
	output [31:0] qpimem_arb_wdata,
	input qpimem_arb_ack,

	// Status
	output empty,
	output full
);

reg [31:0] ram [0:FIFO_WORDS-1];
reg [$clog2(FIFO_WORDS)-1:0] w_ptr;
reg [$clog2(FIFO_WORDS)-1:0] r_ptr;

// TODO: This method of determining full means we can only use 
// 511 of the 512 words. Could do something more clever here.
assign empty = (r_ptr == w_ptr);
assign full = (w_ptr == (r_ptr - 1));

// Data to write always sent to qpimem_arb, but will not be
// written until do_write is detected
assign qpimem_arb_wdata = ram[r_ptr];

always @(posedge clk) begin
	if (reset) begin
		w_ptr <= 0;
		r_ptr <= 0;
		qpimem_arb_addr <= dma_addr;
	end else begin 
		if (empty) begin
			qpimem_arb_do_write <= 0;
		end else begin
			// If there is data available, try to write it
			qpimem_arb_do_write <= 1;
			if (qpimem_arb_next_word) begin
				r_ptr <= r_ptr + 1;
				qpimem_arb_addr <= qpimem_arb_addr + 4;
			end
		end

		// Add data to buffer
		if (dma_data_out_strobe) begin
			// Caller should not be trying to write if full, but just in case
			if (!full) begin
				ram[w_ptr] <= dma_data_out;
				w_ptr <= w_ptr + 1;
			end
		end

	end
end
endmodule;


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