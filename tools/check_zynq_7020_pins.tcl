# HISTORICAL UART1 EMIO ONLY. Not part of the active UART0 MIO14/15 build.
# Requires the previous EMIO checkpoints in build/zynq_validation. The current
# create_zynq_validation.tcl creates a different MIO design and cannot supply
# these inputs. Restore the previous 7020 review package to reproduce EMIO.
# Vivado 2017.4: independent checkpoint-based UART pin/implementation check.
# Uses copied synthesized checkpoints, never opens the original user's XPR.
# Usage: vivado -mode batch -source tools/check_zynq_7020_pins.tcl
# Optional arg0: new output directory. No bitstream, hardware programming or SDK changes.
set root [file normalize [file join [file dirname [info script]] ..]]
set base [file join $root build zynq_validation]
set out [file join $root build zynq_7020_pincheck]
if {$argc > 0} { set out [file normalize [lindex $argv 0]] }
if {[file exists $out]} { error "Refusing an existing output directory: $out" }
set top_src [file join $base project dc_uart_validation.runs synth_1 system_wrapper.dcp]
set ps_src [file join $base input ip system_processing_system7_0_0 system_processing_system7_0_0.dcp]
set reset_src [file join $base input ip system_rst_ps7_0_100M_0 system_rst_ps7_0_100M_0.dcp]
set xdc_src [file join $root platforms zynq navigator_7020_uart1.xdc]
foreach src [list $top_src $ps_src $reset_src $xdc_src] {
    if {![file isfile $src]} { error "Required source not found: $src" }
}
file mkdir $out
set inputs [file join $out input]
file mkdir $inputs
set top [file join $inputs system_wrapper.dcp]
set ps [file join $inputs system_processing_system7_0_0.dcp]
set reset [file join $inputs system_rst_ps7_0_100M_0.dcp]
set xdc [file join $inputs navigator_7020_uart1.xdc]
foreach src [list $top_src $ps_src $reset_src $xdc_src] dest [list $top $ps $reset $xdc] {
    file copy $src $dest
}
set status [open [file join $out status.tsv] w]
proc record {key value} {
    global status
    puts $status "$key\t$value"
    flush $status
}
proc dump_properties {object path} {
    set fp [open $path w]
    foreach property [lsort [list_property $object]] {
        puts $fp "$property=[get_property $property $object]"
    }
    close $fp
}
record tool_version [version -short]
record model7020_user_confirmed true
record actual_vcco_measured false
record default_vcco_source "official core-board 2V5 schematic page 7; BANK13 default 3.3V"
record board_ready false
record bitstream_generated false
record board_tested false
record step copying_complete
set failure [catch {
    open_checkpoint $top
    record part [get_property PART [current_project]]
    if {[get_property PART [current_project]] ne "xc7z020clg400-2"} {
        error "Expected audited xc7z020clg400-2 target"
    }
    record top_black_boxes_before [llength [get_cells -quiet -hierarchical -filter {IS_BLACKBOX == 1}]]
    foreach cell [get_cells -quiet -hierarchical -filter {IS_BLACKBOX == 1}] {
        record black_box_before "[get_property NAME $cell]|[get_property REF_NAME $cell]"
    }
    # Top synth checkpoint intentionally leaves OOC IP black boxes. Stitch
    # matching copied IP DCPs before implementation; no original project is opened.
    set ps_cell [get_cells -quiet system_i/processing_system7_0]
    if {[llength $ps_cell] != 1} { error "Expected PS7 hierarchy cell" }
    if {[get_property IS_BLACKBOX $ps_cell]} {
        read_checkpoint -cell system_i/processing_system7_0 $ps
        record ps_checkpoint_stitched true
    } else { record ps_checkpoint_stitched false }
    set reset_cell [get_cells -quiet system_i/rst_ps7_0_100M]
    if {[llength $reset_cell] == 1 && [get_property IS_BLACKBOX $reset_cell]} {
        read_checkpoint -cell system_i/rst_ps7_0_100M $reset
        record reset_checkpoint_stitched true
    } else { record reset_checkpoint_stitched false }
    set boxes [get_cells -quiet -hierarchical -filter {IS_BLACKBOX == 1}]
    record top_black_boxes_after [llength $boxes]
    if {[llength $boxes] != 0} { error "Remaining black boxes: $boxes" }
    foreach pin_name {U5 T5} {
        set pin [get_package_pins -quiet $pin_name]
        if {[llength $pin] != 1} { error "Package pin unavailable: $pin_name" }
        dump_properties $pin [file join $out ${pin_name}_package_properties.rpt]
        foreach property {NAME BANK PIN_FUNC SITE IS_BONDED IS_GENERAL_PURPOSE} {
            if {[lsearch -exact [list_property $pin] $property] >= 0} {
                record pin_${pin_name}_${property} [get_property $property $pin]
            }
        }
        if {[get_property BANK $pin] != 13} { error "$pin_name is not in BANK13" }
        if {![get_property IS_BONDED $pin] || ![get_property IS_GENERAL_PURPOSE $pin]} {
            error "$pin_name is not a bonded general-purpose IO"
        }
    }
    if {[llength [get_ports -quiet COMM_UART1_rxd]] != 1 ||
        [llength [get_ports -quiet COMM_UART1_txd]] != 1} {
        error "Expected generated COMM_UART1_rxd/txd wrapper ports"
    }
    read_xdc $xdc
    foreach name {COMM_UART1_rxd COMM_UART1_txd} {
        set port [get_ports -quiet $name]
        record port_${name}_DIRECTION [get_property DIRECTION $port]
        record port_${name}_PACKAGE_PIN [get_property PACKAGE_PIN $port]
        record port_${name}_IOSTANDARD [get_property IOSTANDARD $port]
        if {[get_property IOSTANDARD $port] ne "LVCMOS33"} { error "$name IOSTANDARD differs" }
    }
    if {[get_property DIRECTION [get_ports COMM_UART1_rxd]] ne "IN" ||
        [get_property DIRECTION [get_ports COMM_UART1_txd]] ne "OUT"} {
        error "UART1 direction mismatch"
    }
    record step constraints_loaded
    opt_design
    record step opt_completed
    place_design
    record step place_completed
    route_design
    record step route_completed
    write_checkpoint [file join $out uart1_routed.dcp]
    report_drc -file [file join $out drc.rpt]
    report_timing_summary -report_unconstrained -file [file join $out timing_summary.rpt]
    check_timing -verbose -file [file join $out check_timing.rpt]
    report_io -file [file join $out io.rpt]
    report_route_status -file [file join $out route_status.rpt]
    report_utilization -file [file join $out utilization.rpt]
    record clocks [get_clocks -quiet]
    set drc_count 0
    set drc_blocking 0
    foreach violation [get_drc_violations -quiet] {
        incr drc_count
        set severity [get_property SEVERITY $violation]
        record drc_violation "[get_property NAME $violation]|$severity|[get_property DESCRIPTION $violation]"
        if {$severity eq "Error" || $severity eq "Critical Warning"} { incr drc_blocking }
    }
    record drc_violation_count $drc_count
    record drc_blocking_count $drc_blocking
    if {$drc_blocking > 0} { error "Blocking DRC violations: $drc_blocking" }
    record passed true
    record step reports_completed
} message options]
if {$failure} {
    record passed false
    record failure $message
    if {[dict exists $options -errorinfo]} {
        set errorfile [open [file join $out error_info.txt] w]
        puts $errorfile [dict get $options -errorinfo]
        close $errorfile
    }
}
close $status
catch {close_design}
catch {close_project}
if {$failure} {
    puts stderr "Independent UART1 pin/implementation check failed: $message"
    exit 1
}
