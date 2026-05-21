"""Goal Light - Light platform."""
from __future__ import annotations

import logging
from typing import Any

from homeassistant.components.light import (
    ATTR_BRIGHTNESS,
    ATTR_RGB_COLOR,
    ATTR_EFFECT,
    ColorMode,
    LightEntity,
    LightEntityFeature,
)
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
    """Set up Goal Light from a config entry."""
    coordinator: GoalLightCoordinator = hass.data[DOMAIN][entry.entry_id]
    async_add_entities([GoalLightEntity(coordinator, entry)])


class GoalLightEntity(CoordinatorEntity, LightEntity):
    """Representation of the Goal Light."""

    _attr_has_entity_name = True
    _attr_name = None  # Uses device name
    _attr_supported_color_modes = {ColorMode.RGB}
    _attr_color_mode = ColorMode.RGB
    _attr_supported_features = LightEntityFeature.EFFECT

    def __init__(self, coordinator: GoalLightCoordinator, entry: ConfigEntry) -> None:
        """Initialize the light."""
        super().__init__(coordinator)
        self._entry = entry
        self._attr_unique_id = f"{entry.data[CONF_IP]}_light"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, entry.data[CONF_IP])},
            name=f"Goal Light ({entry.data[CONF_IP]})",
            manufacturer="Goal Light",
            model="Goal Light Controller",
        )

    @property
    def is_on(self) -> bool | None:
        """Return true if light is on."""
        if self.coordinator.data:
            return self.coordinator.data.get("on", False)
        return None

    @property
    def brightness(self) -> int | None:
        """Return the brightness of the light (0-255)."""
        if self.coordinator.data:
            return self.coordinator.data.get("brightness")
        return None

    @property
    def rgb_color(self) -> tuple[int, int, int] | None:
        """Return the RGB color based on active color toggles."""
        if not self.coordinator.data:
            return None
        d = self.coordinator.data
        r = 255 if d.get("red") else 0
        g = 255 if d.get("white") else 0
        b = 255 if (d.get("blue") or d.get("white")) else 0
        return (r, g, b)

    @property
    def effect(self) -> str | None:
        """Return the current effect."""
        if self.coordinator.data:
            return self.coordinator.data.get("effect")
        return None

    @property
    def effect_list(self) -> list[str] | None:
        """Return list of available effects."""
        return ["goal", "rainbow", "strobe", "solid", "pulse"]

    async def async_turn_on(self, **kwargs: Any) -> None:
        """Turn the light on."""
        await self.coordinator.async_set_state(True)

        if ATTR_BRIGHTNESS in kwargs:
            await self.coordinator.async_set_brightness(kwargs[ATTR_BRIGHTNESS])

        if ATTR_RGB_COLOR in kwargs:
            r, g, b = kwargs[ATTR_RGB_COLOR]
            # Map RGB to firmware color names
            await self.coordinator.async_set_color("rouge", r > 100)
            await self.coordinator.async_set_color("blanc", g > 100 and b > 100)
            await self.coordinator.async_set_color("bleu", b > 100 and g < 100)

    async def async_turn_off(self, **kwargs: Any) -> None:
        """Turn the light off."""
        await self.coordinator.async_set_state(False)
