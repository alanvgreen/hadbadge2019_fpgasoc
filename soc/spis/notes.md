# Notes

## Integrating To MISO and MOSI

Hard.

Possible if:
- Flash is not used concurrently
- Need a new signal to take over MISO MOSI and CLK
- When set, reverse MISO&MOSI
- Lock out flash???


## Verilog
Local test bench
- test will load stuff into PSRAM OK

Global test bench
- test takes over bus OK

## Design

SPI slave -> qpimem_arbiter -> qpimem_iface -> PSRAM

Slave has custom writer FIFO 