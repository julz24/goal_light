"""Goal Light - Sensor platform."""
from __future__ import annotations

import logging

from homeassistant.components.sensor import SensorEntity, SensorStateClass
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
    """Set up Goal Light sensors from a config entry."""
    coordinator: GoalLightCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([
        GoalLightScoreSensor(coordinator, entry),
        GoalLightStatusSensor(coordinator, entry),
    ])


class GoalLightScoreSensor(CoordinatorEntity, SensorEntity):
    """Sensor representing the current score."""

    _attr_has_entity_name = True
    _attr_name = "Score"
    _attr_icon = "mdi:scoreboard"
    _attr_state_class = SensorStateClass.MEASUREMENT

    def __init__(self, coordinator: GoalLightCoordinator, entry: ConfigEntry) -> None:
        """Initialize the score sensor."""
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.data[CONF_IP]}_score"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.data[CONF_IP])},
        )

    @property
    def native_value(self) -> int | None:
        """Return the current score."""
        if self.coordinator.data:
            return self.coordinator.data.get("score")
        return None


class GoalLightStatusSensor(CoordinatorEntity, SensorEntity):
    """Sensor representing the device status."""

    _attr_has_entity_name = True
    _attr_name = "Status"
    _attr_icon = "mdi:information-outline"

    def __init__(self, coordinator: GoalLightCoordinator, entry: ConfigEntry) -> None:
        """Initialize the status sensor."""
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.data[CONF_IP]}_status"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.data[CONF_IP])},
        )

    @property
    def native_value(self) -> str | None:
        """Return the device status."""
        if self.coordinator.data:
            d = self.coordinator.data
            if d.get("flashing"):
                return "but"
            elif d.get("gameActive"):
                return "en_cours"
            else:
                return "inactif"
        return None

    @property
    def extra_state_attributes(self) -> dict:
        """Return extra state attributes."""
        if not self.coordinator.data:
            return {}
        data = self.coordinator.data
        return {
            "uptime": data.get("uptime"),
            "firmware": data.get("firmware"),
            "ip": self.coordinator.ip,
        }
