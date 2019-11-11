/*
 * Copyright (C) 2019  Alan Green <alan.green@gmail.com> 
 * All rights reserved.
 *
 * BSD 3-clause, see LICENSE.bsd
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
`timescale 1us/1ns

module spistest(
	// Everyone's faves
	input clk,
	input rst,
	
	// Memory bus
	input [21:0] addr,
	output [31:0] wdata,
	output [31:0] rdata,
	input [3:0] wen,
	input ren,
	output ready,

	// SQI/QPI pins for memory chips
	output spi_clk,
	output spi_ncs,
	output [3:0] spi_sout,
	input [3:0] spi_sin,
	output spi_oe,
	output spi_bus_qpi,

	// Slave SPI Pins
	input spis_clk,
	input spis_ncs,
	output spis_miso,
	input spis_mosi
);

wire [31:0] spis_dma_addr;
wire [15:0] spis_dma_len;
wire spis_dma_run;
wire spis_dma_done;
wire spis_dma_strobe;
wire [31:0] spis_dma_data;


// qpimem interface
wire qpi_do_read;
assign qpi_do_read = 0;
wire qpi_do_write;
reg qpi_next_word;
wire [23:0] qpi_addr;
reg [31:0] qpi_rdata;
wire [31:0] qpi_wdata;
reg qpi_is_idle;

spi_slave spis(
	.clk(clk),
	.reset(rst),

	// Bus - used to read and write registers
	.register_num(addr[4:2]),
	.data_in(wdata),
	.data_out(rdata),
	.wen(wen == 4'hf),
	.ren(ren),
	.ready(ready),

	// Interface to qpimem_iface
	.qpimem_iface_do_write(qpi_do_write),
	.qpimem_iface_next_word(qpi_next_word),
	.qpimem_iface_addr(qpi_addr),
	.qpimem_iface_wdata(qpi_wdata),
	.qpimem_iface_is_idle(qpi_is_idle),

	// Signals from outside pins
	.SCK(spis_clk),
	.MOSI(spis_mosi),
	.MISO(spis_miso),
	.CS(spis_ncs)
);


// Translate dma_writers 32 bit output to 24 bits
assign qpi_addr = dma_writer_mem_addr[23:0];
wire [31:0] dma_writer_mem_addr;


qpimem_iface qpimem_iface(
	.clk(clk),
	.rst(rst),
	
	.do_read(qpi_do_read),
	.do_write(qpi_do_write),
	.next_word(qpi_next_word),
	.addr(qpi_addr),
	.wdata(qpi_wdata),
	.rdata(qpi_rdata),
	.is_idle(qpi_is_idle),

	.spi_xfer_wdata(),
	.spi_xfer_rdata(),
	.do_spi_xfer(),
	.spi_xfer_claim(),
	.spi_xfer_idle(),

	.spi_clk(spi_clk),
	.spi_ncs(spi_ncs),
	.spi_selected(),
	.spi_sout(spi_sout),
	.spi_sin(spi_sin),
	.spi_bus_qpi(spi_bus_qpi),
	.spi_oe(spi_oe)
);


endmodule