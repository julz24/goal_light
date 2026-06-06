"""Goal Light - Switch platform (mode hors-saison)."""
from __future__ import annotations

import logging

from homeassistant.components.switch import SwitchEntity
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
    """Set up Goal Light switch from a config entry."""
    coordinator: GoalLightCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([GoalLightOffSeasonSwitch(coordinator, entry)])


class GoalLightOffSeasonSwitch(CoordinatorEntity, SwitchEntity):
    """Switch pour activer/désactiver le mode hors-saison."""

    _attr_has_entity_name = True
    _attr_name = "Mode hors-saison"
    _attr_icon = "mdi:hockey-puck"

    def __init__(self, coordinator: GoalLightCoordinator, entry: ConfigEntry) -> None:
        """Initialize the switch."""
        super().__init__(coordinator)
        self._attr_unique_id = f"{entry.data[CONF_IP]}_off_season"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.data[CONF_IP])},
        )

    @property
    def is_on(self) -> bool | None:
        """Return true if off-season mode is active."""
        if self.coordinator.data:
            return self.coordinator.data.get("offSeason", False)
        return None

    async def async_turn_on(self, **kwargs) -> None:
        """Activer le mode hors-saison."""
        await self.coordinator.async_set_off_season(True)

    async def async_turn_off(self, **kwargs) -> None:
        """Désactiver le mode hors-saison."""
        await self.coordinator.async_set_off_season(False)
