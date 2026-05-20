"""Goal Light integration."""
from __future__ import annotations

import logging
from datetime import timedelta

import aiohttp
import async_timeout

from homeassistant.config_entries import ConfigEntry
from homeassistant.const import Platform
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import ConfigEntryNotReady
from homeassistant.helpers.aiohttp_client import async_get_clientsession
from homeassistant.helpers.update_coordinator import DataUpdateCoordinator, UpdateFailed

from .const import DOMAIN, CONF_IP, SCAN_INTERVAL

_LOGGER = logging.getLogger(__name__)

PLATFORMS: list[Platform] = [
    Platform.LIGHT,
    Platform.SENSOR,
    Platform.NUMBER,
]


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up Goal Light from a config entry."""
    ip = entry.data[CONF_IP]
    session = async_get_clientsession(hass)

    coordinator = GoalLightCoordinator(hass, session, ip)

    await coordinator.async_config_entry_first_refresh()

    hass.data.setdefault(DOMAIN, {})[entry.entry_id] = coordinator

    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)

    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload a config entry."""
    if unload_ok := await hass.config_entries.async_unload_platforms(entry, PLATFORMS):
        hass.data[DOMAIN].pop(entry.entry_id)
    return unload_ok


class GoalLightCoordinator(DataUpdateCoordinator):
    """Coordinator to fetch data from the Goal Light device."""

    def __init__(self, hass: HomeAssistant, session: aiohttp.ClientSession, ip: str) -> None:
        """Initialize."""
        self.session = session
        self.ip = ip
        self.base_url = f"http://{ip}"

        super().__init__(
            hass,
            _LOGGER,
            name=DOMAIN,
            update_interval=timedelta(seconds=SCAN_INTERVAL),
        )

    async def _async_update_data(self) -> dict:
        """Fetch data from Goal Light device."""
        try:
            async with async_timeout.timeout(10):
                async with self.session.get(f"{self.base_url}/state") as resp:
                    resp.raise_for_status()
                    return await resp.json()
        except aiohttp.ClientError as err:
            raise UpdateFailed(f"Error communicating with Goal Light at {self.ip}: {err}") from err
        except Exception as err:
            raise UpdateFailed(f"Unexpected error: {err}") from err

    async def async_set_state(self, on: bool) -> None:
        """Turn light on or off."""
        payload = {"on": on}
        async with async_timeout.timeout(10):
            async with self.session.post(f"{self.base_url}/set", json=payload) as resp:
                resp.raise_for_status()
        await self.async_request_refresh()

    async def async_set_leds(self, color: str | None = None, effect: str | None = None) -> None:
        """Set LED color or effect."""
        payload = {}
        if color is not None:
            payload["color"] = color
        if effect is not None:
            payload["effect"] = effect
        async with async_timeout.timeout(10):
            async with self.session.post(f"{self.base_url}/setleds", json=payload) as resp:
                resp.raise_for_status()
        await self.async_request_refresh()

    async def async_set_brightness(self, brightness: int) -> None:
        """Set LED brightness (0-255)."""
        payload = {"brightness": brightness}
        async with async_timeout.timeout(10):
            async with self.session.post(f"{self.base_url}/brightness", json=payload) as resp:
                resp.raise_for_status()
        await self.async_request_refresh()

    async def async_set_led_count(self, count: int) -> None:
        """Set number of LEDs."""
        payload = {"count": count}
        async with async_timeout.timeout(10):
            async with self.session.post(f"{self.base_url}/setleds", json=payload) as resp:
                resp.raise_for_status()
        await self.async_request_refresh()
