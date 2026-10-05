"""Historical UART1 EMIO pin/place/route collector, not the current UART0 build.

Requires the old EMIO workspace and its matching board profile/source snapshot.
Restore the previous 7020 review package for reproduction. Never use this report
as evidence for the active PS MIO14/15 route; see zynq_sdk_validation.json.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def relative(path):
    return path.relative_to(ROOT).as_posix()


def number(text, label):
    match = re.search(re.escape(label) + r'\.+\s*:\s*(\d+)', text)
    return int(match[1]) if match else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-dir', type=Path, required=True)
    parser.add_argument('--launch-log', type=Path)
    args = parser.parse_args()
    run = args.run_dir.resolve()
    log = args.launch_log.resolve() if args.launch_log else run.with_name(run.name + '_launch.log')
    records = []
    for line in (run / 'status.tsv').read_text(encoding='utf-8').splitlines():
        if '\t' in line:
            records.append(line.split('\t', 1))
    state = dict(records)
    errors = []
    if state.get('passed') != 'true':
        errors.append('Tcl run failed: ' + state.get('failure', 'no passed=true marker'))
    text = log.read_text(encoding='utf-8', errors='replace')
    messages = {key: [line for line in text.splitlines() if line.startswith(prefix)]
                for key, prefix in [('warning', 'WARNING:'), ('critical_warning', 'CRITICAL WARNING:'), ('error', 'ERROR:')]}
    if messages['error']:
        errors.append('Vivado error messages are present')
    # The checkpoint-only audit emits expected temporary black-box diagnostics
    # and an OOC-source recommendation. Preserve those verbatim; reject others.
    expected_critical_ids = ['[Project 1-486]', '[Vivado 12-5469]']
    unexpected_critical = [line for line in messages['critical_warning']
                           if not any(value in line for value in expected_critical_ids)]
    if unexpected_critical:
        errors.append('Unexpected critical warnings are present')
    drc = (run / 'drc.rpt').read_text(encoding='utf-8', errors='replace')
    route = (run / 'route_status.rpt').read_text(encoding='utf-8', errors='replace')
    io = (run / 'io.rpt').read_text(encoding='utf-8', errors='replace')
    timing = (run / 'timing_summary.rpt').read_text(encoding='utf-8', errors='replace')
    routable = number(route, '# of routable nets')
    fully_routed = number(route, '# of fully routed nets')
    routing_errors = number(route, '# of nets with routing errors')
    if routable is None or routable != fully_routed or routing_errors != 0:
        errors.append('Routing is incomplete or route-status evidence cannot be parsed')
    if 'Violations found: 0' not in drc or state.get('drc_blocking_count') != '0':
        errors.append('Final DRC did not establish zero violations')
    if state.get('top_black_boxes_after') != '0':
        errors.append('OOC checkpoint stitching did not resolve all black boxes')
    ports = []
    for name, pin, direction, function in [
        ('COMM_UART1_rxd', 'U5', 'IN', 'IO_L19N_T3_VREF_13'),
        ('COMM_UART1_txd', 'T5', 'OUT', 'IO_L19P_T3_13'),
    ]:
        row = next((line for line in io.splitlines() if ('| ' + pin + ' ') in line and name in line), '')
        correct = (state.get('pin_' + pin + '_BANK') == '13'
                   and state.get('pin_' + pin + '_PIN_FUNC') == function
                   and state.get('pin_' + pin + '_IS_BONDED') == '1'
                   and state.get('pin_' + pin + '_IS_GENERAL_PURPOSE') == '1'
                   and state.get('port_' + name + '_PACKAGE_PIN') == pin
                   and state.get('port_' + name + '_DIRECTION') == direction
                   and state.get('port_' + name + '_IOSTANDARD') == 'LVCMOS33'
                   and 'LVCMOS33' in row and 'FIXED' in row)
        if not correct:
            errors.append('Pin/property/report IO evidence differs for ' + name)
        ports.append({'port': name, 'direction': direction, 'package_pin': pin,
                      'bank': 13, 'pin_function': function, 'bonded': True,
                      'general_purpose': True, 'iostandard': 'LVCMOS33',
                      'io_report_row': row, 'device_pincheck_passed': correct})
    checkpoints = []
    base = ROOT / 'build/zynq_validation'
    for original, copied in [
        (base / 'project/dc_uart_validation.runs/synth_1/system_wrapper.dcp', 'system_wrapper.dcp'),
        (base / 'input/ip/system_processing_system7_0_0/system_processing_system7_0_0.dcp', 'system_processing_system7_0_0.dcp'),
        (base / 'input/ip/system_rst_ps7_0_100M_0/system_rst_ps7_0_100M_0.dcp', 'system_rst_ps7_0_100M_0.dcp'),
    ]:
        copy = run / 'input' / copied
        matches = sha(original) == sha(copy)
        if not matches:
            errors.append('Copied checkpoint differs: ' + copied)
        checkpoints.append({'source': relative(original), 'copy': relative(copy),
                            'source_sha256': sha(original), 'copy_sha256': sha(copy),
                            'bytes': original.stat().st_size, 'copy_matches': matches})
    bitstreams = list(run.rglob('*.bit'))
    if bitstreams:
        errors.append('Unexpected bitstream in pincheck output')
    sources = ['tools/check_zynq_7020_pins.tcl', 'tools/collect_zynq_7020_pincheck.py',
               'platforms/zynq/navigator_7020_uart1.xdc', 'platforms/zynq/board_profile_7020.json',
               'platforms/zynq/reference/system_7020_ps_original.bd']
    evidence = ROOT / 'reports/zynq_7020_pincheck_evidence'
    evidence.mkdir(parents=True, exist_ok=True)
    for path in [log, *(run / name for name in ['status.tsv', 'drc.rpt', 'io.rpt',
                   'route_status.rpt', 'timing_summary.rpt', 'check_timing.rpt', 'utilization.rpt',
                   'U5_package_properties.rpt', 'T5_package_properties.rpt'])]:
        shutil.copy2(path, evidence / path.name)
    # Keep the initial tool-script diagnostic separate from the final successful run.
    earlier = ROOT / 'build/zynq_7020_pincheck_launch.log'
    if earlier.exists():
        shutil.copy2(earlier, evidence / 'initial_redirect_unavailable.log')
    retry1 = ROOT / 'build/zynq_7020_pincheck_retry1_launch.log'
    if retry1.exists():
        shutil.copy2(retry1, evidence / 'intermediate_xdc_guard_warning.log')
    routed = run / 'uart1_routed.dcp'
    profile = json.loads((ROOT / 'platforms/zynq/board_profile_7020.json').read_text(encoding='utf-8'))
    result = {
        'schema': 1, 'checked_at': datetime.now(timezone.utc).isoformat(),
        'passed': not errors, 'errors': errors,
        'scope': 'Independent copied-checkpoint opt/place/route and IO/package DRC for PS UART1 EMIO; no communication timing signoff',
        'source_sha256': {name: sha(ROOT / name) for name in sources},
        'vivado_version': state.get('tool_version'), 'part': state.get('part'),
        'model7020_user_confirmed': True, 'vendor_ordering_code': profile.get('vendor_standard_ordering_code'),
        'physical_speed_grade_read': False, 'board_ready': False, 'board_tested': False,
        'actual_bank13_voltage_measured': False, 'bank13_design_default_volts': 3.3,
        'vcco_basis': profile['uart1']['vcco_documentation'],
        'run_dir': relative(run), 'input_checkpoints': checkpoints,
        'checkpoint_source_sha256': {item['source']: item['source_sha256'] for item in checkpoints},
        'stitching': {'black_boxes_before': int(state.get('top_black_boxes_before', '-1')),
                      'black_boxes_after': int(state.get('top_black_boxes_after', '-1')),
                      'ps_ip_checkpoint_stitched': state.get('ps_checkpoint_stitched') == 'true',
                      'reset_ip_checkpoint_stitched': state.get('reset_checkpoint_stitched') == 'true'},
        'ports': ports,
        'implementation': {'steps': [value for key, value in records if key == 'step'],
                           'routable_nets': routable, 'fully_routed_nets': fully_routed,
                           'nets_with_routing_errors': routing_errors,
                           'drc_violations': int(state.get('drc_violation_count', '-1')),
                           'drc_blocking': int(state.get('drc_blocking_count', '-1'))},
        'timing': {'clocks': state.get('clocks', '').split(),
                   'no_user_timing_constraints': 'There are no user specified timing constraints.' in timing,
                   'wns_ns': None, 'tns_ns': None, 'communication_timing_signoff': False,
                   'note': 'WNS/TNS are NA and clock list is empty. PS UART is asynchronous; no fictitious external clock, input/output_delay or blanket false path is added. This implementation does not validate UART baud/timing on hardware.'},
        'vivado_messages': {'warning_count': len(messages['warning']),
                            'critical_warning_count': len(messages['critical_warning']),
                            'error_count': len(messages['error']),
                            'warning_lines': messages['warning'],
                            'critical_warning_lines': messages['critical_warning'],
                            'unexpected_critical_warnings': unexpected_critical,
                            'note': 'Two temporary top-level black boxes are resolved by copied OOC checkpoints. Two Vivado 12-5469 recommendations to use original XCI are retained. Final black-box count and routed DRC are zero.'},
        'routed_checkpoint': {'path': relative(routed), 'sha256': sha(routed),
                              'bytes': routed.stat().st_size, 'included_in_evidence': False},
        'bitstream_generated': bool(bitstreams), 'original_fpga_project_opened': False,
        'original_validation_and_sdk_workspaces_modified': False,
        'limitations': ['Physical BANK13 VCCO and header orientation remain board bring-up checks.',
                        'No bitstream generated, no flashing or board serial test.',
                        'No camera capture, video hardware or YOLO-on-PL completion is claimed.',
                        'Independent checkpoint integration is an offline pin/place/route audit; original validation HDF/BSP/ELF is unchanged.'],
        'evidence_sha256': {relative(path): sha(path) for path in sorted(evidence.iterdir()) if path.is_file()},
    }
    target = ROOT / 'reports/zynq_7020_pincheck.json'
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'passed': result['passed'], 'report': str(target),
                      'drc_violations': result['implementation']['drc_violations'],
                      'routed_nets': fully_routed, 'critical_warnings': len(messages['critical_warning']),
                      'errors': errors}, ensure_ascii=False, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
