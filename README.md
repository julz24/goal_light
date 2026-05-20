# Goal Light — Intégration Home Assistant

[![hacs_badge](https://img.shields.io/badge/HACS-Custom-orange.svg)](https://github.com/hacs/integration)
[![GitHub release](https://img.shields.io/github/release/julz24/goal_light.svg)](https://github.com/julz24/goal_light/releases)
[![Validate](https://github.com/julz24/goal_light/actions/workflows/validate.yml/badge.svg)](https://github.com/julz24/goal_light/actions/workflows/validate.yml)

Intégration HACS pour contrôler un appareil **Goal Light** via Home Assistant.  
Polling local via HTTP — aucune dépendance cloud.

---

## Entités créées

| Entité | Type | Description |
|--------|------|-------------|
| `light.goal_light` | Light | Allumage/extinction, RGB, effets, brightness |
| `sensor.goal_light_score` | Sensor | Score actuel |
| `sensor.goal_light_status` | Sensor | Statut de l'appareil |
| `number.goal_light_led_count` | Number | Nombre de LEDs (1–1000) |
| `number.goal_light_brightness` | Number | Luminosité (0–255) |

---

## 📱 Application Android (Pour Connexion Wi-Fi initiale)

👉 **[Télécharger l'APK](https://github.com/julz24/goal_light/releases/latest/download/canadiens_goal_light.apk)**

Contrôle ta Goal Light depuis ton téléphone Android.  
> ⚠️ Activer *Sources inconnues* dans les paramètres Android pour installer l'APK.

## Installation via HACS (Si vous avez Home Assistant)

1. Dans Home Assistant, ouvrir **HACS → Intégrations**
2. Cliquer sur **⋮ (menu)** → **Dépôts personnalisés**
3. Ajouter l'URL : `https://github.com/julz24/goal_light`
4. Catégorie : **Intégration**
5. Cliquer **Ajouter**, puis chercher **Goal Light** dans HACS et installer
6. **Redémarrer Home Assistant**

[![Ouvrir dans HACS](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=julz24&repository=goal_light&category=integration)

[![Flash Firmware](https://img.shields.io/badge/Flash-ESP32--C3-blue?logo=espressif)](https://julz24.github.io/goal_light/flash.html)

## 🖨️ Impression 3D

👉 **[Télécharger le modèle sur MakerWorld](https://makerworld.com/en/models/1852866-montreal-canadiens-logo-lightbox#profileId-1980887)**

Boîtier et diffuseur imprimables en 3D — logo des Canadiens de Montréal.  
Compatible avec le montage LED WS2812B et l'ESP32-C3 Super Mini.
>
> ## 🛒 Liste de matériel

| Composant | Lien |
|-----------|------|
| ESP32-C3 Super Mini (USB-C) | [AliExpress](https://www.aliexpress.com/item/1005008956664945.html) |
| LEDs WS2812B DC5V 4M 60 LEDs/m IP30 | [AliExpress](https://www.aliexpress.com/item/2036819167.html) |
> 💡 Les liens AliExpress peuvent expirer — chercher les mêmes composants si le lien ne fonctionne plus.

---

## Installation manuelle

1. Télécharger le fichier `goal_light.zip` depuis les [Releases](https://github.com/julz24/goal_light/releases/latest)
2. Extraire le contenu dans `config/custom_components/goal_light/`
3. **Redémarrer Home Assistant**

---

## Configuration

1. Aller dans **Paramètres → Appareils et services → Ajouter une intégration**
2. Chercher **Goal Light**
3. Entrer l'adresse IP de l'appareil (ex. `192.168.1.x`)
4. Un test de connexion est effectué automatiquement — si ça passe, c'est configuré !

--- 

## Format JSON attendu (`/state`)

L'intégration interroge l'endpoint `/state` toutes les 10 secondes.  
L'appareil doit retourner un JSON avec les champs suivants :

```json
{
  "on": true,
  "brightness": 200,
  "color": "#ff0000",
  "effect": "goal",
  "score": 3,
  "status": "playing",
  "led_count": 60,
  "uptime": 12345,
  "firmware": "1.0.0"
}
```

### Endpoints utilisés

| Endpoint | Méthode | Description |
|----------|---------|-------------|
| `/state` | GET | Lecture de l'état complet |
| `/set` | POST `{"on": true}` | Allumer / éteindre |
| `/setleds` | POST `{"color": "#ff0000", "effect": "goal", "count": 60}` | Couleur, effet, nombre de LEDs |
| `/brightness` | POST `{"brightness": 200}` | Luminosité |
| `/reboot` | POST | Redémarrage de l'appareil |
| `/logs` | GET | Logs de l'appareil |
| `/update` | POST | Mise à jour firmware |

---

## Effets disponibles

- `goal` — Animation but (Flash Rouge)
- `solid` — Couleur fixe (Bleu, Blanc et Rouge)

---

## Contribuer

Les PRs sont les bienvenues. Pour les bugs, ouvrir une [issue](https://github.com/julz24/goal_light/issues).

---

## Licence

MIT
