"""Rebuild relocated CubeMX base projects; preserve warnings and hashes for audit."""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parents[2]
UV4 = Path('C:/Users/Li/AppData/Local/Keil_v5/UV4/UV4.exe')
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = subprocess.SW_HIDE
evidence = SOURCE / 'evidence'
evidence.mkdir(exist_ok=True)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check_generated_route(role):
    """Inspect generated code, not only the IOC or a successful link."""
    folder = SOURCE / role
    uart = (folder / 'Src/usart.c').read_text()
    main = (folder / 'Src/main.c').read_text()
    errors = []
    expected = ['huart2.Init.BaudRate = 115200;', 'PA2     ------> USART2_TX',
                'PA3     ------> USART2_RX', 'hdma_usart2_rx.Init.Mode = DMA_CIRCULAR;']
    if role == 'ground_f407':
        expected += ['huart3.Init.BaudRate = 115200;', 'PB10     ------> USART3_TX',
                     'PB11     ------> USART3_RX', 'hdma_usart2_rx.Instance = DMA1_Stream5;',
                     'hdma_usart2_rx.Init.Channel = DMA_CHANNEL_4;',
                     'hdma_usart3_rx.Instance = DMA1_Stream1;',
                     'hdma_usart3_rx.Init.Channel = DMA_CHANNEL_4;',
                     'hdma_usart3_rx.Init.Mode = DMA_CIRCULAR;',
                     'GPIO_InitStruct.Alternate = GPIO_AF7_USART2;',
                     'GPIO_InitStruct.Alternate = GPIO_AF7_USART3;']
        if 'USART6' in uart: errors.append('Obsolete USART6 remains in generated UART implementation')
        gpio = (folder / 'Src/gpio.c').read_text()
        board = (folder / 'Inc/main.h').read_text()
        for token in ['HAL_GPIO_WritePin(PHY_RESET_N_GPIO_Port, PHY_RESET_N_Pin, GPIO_PIN_RESET);',
                      'HAL_GPIO_WritePin(PHY_MDC_HOLD_LOW_GPIO_Port, PHY_MDC_HOLD_LOW_Pin, GPIO_PIN_RESET);']:
            if token not in gpio: errors.append('Missing generated guard: ' + token)
        for token in ['#define PHY_RESET_N_Pin GPIO_PIN_3', '#define PHY_RESET_N_GPIO_Port GPIOD',
                      '#define PHY_MDC_HOLD_LOW_Pin GPIO_PIN_1', '#define PHY_MDC_HOLD_LOW_GPIO_Port GPIOC']:
            if token not in board: errors.append('Wrong PHY guard pin: ' + token)
        if not main.index('MX_GPIO_Init();') < main.index('MX_USART2_UART_Init();'):
            errors.append('PHY guard GPIO must initialize before USART2')
        for token in ['MX_ETH_Init();', 'MX_I2S', 'MX_DAC_Init();']:
            if token in main: errors.append('Conflicting peripheral initialization: ' + token)
        irq = (folder / 'Src/stm32f4xx_it.c').read_text()
        for token in ['void DMA1_Stream5_IRQHandler(void)', 'void DMA1_Stream1_IRQHandler(void)',
                      'void USART2_IRQHandler(void)', 'void USART3_IRQHandler(void)',
                      'HAL_DMA_IRQHandler(&hdma_usart2_rx);', 'HAL_UART_IRQHandler(&huart2);']:
            if token not in irq: errors.append('Missing generated IRQ binding: ' + token)
    else:
        expected.append('hdma_usart2_rx.Instance = DMA1_Channel6;')
    errors += ['Missing generated UART setting: ' + token for token in expected if token not in uart]
    return {'passed': not errors, 'errors': errors, 'uart_baud': 115200,
            'physical_waveform_tested': False}


result = {
    'tested_at': datetime.now(timezone.utc).isoformat(),
    'scope': 'CubeMX peripheral base projects only; main loop empty; communication app not integrated; board not tested',
    'compiler': 'Arm Compiler 6.22', 'projects': [],
}
for role in ['ground_f103', 'ground_f407']:
    project = SOURCE / role / 'MDK-ARM' / (role + '.uvprojx')
    output = ROOT / 'build/cubemx_delivery' / role
    output.mkdir(parents=True, exist_ok=True)
    axf, hexfile = output / (role + '.axf'), output / (role + '.hex')
    log = output / (role + '.log')
    for path in [axf, hexfile, log]:
        path.unlink(missing_ok=True)
    command = [str(UV4), '-r', str(project), '-o', str(log), '-j0']
    try:
        proc = subprocess.run(command, cwd=project.parent, startupinfo=startup,
                              capture_output=True, text=True, timeout=60)
        code = proc.returncode
    except subprocess.TimeoutExpired:
        code = -1
    text = log.read_text(errors='replace') if log.exists() else ''
    summary_lines = [line for line in text.splitlines() if ' Error(s), ' in line]
    summary = summary_lines[-1] if summary_lines else text[-1000:]
    counts = re.search(r'(\d+) Error\(s\), (\d+) Warning\(s\)', summary)
    errors, warnings = (int(counts[1]), int(counts[2])) if counts else (-1, -1)
    route = check_generated_route(role)
    linked = code in [0, 1] and errors == 0 and axf.is_file() and hexfile.is_file() and route['passed']
    status = 'passed_with_warnings' if linked and warnings else 'passed' if linked else 'failed'
    entry = {'role': role, 'command': command, 'returncode': code, 'summary': summary,
             'generated_route_check': route,
             'status': status, 'passed': linked, 'error_count': errors, 'warning_count': warnings,
             'project': project.relative_to(ROOT).as_posix(), 'project_sha256': digest(project),
             'axf': axf.relative_to(ROOT).as_posix(), 'hex': hexfile.relative_to(ROOT).as_posix()}
    for key, path in [('axf', axf), ('hex', hexfile)]:
        if path.exists():
            entry[key + '_sha256'] = digest(path)
            entry[key + '_bytes'] = path.stat().st_size
    final_log = evidence / (role + '_delivery_keil.log')
    if log.exists():
        final_log.write_bytes(log.read_bytes())
        entry['log'] = final_log.relative_to(ROOT).as_posix()
        entry['log_sha256'] = digest(final_log)
    entry['warnings'] = [line for line in text.splitlines() if ': warning:' in line]
    print(json.dumps(entry, ensure_ascii=False), flush=True)
    result['projects'].append(entry)
result['passed'] = all(item['passed'] for item in result['projects'])
result['source_sha256'] = {
    path.relative_to(ROOT).as_posix(): digest(path)
    for path in sorted(SOURCE.rglob('*'))
    if path.is_file() and 'evidence' not in path.relative_to(SOURCE).parts
    and path.suffix.lower() in ['.c', '.h', '.s', '.ioc', '.uvprojx', '.py', '.ps1']
}
(evidence / 'generated_keil_results.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
raise SystemExit(0 if result['passed'] else 1)
