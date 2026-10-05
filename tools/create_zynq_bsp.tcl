# SDK 2017.4 XSCT: generate a real standalone BSP and compile/link the UART app.
# Usage: xsct tools/create_zynq_bsp.tcl [hardware.hdf] [fresh-workspace] [115200|460800]
set root [file normalize [file join [file dirname [info script]] ..]]
set hdf [file join $root build zynq_uart0_validation dc_uart_validation.hdf]
set workspace [file join $root build zynq_uart0_sdk_115200]
set baud 115200
if {$argc > 0} { set hdf [file normalize [lindex $argv 0]] }
if {$argc > 1} { set workspace [file normalize [lindex $argv 1]] }
if {$argc > 2} { set baud [lindex $argv 2] }
if {$baud ni {115200 460800}} { error "Unsupported UART test baud: $baud" }
if {![file isfile $hdf]} { error "Missing hardware export: $hdf" }
if {[file exists $workspace]} { error "Refusing existing SDK workspace: $workspace" }
set stage [file join ${workspace}_sources]
if {[file exists $stage]} { error "Refusing existing source stage: $stage" }
file mkdir $stage
foreach relative {
    include/dc_protocol.h include/dc_node.h include/dc_link_config.h src/dc_protocol.c src/dc_node.c
    platforms/common/dc_byte_ring.h platforms/common/dc_byte_ring.c
    platforms/zynq/dc_zynq_port.h platforms/zynq/dc_zynq_port.c
    platforms/zynq/main_hooks.c platforms/zynq/board_config.h
    platforms/zynq/standalone_main.c
    platforms/zynq/dc_zynq_log.h platforms/zynq/dc_zynq_log.c
} {
    file copy [file join $root $relative] [file join $stage [file tail $relative]]
}
setws $workspace
createhw -name dc_hw -hwspec $hdf
createbsp -name dc_bsp -hwproject dc_hw -proc ps7_cortexa9_0 -os standalone
# The protocol owns UART0 exclusively. SDK's generated no-device outbyte/inbyte
# are used; diagnostic text goes only to the application's bounded RAM log.
configbsp -bsp dc_bsp stdin none
configbsp -bsp dc_bsp stdout none
regenbsp -bsp dc_bsp
foreach app {dc_zynq_comm dc_zynq_comm_link_only_ready1} {
    createapp -name $app -app {Empty Application} -hwproject dc_hw \
        -proc ps7_cortexa9_0 -bsp dc_bsp -os standalone -lang c
    importsources -name $app -path $stage
    set flags "-std=c99 -Wall -Wextra -Werror -DDC_UART_BAUD=${baud}u"
    if {$app eq "dc_zynq_comm_link_only_ready1"} {
        append flags " -DDC_ZYNQ_BOARD_READY=1"
    }
    configapp -app $app -add compiler-misc $flags
}
projects -build
set elf [file join $workspace dc_zynq_comm Debug dc_zynq_comm.elf]
if {![file isfile $elf]} { error "Build returned without the expected ELF: $elf" }
set enabled_elf [file join $workspace dc_zynq_comm_link_only_ready1 Debug dc_zynq_comm_link_only_ready1.elf]
if {![file isfile $enabled_elf]} { error "Missing isolated ready1 link-only ELF: $enabled_elf" }
set fp [open [file join $workspace validation.txt] w]
puts $fp "ELF=$elf"
puts $fp "SOURCE_BSP=actual exported HDF; compile_fixture excluded"
puts $fp "ROUTE=uart0_mio14_15; BAUD=$baud; STDIN=none; STDOUT=none; LOG=RAM"
puts $fp "BOARD_READY=0; no board execution or final bitstream"
puts $fp "EXTRA_LINK_ONLY_READY1=$enabled_elf; never programmed or delivered as a flash image"
close $fp
exit
