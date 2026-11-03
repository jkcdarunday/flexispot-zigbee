"""Check native uptime discovery and decoding of read-only restart diagnostics."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import MagicMock

import zigpy.device
import zigpy.types as t
from zigpy.profiles import zha
from zigpy.zcl.clusters.general import AnalogInput, Basic, Identify
from zha.application import EntityPlatform, EntityType, Platform
from zha.application.discovery import discover_entities_for_endpoint
from zha.application.platforms.number.bacnet import BACNET_UNITS_TO_HA_UNITS
from zha.application.platforms.sensor import EnumSensor
from zha.application.platforms.sensor.const import SensorDeviceClass, SensorStateClass

handler = Path(__file__).resolve().parents[1] / "integrations/zha/flexispot_e7q_h2.py"
spec = importlib.util.spec_from_file_location("diagnostics_handler", handler)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
definition = module.QUIRK.zha_device_factory.quirk_definition
metadata = {item.attribute_name: item for item in definition.entity_metadata if item.endpoint_id == 10}
assert set(metadata) == {"present_value", "boot_count", "reset_reason"}
assert all(item.entity_platform == EntityPlatform.SENSOR for item in metadata.values())
assert all(item.entity_type == EntityType.DIAGNOSTIC for item in metadata.values())
assert all(not item.initially_disabled for item in metadata.values())
# Explicit reporting is required: ZHA only binds custom sensor clusters when
# reporting_config is present. Otherwise unsolicited firmware reports are lost.
assert all(item.reporting_config is not None for item in metadata.values())
assert all(item.reporting_config.min_interval == 30 for item in metadata.values())
assert all(item.reporting_config.max_interval == 60 for item in metadata.values())
assert metadata["present_value"].unit == "s"
assert metadata["present_value"].device_class == SensorDeviceClass.DURATION
assert metadata["present_value"].state_class == SensorStateClass.MEASUREMENT
assert metadata["boot_count"].state_class == SensorStateClass.TOTAL_INCREASING
assert metadata["reset_reason"].enum is module.ResetReason

app = MagicMock()
raw = zigpy.device.Device(app, t.EUI64.convert("74:4d:bd:ff:fe:64:10:6a"), 0x1234)
ep = raw.add_endpoint(10)
ep.profile_id = zha.PROFILE_ID
ep.device_type = zha.DeviceType.SIMPLE_SENSOR
for cluster in [Basic, Identify]:
    ep.add_input_cluster(cluster.cluster_id)
analog = ep.add_input_cluster(AnalogInput.cluster_id)
analog.update_attribute(AnalogInput.AttributeDefs.description.id, "Uptime")
analog.update_attribute(AnalogInput.AttributeDefs.engineering_units.id, 73)
analog.update_attribute(AnalogInput.AttributeDefs.resolution.id, 1.0)
device = SimpleNamespace(
    ieee=raw.ieee, manufacturer="JKCD", model="Flexispot-E7Q-H2", exposes_features=set(),
    gateway=SimpleNamespace(config=SimpleNamespace(config=SimpleNamespace(device_overrides={}))),
)
entities = list(discover_entities_for_endpoint(SimpleNamespace(device=device, id=10, zigpy_endpoint=ep)))
assert len([entity for entity in entities if entity.PLATFORM == Platform.SENSOR]) == 1
assert all(entity.PLATFORM != Platform.LIGHT for entity in entities)
assert BACNET_UNITS_TO_HA_UNITS[73] == "s"

# Custom fields preserve the actual uint32/enum8 wire values. Counts greater
# than the float32 exact-integer limit must not lose a reboot.
# Exercise the real quirk transforms and preserve native cached uptime metadata.
for transform in module.QUIRK.zigpy_transforms:
    raw = transform(raw)
cluster = raw.endpoints[10].in_clusters[AnalogInput.cluster_id]
assert isinstance(cluster, module.DiagnosticAnalogInput)
assert cluster.get("description") == "Uptime"
assert cluster.get("engineering_units") == 73
count, trailing = t.uint32_t.deserialize(t.uint32_t(16777217).serialize())
assert not trailing
cluster.update_attribute(0xf000, count)
assert cluster.get("boot_count") == 16777217
assert metadata["boot_count"].attribute_converter(cluster.get("boot_count")) == 16777217
assert metadata["boot_count"].attribute_converter(0) is None
assert module.DiagnosticAnalogInput.AttributeDefs.boot_count.type is t.uint32_t
enum_sensor = SimpleNamespace(_enum=module.ResetReason)
for reason in module.ResetReason:
    value, trailing = t.enum8.deserialize(t.enum8(reason.value).serialize())
    assert not trailing
    cluster.update_attribute(0xf001, value)
    assert EnumSensor.formatter(enum_sensor, cluster.get("reset_reason")) == reason.name
assert EnumSensor.formatter(enum_sensor, 9) == "brownout"
assert EnumSensor.formatter(enum_sensor, 6) == "task_watchdog"
print("Native uptime discovery, exact uint32 boot count, unknown handling, and reset-reason decoding passed.")
