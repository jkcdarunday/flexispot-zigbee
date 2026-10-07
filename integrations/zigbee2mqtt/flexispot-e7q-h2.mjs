import {deviceEndpoints, onOff, numeric} from 'zigbee-herdsman-converters/lib/modernExtend';

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
        }}),
        onOff({endpointNames: controls, powerOnBehavior: false,
            description: 'Momentary desk command: turn ON to press; automatically returns OFF'}),
        numeric({name: 'height', cluster: 'genAnalogInput', attribute: 'presentValue',
            endpointNames: ['height'], description: 'Last valid desk display height',
            unit: 'cm', access: 'STATE_GET', precision: 1,
            reporting: {min: 1, max: 60, change: 0.1}}),
    ],
};
