# HISTORICAL FALLBACK ONLY. Current communication uses PS UART0 MIO14/15.
# Do not add this file to the UART0 hardware build. Kept for old report provenance.
# Offline UART1 EMIO constraint for the user-confirmed Zynq7020 Navigator board.
# Design part: xc7z020clg400-2 (audited existing project target; full marking pending).
# Official core-board 2V5 schematic: BANK13 default VCCO=3.3V through 0R links.
# Actual board voltage has not been measured. This file does not authorize flashing.
# BOARD_READY remains 0; no bitstream or board acceptance is implied.
# RX: J4 pin3 -> core X4 pin88 -> U5 / IO_L19N_T3_VREF_13.
# TX: J4 pin4 -> core X4 pin90 -> T5 / IO_L19P_T3_13.
# The companion Tcl script checks exact wrapper ports before read_xdc.
# Keep XDC limited to commands supported by Vivado 2017.4's constraint reader.
set_property PACKAGE_PIN U5 [get_ports COMM_UART1_rxd]
set_property PACKAGE_PIN T5 [get_ports COMM_UART1_txd]
set_property IOSTANDARD LVCMOS33 [get_ports {COMM_UART1_rxd COMM_UART1_txd}]
# External UART is asynchronous. No invented I/O delay, UART clock or blanket
# false-path constraints are applied; report timing limitations explicitly.
