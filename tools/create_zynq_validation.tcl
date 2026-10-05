# Vivado 2017.4: independent, compile-only UART hardware export.
# Usage: vivado -mode batch -source tools/create_zynq_validation.tcl
# Optional first argument: a fresh output directory. Never opens the user's XPR.
set root [file normalize [file join [file dirname [info script]] ..]]
set source_bd [file join $root platforms zynq reference system_7020_ps_original.bd]
set out [file join $root build zynq_uart0_validation]
if {$argc > 0} { set out [file normalize [lindex $argv 0]] }
if {[file exists $out]} { error "Refusing an existing output directory: $out" }
if {![file isfile $source_bd]} { error "Missing audited reference BD: $source_bd" }
file mkdir $out
file mkdir [file join $out input]
set bd [file join $out input system.bd]
file copy $source_bd $bd
create_project dc_uart_validation [file join $out project] -part xc7z020clg400-2
add_files -norecurse $bd
open_bd_design $bd
set ps [get_bd_cells -quiet processing_system7_0]
if {[llength $ps] != 1} { error "Expected the audited PS7 cell" }
set_property -dict [list CONFIG.PCW_UART0_PERIPHERAL_ENABLE {1} \
    CONFIG.PCW_UART0_UART0_IO {MIO 14 .. 15} \
    CONFIG.PCW_UART1_PERIPHERAL_ENABLE {0}] $ps
validate_bd_design
save_bd_design
generate_target all [get_files $bd]
set wrapper [make_wrapper -files [get_files $bd] -top]
add_files -norecurse $wrapper
set_property top system_wrapper [current_fileset]
update_compile_order -fileset sources_1
# Synthesis/export verifies the hardware description. No implementation or
# bitstream is produced. UART0 uses PS MIO, so no PL UART XDC is needed.
launch_runs synth_1 -jobs 2
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {
    error "Synthesis did not finish: [get_property STATUS [get_runs synth_1]]"
}
open_run synth_1
write_hwdef -force -file [file join $out dc_uart_validation.hdf]
report_utilization -file [file join $out utilization.rpt]
set fp [open [file join $out validation.txt] w]
puts $fp "PART=xc7z020clg400-2"
puts $fp "UART0=MIO14/15 communication; UART1=disabled; route=uart0_mio14_15"
puts $fp "STATUS=HDF exported after synthesis; no board test, XDC, implementation or bitstream"
close $fp
close_project
