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

## Installation via HACS (recommandé)

1. Dans Home Assistant, ouvrir **HACS → Intégrations**
2. Cliquer sur **⋮ (menu)** → **Dépôts personnalisés**
3. Ajouter l'URL : `https://github.com/julz24/goal_light`
4. Catégorie : **Intégration**
5. Cliquer **Ajouter**, puis chercher **Goal Light** dans HACS et installer
6. **Redémarrer Home Assistant**

[![Ouvrir dans HACS](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=julz24&repository=goal_light&category=integration)

---

## Installation manuelle

1. Télécharger le fichier `goal_light.zip` depuis les [Releases](https://github.com/julz24/goal_light/releases/latest)
2. Extraire le contenu dans `config/custom_components/goal_light/`
3. **Redémarrer Home Assistant**

---

## Configuration

1. Aller dans **Paramètres → Appareils et services → Ajouter une intégration**
2. Chercher **Goal Light**
3. Entrer l'adresse IP de l'appareil (ex. `192.168.2.x`)
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

- `goal` — Animation but !
- `rainbow` — Arc-en-ciel
- `strobe` — Stroboscope
- `solid` — Couleur fixe
- `pulse` — Pulsation

---

## Contribuer

Les PRs sont les bienvenues. Pour les bugs, ouvrir une [issue](https://github.com/julz24/goal_light/issues).

---

## Licence

MIT
