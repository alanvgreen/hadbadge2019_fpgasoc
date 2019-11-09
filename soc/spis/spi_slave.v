/**
 * SPI Slave that receives large amounts of data and writes to main memory via DMA.
 *
 * - all transfers are a multiple of 32 32 bit words (1024bits) 
 * - does not output any data
 */

module SPI_slave(
	input clk, 
	input reset,

	// Bus - used to read and write registers
	input [2:0] register_num, 
	input [31:0] data_in,
	output [31:0] data_out,
	input wen,
	input ren,
	output ready,

	// Interface to dma_writer
	output [31:0] dma_addr;
	output [15:0] dma_len;
	output dma_run;
	input dma_done;

	output dma_strobe;
	output dma_data;

	// Signals from outside pins
	input SCK,
	input MOSI,
	output MISO,
	input CS,    // Active low
	);

// Internal registers 
// Reg 1 - dest addr in RAM for DMA
reg [31:0] register_dma_dest_addr;

// Reg 0 - Control and status;
reg enable; // bit 0 (r/w)
wire transfer_in_progress; // bit 1 (ro)

// chip select is active low
assign transfer_in_progress = !cs_out;

// Register handling
always @(posedge clk) begin
	if (wen) begin
		case (register_num) begin
			0: enable <= data_in[0];
			1: register_dma_dest_addr <= data_in;
			default: /*nop*/;
		end 
	end
	if (ren) begin
		case (register_num) begin
			0: data_out <= {30'b0, transfer_in_progress, enable};
			1: data_out <= register_dma_dest_addr;
			default: data_out <= 0;
		end 
	end
end

// DMA writer - always writes blocks of 32 bytes to addr
reg [31:0] dma_addr;
assign dma_len <= 32;

// Have SPI inputs cross clock domain
wire cs_out;
wire cs_start;
wire cs_end;
SPI_bit_fifo cs_fifo(
	.in_data(CS),
	.clk(clk),
	.reset(reset),
	.out_data(cs_out),
	.is_neg_edge(cs_start),
	.is_pos_edge(cs_end));

wire sck_edge;
SPI_bit_fifo sck_fifo(
	.in_data(SCK),
	.clk(clk),
	.reset(reset),
	.out_data(),
	.is_pos_edge(sck_edge),
	.is_neg_edge());

wire mosi_out;
SPI_bit_fifo sck_fifo(
	.in_data(MOSI),
	.clk(clk),
	.reset(reset),
	.out_data(mosi_out),
	.is_pos_edge(),
	.is_neg_edge());

// Always output zeros
assign MOSI = 0;

// Word input FIFO
reg [31:0] input_bits; // FIFO for word in - 31 bits + 1 bit for guard

// Number of words sent to dma_writer
reg [4:0] words_sent; // Counts up from 0 to 31


always @(posedge clk) begin
	if (reset) begin
		word_in_fifo <= {1'b1, 32'b0}; // set guard bit
		dma_strobe <= 0;
		dma_run <= 0;
		// Reset on cs low
		input_bits <= {1'b1, 31'b0}; // set guard bit;
		dma_addr <= register_dma_dest_addr;
		words_sent <= 0;

	end else begin
		// Things that are set by default
		dma_strobe <= 0;
		dma_run <= 0;

		if (transfer_in_progress) begin
			// TODO: any error checking

			// Check for clock edge
			if (sck_edge) begin
				if (words_sent == 0) begin
					dma_run = 1;
				end
				// Take data in
				if (word_in_fifo[0]) begin
					// See the guard bit
					// Strobe data into dma_writer
					dma_data <= {mosi_out, word_in_fifo[31:1]};
					dma_strobe <= 1;

					// Increment words sent, and if all done, then 
					// also increment dma address, ready to start again
					words_sent <= words_sent + 1;
					if (words_sent == 5'h1f) begin
						dma_addr <= dma_addr + 32;	
					end
				end else begin 
					// Don't see the guard bit
					input_bits <= {mosi_out, word_in_fifo[31:1]};
				end
			end
		end else begin
			if (cs_end) begin
				// Reset on cs low
				input_bits <= {1'b1, 31'b0}; // set guard bit;
				dma_addr <= register_dma_dest_addr;
				words_sent <= 0;
			end
		end
	end
end

endmodule


// A FIFO that handles both metatstability and edge detection
module SPI_bit_fifo #(
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
	)

// FIFO: bits added to MSB and taken from LSB
reg [BITS-1:0] fifo;

// Assign output from FIFO
assign out_data = fifo[0];
assign is_pos_edge = fifo[0] & !fifo[1];
assign is_neg_edge = !fifo[0] & fifo[1];

// Shift data through FIFO
always @(posedge clk) begin
	if (reset) begin
		fifo <= 0;
	end else begin
		fifo <= {in_data, fifo[BITS-1:1]};
	end
end
endmodule