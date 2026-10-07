"""Validate button metadata and serialize actual Zigbee ON commands per endpoint."""

import asyncio
import importlib.util
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, MagicMock

import zigpy.device
import zigpy.types as t
from zigpy.zcl import foundation
from zigpy.zcl.clusters.general import OnOff

handler = Path(__file__).resolve().parents[1] / "integrations/zha/flexispot_e7q_h2.py"
spec = importlib.util.spec_from_file_location("flexispot_e7q_h2", handler)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


async def main():
    from zha.application.platforms.button import Button
    from zha.application import EntityPlatform, EntityType

    definition = module.QUIRK.zha_device_factory.quirk_definition
    metadata = definition.entity_metadata
    assert len(metadata) == 8
    assert {item.endpoint_id for item in metadata} == set(range(1, 9))
    assert {rule.endpoint_id for rule in definition.disabled_default_entities} == set(range(1, 9))
    assert all(rule.cluster_id == OnOff.cluster_id for rule in definition.disabled_default_entities)
    # Endpoint 9 remains a normal Analog Input height sensor.
    assert all(rule.endpoint_id != 9 for rule in definition.disabled_default_entities)
    assert module.hide_original_control(SimpleNamespace(translation_key=None))

    app = MagicMock()
    app.get_sequence.return_value = 42
    device = zigpy.device.Device(app, t.EUI64.convert("74:4d:bd:ff:fe:64:10:6a"), 0x1234)
    for item in metadata:
        assert item.entity_platform == EntityPlatform.BUTTON
        assert item.command_name == "on"
        assert item.cluster_id == OnOff.cluster_id
        assert not module.hide_original_control(SimpleNamespace(translation_key=item.translation_key))
        assert item.initially_disabled == (item.endpoint_id == 7)
        assert item.entity_type == (EntityType.CONFIG if item.endpoint_id == 7 else EntityType.STANDARD)
        endpoint = device.add_endpoint(item.endpoint_id)
        cluster = endpoint.add_input_cluster(OnOff.cluster_id)
        endpoint.request = AsyncMock(return_value=[foundation.Status.SUCCESS])
        # Exercise ZHA's actual button press method, then Zigpy's actual serializer.
        button = SimpleNamespace(_cluster=cluster, _command_name=item.command_name,
                                 args=list(item.args), kwargs=dict(item.kwargs))
        await Button.async_press(button)
        request = endpoint.request.call_args.kwargs
        assert request["cluster"] == 0x0006
        assert request["command_id"] == 0x01
        header, payload = cluster.deserialize(request["data"])
        assert header.command_id == OnOff.ServerCommandDefs.on.id
        assert payload.serialize() == b""
    print("ZHA button metadata, light suppression, and all eight wire commands passed.")


asyncio.run(main())
