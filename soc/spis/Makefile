
verilator: verilator-build/Vspistest
	./verilator-build/Vspistest

verilator-build/Vspistest: spi_slave.v ../qpi_cache/qpimem_iface.v verilator_top.v verilator_main.cpp ../sim/ecp5_io_sim.v ../psram_emu.cpp
	verilator -CFLAGS "-ggdb" --assert --trace --Mdir verilator-build -Wno-style -Wno-fatal -cc --top-module spistest --exe $^
	make OPT_FAST="-O2 -fno-stack-protector" -C verilator-build -f Vspistest.mk

clean:
	rm -rf ./verilator-build
