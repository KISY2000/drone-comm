"""Set explicit board values in CubeMX-written IOC; then reload and regenerate with CubeMX."""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def load(name):
    path = ROOT / name / (name + '.ioc')
    return path, dict(line.split('=', 1) for line in path.read_text().splitlines()
                      if '=' in line and not line.startswith('#'))


def add_dma(cfg, requests):
    cfg['Dma.RequestsNb'] = str(len(requests))
    for index, (uart, instance, channel) in enumerate(requests):
        cfg[f'Dma.Request{index}'] = uart + '_RX'
        params = {
            'Instance': instance, 'Direction': 'DMA_PERIPH_TO_MEMORY',
            'PeriphInc': 'DMA_PINC_DISABLE', 'MemInc': 'DMA_MINC_ENABLE',
            'PeriphDataAlignment': 'DMA_PDATAALIGN_BYTE',
            'MemDataAlignment': 'DMA_MDATAALIGN_BYTE', 'Mode': 'DMA_CIRCULAR',
            'Priority': 'DMA_PRIORITY_HIGH',
        }
        if channel:
            params['Channel'] = channel
            params['FIFOMode'] = 'DMA_FIFOMODE_DISABLE'
        prefix = f'Dma.{uart}_RX.{index}.'
        for key, value in params.items():
            cfg[prefix + key] = value
        cfg[prefix + 'RequestParameters'] = ','.join(params)


def configure(name, rcc, requests, interrupts):
    path, cfg = load(name)
    ips = sorted(set([cfg.pop(k) for k in list(cfg) if k.startswith('Mcu.IP') and k != 'Mcu.IPNb']) | {'DMA'})
    cfg['Mcu.IPNb'] = str(len(ips))
    for i, ip in enumerate(ips):
        cfg[f'Mcu.IP{i}'] = ip
    add_dma(cfg, requests)
    for key, value in rcc.items():
        cfg['RCC.' + key] = str(value)
    cfg['RCC.IPParameters'] = ','.join(sorted(k[4:] for k in cfg if k.startswith('RCC.') and k != 'RCC.IPParameters'))
    for irq in interrupts:
        cfg['NVIC.' + irq] = r'true\:5\:0\:false\:false\:true\:true\:true\:true'
    cfg['ProjectManager.StackSize'] = '0x1000'
    cfg['ProjectManager.HeapSize'] = '0x200'
    cfg['ProjectManager.UnderRoot'] = 'true'
    cfg['ProjectManager.LibraryCopy'] = '1'
    path.write_text('#MicroXplorer Configuration settings - do not modify\n' +
                    '\n'.join(k + '=' + v for k, v in sorted(cfg.items())) + '\n')


configure('ground_f103', {
    'HSE_VALUE': 8000000, 'PLLSourceVirtual': 'RCC_PLLSOURCE_HSE',
    'HSEDivPLL': 'RCC_HSE_PREDIV_DIV1', 'PLLMUL': 'RCC_PLL_MUL9',
    'SYSCLKSource': 'RCC_SYSCLKSOURCE_PLLCLK',
    'AHBCLKDivider': 'RCC_SYSCLK_DIV1', 'APB1CLKDivider': 'RCC_HCLK_DIV2',
    'APB2CLKDivider': 'RCC_HCLK_DIV1', 'ADCPresc': 'RCC_ADCPCLK2_DIV6',
    'SYSCLKFreq_VALUE': 72000000, 'AHBFreq_Value': 72000000,
    'CortexFreq_Value': 72000000, 'APB1Freq_Value': 36000000,
    'APB2Freq_Value': 72000000, 'APB1TimFreq_Value': 72000000,
    'APB2TimFreq_Value': 72000000, 'PLLCLKFreq_Value': 72000000,
    'TimSysFreq_Value': 72000000,
}, [('USART2', 'DMA1_Channel6', None)],
   ['DMA1_Channel6_IRQn', 'USART2_IRQn', 'EXTI0_IRQn'])

configure('ground_f407', {
    'HSE_VALUE': 8000000, 'PLLSourceVirtual': 'RCC_PLLSOURCE_HSE',
    'PLLM': 8, 'PLLN': 336, 'PLLP': 'RCC_PLLP_DIV2', 'PLLQ': 7,
    'SYSCLKSource': 'RCC_SYSCLKSOURCE_PLLCLK',
    'AHBCLKDivider': 'RCC_SYSCLK_DIV1', 'APB1CLKDivider': 'RCC_HCLK_DIV4',
    'APB2CLKDivider': 'RCC_HCLK_DIV2',
    'SYSCLKFreq_VALUE': 168000000, 'AHBFreq_Value': 168000000,
    'CortexFreq_Value': 168000000, 'APB1Freq_Value': 42000000,
    'APB2Freq_Value': 84000000, 'APB1TimFreq_Value': 84000000,
    'APB2TimFreq_Value': 168000000, 'PLLCLKFreq_Value': 168000000,
    'VCOInputFreq_Value': 1000000, 'VCOOutputFreq_Value': 336000000,
    'PLLQCLKFreq_Value': 48000000,
}, [('USART3', 'DMA1_Stream1', 'DMA_CHANNEL_4'),
    ('USART2', 'DMA1_Stream5', 'DMA_CHANNEL_4')],
   ['DMA1_Stream1_IRQn', 'DMA1_Stream5_IRQn', 'USART3_IRQn', 'USART2_IRQn'])
