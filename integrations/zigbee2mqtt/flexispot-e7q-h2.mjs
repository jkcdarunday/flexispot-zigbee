import {deviceEndpoints, onOff, numeric, enumLookup} from 'zigbee-herdsman-converters/lib/modernExtend';

const controls = ['stand', 'sit', 'preset_1', 'preset_2', 'up', 'down', 'memory', 'release'];
export default {
    zigbeeModel: ['Flexispot-E7Q-H2'],
    model: 'Flexispot-E7Q-H2',
    vendor: 'JKCD',
    description: 'Flexispot E7Q desk bridge on ESP32-H2',
    extend: [
        deviceEndpoints({endpoints: {
            stand: 1, sit: 2, preset_1: 3, preset_2: 4,
            up: 5, down: 6, memory: 7, release: 8, height: 9,
            diagnostics: 10,
        }}),
        onOff({endpointNames: controls, powerOnBehavior: false,
            description: 'Momentary desk command: turn ON to press; automatically returns OFF'}),
        numeric({name: 'height', cluster: 'genAnalogInput', attribute: 'presentValue',
            endpointNames: ['height'], description: 'Last valid desk display height',
            unit: 'cm', access: 'STATE_GET', precision: 1,
            reporting: {min: 1, max: 60, change: 0.1}}),
        numeric({name: 'uptime', cluster: 'genAnalogInput', attribute: 'presentValue',
            endpointNames: ['diagnostics'], entityCategory: 'diagnostic', description: 'Seconds since the ESP32 booted',
            unit: 's', access: 'STATE_GET', precision: 0,
            reporting: {min: 1, max: 30, change: 30}}),
        numeric({name: 'boot_count', cluster: 'genAnalogInput', attribute: {ID: 0xf000, type: 0x23},
            endpointNames: ['diagnostics'], entityCategory: 'diagnostic', description: 'Persistent boot count; resets after a full flash erase',
            access: 'STATE_GET', precision: 0, reporting: {min: 1, max: 30, change: 1}}),
        enumLookup({name: 'reset_reason', cluster: 'genAnalogInput', attribute: {ID: 0xf001, type: 0x30},
            endpointName: 'diagnostics', entityCategory: 'diagnostic', description: 'Reason recorded by the ESP32 for its last restart',
            access: 'STATE_GET', lookup: {unknown: 0, power_on: 1, external_reset: 2,
                software: 3, panic: 4, interrupt_watchdog: 5, task_watchdog: 6,
                watchdog: 7, deep_sleep: 8, brownout: 9, sdio: 10, usb: 11,
                jtag: 12, efuse: 13, power_glitch: 14, cpu_lockup: 15},
            reporting: {min: 1, max: 30, change: 1}}),
    ],
};
