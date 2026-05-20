"""Config flow for Goal Light integration."""
from __future__ import annotations

import logging
from typing import Any

import aiohttp
import async_timeout
import voluptuous as vol

from homeassistant import config_entries
from homeassistant.data_entry_flow import FlowResult
from homeassistant.helpers.aiohttp_client import async_get_clientsession

from .const import CONF_IP, DOMAIN

_LOGGER = logging.getLogger(__name__)

STEP_USER_DATA_SCHEMA = vol.Schema(
    {
        vol.Required(CONF_IP): str,
    }
)


async def _test_connection(session: aiohttp.ClientSession, ip: str) -> bool:
    """Test connectivity to the Goal Light device."""
    try:
        async with async_timeout.timeout(5):
            async with session.get(f"http://{ip}/state") as resp:
                resp.raise_for_status()
                return True
    except Exception:
        return False


class GoalLightConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle a config flow for Goal Light."""

    VERSION = 1

    async def async_step_user(
        self, user_input: dict[str, Any] | None = None
    ) -> FlowResult:
        """Handle the initial step."""
        errors: dict[str, str] = {}

        if user_input is not None:
            ip = user_input[CONF_IP].strip()

            # Check for duplicate entry
            await self.async_set_unique_id(ip)
            self._abort_if_unique_id_configured()

            session = async_get_clientsession(self.hass)
            if await _test_connection(session, ip):
                return self.async_create_entry(
                    title=f"Goal Light ({ip})",
                    data={CONF_IP: ip},
                )
            else:
                errors["base"] = "cannot_connect"

        return self.async_show_form(
            step_id="user",
            data_schema=STEP_USER_DATA_SCHEMA,
            errors=errors,
            description_placeholders={
                "url": "http://<ip>/state",
            },
        )
