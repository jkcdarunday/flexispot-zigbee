"""Verify generic On/Off Output endpoints become switches without any quirk."""

from types import SimpleNamespace
from unittest.mock import MagicMock

import zigpy.device
import zigpy.types as t
from zigpy.profiles import zha
from zigpy.zcl.clusters.general import Basic, Groups, Identify, OnOff, Scenes
from zha.application import Platform
from zha.application.discovery import discover_entities_for_endpoint

app = MagicMock()
zigpy_device = zigpy.device.Device(app, t.EUI64.convert("74:4d:bd:ff:fe:64:10:6a"), 0x1234)
device = SimpleNamespace(
    ieee=zigpy_device.ieee,
    manufacturer="JKCD",
    model="Flexispot-E7Q-H2",
    exposes_features=set(),
    gateway=SimpleNamespace(config=SimpleNamespace(config=SimpleNamespace(device_overrides={}))),
)

for endpoint_id in range(1, 9):
    endpoint = zigpy_device.add_endpoint(endpoint_id)
    endpoint.profile_id = zha.PROFILE_ID
    endpoint.device_type = zha.DeviceType.ON_OFF_OUTPUT  # 0x0002, matches firmware
    for cluster in (Basic, Identify, Groups, Scenes, OnOff):
        endpoint.add_input_cluster(cluster.cluster_id)
    zha_endpoint = SimpleNamespace(device=device, id=endpoint_id, zigpy_endpoint=endpoint)
    entities = list(discover_entities_for_endpoint(zha_endpoint))
    controls = [entity for entity in entities if entity.PLATFORM in (Platform.LIGHT, Platform.SWITCH)]
    assert len(controls) == 1, (endpoint_id, controls)
    assert controls[0].PLATFORM == Platform.SWITCH, (endpoint_id, controls)

print("All eight generic endpoints discovered as switches, with zero light entities and no quirk.")
