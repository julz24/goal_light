"""Goal Light - Number platform."""
from __future__ import annotations

import logging

from homeassistant.components.number import NumberEntity, NumberMode
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity import DeviceInfo
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.helpers.update_coordinator import CoordinatorEntity

from . import GoalLightCoordinator
from .const import DOMAIN, CONF_IP

_LOGGER = logging.getLogger(__name__)


async def async_setup_entry(
    hass: HomeAssistant,
    entry: ConfigEntry,
    async_add_entities: AddEntitiesCallback,
) -> None:
    """Set up Goal Light number entities from a config entry."""
    coordinator: GoalLightCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([
        GoalLightLedCountNumber(coordinator, entry),
        GoalLightBrightnessNumber(coordinator, entry),
    ])


class GoalLightLedCountNumber(CoordinatorEntity, NumberEntity):
    """Number entity for LED count."""

    _attr_has_entity_name = True
    _attr_name = "LED Count"
    _attr_icon = "mdi:led-strip-variant"
    _attr_native_min_value = 1
    _attr_native_max_value = 1000
    _attr_native_step = 1
    _attr_mode = NumberMode.BOX

    def __init__(self, coordinator: GoalLightCoordinator, entry: ConfigEntry) -> None:
        """Initialize the LED count entity."""
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.data[CONF_IP]}_led_count"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.data[CONF_IP])},
        )

    @property
    def native_value(self) -> float | None:
        """Return the current LED count."""
        if self.coordinator.data:
            return self.coordinator.data.get("led_count")
        return None

    async def async_set_native_value(self, value: float) -> None:
        """Set the LED count."""
        await self.coordinator.async_set_led_count(int(value))


class GoalLightBrightnessNumber(CoordinatorEntity, NumberEntity):
    """Number entity for brightness (0-255)."""

    _attr_has_entity_name = True
    _attr_name = "Brightness"
    _attr_icon = "mdi:brightness-6"
    _attr_native_min_value = 0
    _attr_native_max_value = 255
    _attr_native_step = 1
    _attr_mode = NumberMode.SLIDER

    def __init__(self, coordinator: GoalLightCoordinator, entry: ConfigEntry) -> None:
        """Initialize the brightness number entity."""
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.data[CONF_IP]}_brightness"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.data[CONF_IP])},
        )

    @property
    def native_value(self) -> float | None:
        """Return the current brightness."""
        if self.coordinator.data:
            return self.coordinator.data.get("brightness")
        return None

    async def async_set_native_value(self, value: float) -> None:
        """Set the brightness."""
        await self.coordinator.async_set_brightness(int(value))
