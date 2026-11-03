// Run from a directory with zigbee-herdsman-converters installed; pass the
// converter's path as argv[2]. CI copies both files into an isolated test folder.
import assert from 'node:assert/strict';
import {pathToFileURL} from 'node:url';
const {default: definition} = await import(pathToFileURL(process.argv[2]));
const [mapping, controls, height, uptime, bootCount, resetReason] = definition.extend;
assert.equal(definition.zigbeeModel[0], 'Flexispot-E7Q-H2');
const endpoints = mapping.endpoint();
assert.equal(endpoints.release, 8);
assert.equal(endpoints.height, 9);
assert.equal(controls.exposes.length, 8);
assert.equal(controls.exposes[4].features[0].property, 'state_up');
assert.equal(height.exposes[0].property, 'height_height');
assert.equal(height.exposes[0].unit, 'cm');
assert.equal(height.exposes[0].access, 5);  // STATE_GET only; height cannot be written

// Check real converter routing from an Analog Input report and outbound command.
const heightMessage = {endpoint: {ID: 9}, data: {presentValue: 103.25}};
const heightResult = height.fromZigbee[0].convert(
    {endpoint: () => endpoints, meta: {multiEndpoint: true}}, heightMessage,
    () => {}, {}, {mapped: {endpoint: () => endpoints, meta: {multiEndpoint: true}}, device: {}});
assert.equal(heightResult.height_height, 103.3);
let sent;
await controls.toZigbee[0].convertSet(
    {command: async (...args) => { sent = args; }}, 'state', 'ON',
    {message: {state: 'ON'}, mapped: {}, options: {}});
assert.equal(sent[0], 'genOnOff');
assert.equal(sent[1], 'on');
// Diagnostic reports are read-only and use the correct endpoint and value type.
for (const [extension, name, attribute, value, expected] of [
    [uptime, 'uptime', 'presentValue', 1234, 1234],
    [bootCount, 'boot_count', 0xf000, 7, 7],
    [resetReason, 'reset_reason', 0xf001, 9, 'brownout'],
]) {
    assert.equal(extension.exposes[0].access, 5);
    const result = extension.fromZigbee[0].convert(
        {endpoint: () => endpoints, meta: {multiEndpoint: true}},
        {endpoint: {ID: endpoints.diagnostics}, data: {[attribute]: value}},
        () => {}, {}, {mapped: {endpoint: () => endpoints, meta: {multiEndpoint: true}}, device: {}});
    assert.equal(result[`${name}_diagnostics`], expected);
}
assert.equal(uptime.exposes[0].unit, 's');
// A height report must never be decoded as a reboot reason.
assert.equal(resetReason.fromZigbee[0].convert(
    {endpoint: () => endpoints, meta: {multiEndpoint: true}}, heightMessage,
    () => {}, {}, {mapped: {endpoint: () => endpoints, meta: {multiEndpoint: true}}, device: {}}), undefined);
console.log('Zigbee2MQTT controls, height, uptime, boot count and reset-reason tests passed.');
