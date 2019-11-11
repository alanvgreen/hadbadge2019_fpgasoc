
verilator: verilator-build/Vspistest
	./verilator-build/Vspistest

SIM_SRC_V = spi_slave.v ../qpi_cache/qpimem_arbiter.v ../qpi_cache/qpimem_iface.v
SIM_SRC_V += ../sim/ecp5_io_sim.v verilator_top.v

verilator-build/Vspistest: $(SIM_SRC_V) verilator_main.cpp  ../psram_emu.cpp
	verilator -CFLAGS "-ggdb" --assert --trace --Mdir verilator-build -Wno-style -Wno-fatal -cc --top-module spistest --exe $^
	make OPT_FAST="-O2 -fno-stack-protector" -C verilator-build -f Vspistest.mk

clean:
	rm -rf ./verilator-build
