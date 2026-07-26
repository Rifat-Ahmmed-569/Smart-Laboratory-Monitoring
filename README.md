<div align="center">

# SCS-RG · Smart Campus Safety & Response Grid

**Sense · Analyze · Prioritize · Respond**

A real-time, multi-room safety monitoring platform built for university laboratories.
One ESP32 fleet, one MQTT topic, one browser dashboard, zero backend to babysit.

![Status](https://img.shields.io/badge/status-live-2E9F4E?style=flat-square)
![Version](https://img.shields.io/badge/version-1.0.0-1B3A6E?style=flat-square)
![Platform](https://img.shields.io/badge/platform-ESP32-black?style=flat-square)
![Transport](https://img.shields.io/badge/transport-MQTT%20over%20WSS-2196F3?style=flat-square)
![Frontend](https://img.shields.io/badge/frontend-single%20HTML-F59E0B?style=flat-square)
![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)

*A joint project of Bots Bangla and TahCreations · University of Frontier Technology, Bangladesh*

</div>

---

## Table of contents

- [1 · What it does](#1--what-it-does)
- [2 · Why it exists](#2--why-it-exists)
- [3 · System architecture](#3--system-architecture)
- [4 · Feature reference](#4--feature-reference)
- [5 · Hardware](#5--hardware)
  - [5.1 · Bill of materials](#51--bill-of-materials)
  - [5.2 · Pin map](#52--pin-map)
  - [5.3 · Wiring schematic](#53--wiring-schematic)
  - [5.4 · Power budget](#54--power-budget)
  - [5.5 · Physical build](#55--physical-build)
  - [5.6 · Deployment topology](#56--deployment-topology)
- [6 · MQTT contract](#6--mqtt-contract)
- [7 · Alert engine](#7--alert-engine)
- [8 · Firmware setup](#8--firmware-setup)
- [9 · Dashboard setup](#9--dashboard-setup)
- [10 · Configuration](#10--configuration)
- [11 · Repository layout](#11--repository-layout)
- [12 · Roadmap](#12--roadmap)
- [13 · Troubleshooting](#13--troubleshooting)
- [14 · Credits](#14--credits)

---

## 1 · What it does

SCS-RG watches every laboratory on a campus at once and turns a rising sensor reading into a **named room**, a **severity**, and an **action**, in under two seconds.

Each lab carries an ESP32-based sensor pod that samples seven safety-critical variables — combustible gas, carbon monoxide, indoor air quality, open flame, water level, temperature and humidity, and human occupancy. The pod publishes one consolidated JSON packet over MQTT, and a single-file browser dashboard renders the whole campus as a live command board.

There is no server, no database, and no build pipeline between the sensor and the operator. The dashboard is a static HTML file that speaks MQTT directly to the broker over secure WebSockets.

---

## 2 · Why it exists

Traditional campus safety systems are wired, siloed, and slow. A flame sensor triggers a local buzzer; a gas leak trips a single relay; nothing tells the security office *which* room, *how bad*, or *whether anyone is still inside*.

SCS-RG closes that gap:

- **Sense** every hazard class in every room continuously, not just when someone walks by
- **Analyze** each reading against a configurable threshold band and a rolling 60-sample history
- **Prioritize** so the room that matters most rises to the top of the operator's screen
- **Respond** through visual alerts, toast notifications, and a timestamped stream for post-incident review

The tagline on the logo — *Safer Campus. Smarter Response* — is the design brief in five words.

---

## 3 · System architecture

```
┌─────────────────┐    Wi-Fi / MQTT       ┌──────────────────┐    WSS / MQTT       ┌───────────────────┐
│  ESP32 sensor   │  ─────────────────▶   │   MQTT broker    │  ─────────────────▶ │  SCS-RG dashboard │
│  pod (per lab)  │       koba-samsu      │  broker.hivemq   │      koba-samsu     │   (single HTML)   │
└─────────────────┘                       └──────────────────┘                     └───────────────────┘
        ▲                                                                                   │
        │                                                                                   │
        │ ADC / OneWire / GPIO                                          Chart.js · MQTT.js · GSAP
        │                                                                                   │
   ┌────┴────────────────────────────┐                                                       │
   │  MQ-2 · MQ-7 · MQ-135 · Flame   │                                                       ▼
   │  DHT22 · Water probe · PIR      │                                              Operator's browser
   │  Relay · Button · Buzzer        │                                              (no backend, no DB)
   └─────────────────────────────────┘
```

**Layer split**

| Layer | Component | Responsibility |
|---|---|---|
| Edge | ESP32-WROOM-32 | Sample every sensor on a fixed interval, build one JSON packet, publish over Wi-Fi |
| Transport | HiveMQ (public) or self-hosted broker | Fan-out, retention, MQTT over WSS for browser clients |
| Command | `scs-rg.html` | Parse, score, chart, alert, and render every packet — all in the browser |
| Access | Password gate | Single-key access control on the dashboard's landing page |

---

## 4 · Feature reference

**Landing page**
- GSAP-driven scroll choreography (parallax hero, staggered card reveals, counting stat strip)
- Live schematic simulating campus telemetry with periodic incident cycles
- Password-gated route into the live dashboard

**Dashboard**
- Nine views: Dashboard, Live Monitoring, Rooms, Alerts, Charts, Device Health, System Health, MQTT Monitor, Settings
- Dynamic room cards — grid rebuilds itself from `payload.rooms[]`; no code change needed to add a lab
- Rolling history charts per metric per room (Chart.js, 60 samples deep)
- Three-state alert engine: Normal → Warning → Critical, with toast-on-first-occurrence
- Full-detail room modal with all sensor readouts plus device context
- Live MQTT log with topic, timestamp, and syntax-highlighted parsed payload
- HiveMQ-style connection panel: host / port / path / TLS / client ID / username / password / topic / keep-alive
- Persistent settings in `localStorage`
- Light + dark theme, mobile drawer nav, `prefers-reduced-motion` honoured
- No backend, no database, no build step — one static file

---

## 5 · Hardware

### 5.1 · Bill of materials

**Per-lab sensor pod**

| # | Component | Part | Interface | Purpose |
|---|---|---|---|---|
| 1 | Microcontroller | ESP32-WROOM-32 DevKit v1 | — | Sampling, Wi-Fi, MQTT publish |
| 2 | Combustible gas | MQ-2 module | Analog | LPG, propane, smoke |
| 3 | Carbon monoxide | MQ-7 module | Analog | CO detection |
| 4 | Air quality | MQ-135 module | Analog | VOC, NH₃, benzene |
| 5 | Flame detector | IR flame sensor module | Analog (inverted) | Open-flame detection |
| 6 | Water level | Analog water probe | Analog | Flood / spill detection |
| 7 | Climate | DHT22 (AM2302) | 1-wire digital | Temperature + humidity |
| 8 | Occupancy | HC-SR501 PIR | Digital | Presence in room |
| 9 | Actuator | 5V single-channel relay | Digital | Local buzzer / cutoff |
| 10 | Input | Momentary push button | Digital, pulled-up | Manual arm / reset |
| 11 | Signaling | Active buzzer 5V | Via relay | Audible alert |
| 12 | Power | 5V 2A adapter + AMS1117-3.3 (on-board) | — | ESP32 and sensor rails |
| 13 | Prototype | Full-size breadboard + jumpers, *or* custom PCB | — | Interconnect |
| 14 | Enclosure | ABS project box, wall-mount | — | Field housing |

**Central**

| # | Component | Notes |
|---|---|---|
| — | MQTT broker | `broker.hivemq.com` for demos; Mosquitto or HiveMQ on a VPS for production |
| — | Any modern browser | Dashboard runs client-side; no server needed |

### 5.2 · Pin map

| ESP32 GPIO | Direction | Connected to | Sensor / role |
|---|---|---|---|
| **GPIO 34** | ADC1_CH6, input | MQ-2 AO | Combustible gas |
| **GPIO 35** | ADC1_CH7, input | MQ-7 AO | Carbon monoxide |
| **GPIO 32** | ADC1_CH4, input | MQ-135 AO | Air quality |
| **GPIO 33** | ADC1_CH5, input | Flame sensor AO | Flame (inverted reading) |
| **GPIO 36** | ADC1_CH0, input | Water probe signal | Water level |
| **GPIO 4** | 1-wire, in/out | DHT22 DATA | Temp + humidity |
| **GPIO 27** | Digital, input | PIR OUT | Motion detection |
| **GPIO 26** | Digital, output | Relay IN | Buzzer / cutoff |
| **GPIO 25** | Digital, input, pull-up | Push button | Manual arm / reset |
| **GPIO 2** | Digital, output | On-board LED | Status indicator |
| **3V3** | Power | DHT22, PIR VCC | Logic-level sensors |
| **VIN / 5V** | Power | MQ modules, flame, relay VCC | 5V sensor rails |
| **GND** | Ground | Common ground for all modules | — |

> ADC1 channels are used exclusively — ADC2 is unavailable while Wi-Fi is active on ESP32.

### 5.3 · Wiring schematic

```
                                    ┌────────────────────────────┐
                                    │        ESP32-WROOM-32      │
                                    │        (DevKit v1)         │
                                    │                            │
   MQ-2   AO ──── 100k ─┬── GND ────┤ GPIO 34 (ADC)      GPIO 4 ├──── DATA ── DHT22
                        │            │                            │              │ VCC ─ 3V3
                        └── ADC-in   │                            │              │ GND ─ GND
   MQ-7   AO ─── voltage divider ────┤ GPIO 35 (ADC)              │
                                    │                            │
   MQ-135 AO ─── voltage divider ────┤ GPIO 32 (ADC)     GPIO 27 ├──── OUT ── PIR HC-SR501
                                    │                            │              │ VCC ─ 5V
   Flame  AO ────────────────────────┤ GPIO 33 (ADC)              │              │ GND ─ GND
                                    │                            │
   Water probe SIG ──────────────────┤ GPIO 36 (ADC)     GPIO 26 ├──── IN ─── Relay (NO)
                                    │                            │              └── Buzzer / cutoff
   Push button ── 10k pull-up ───────┤ GPIO 25                    │
                                    │                            │
                                    │ 3V3 ──────────┐    GPIO 2 ├──── Status LED
                                    │ 5V  (VIN) ────┤            │
                                    │ GND ──────────┴── Common ──┤
                                    └────────────────────────────┘

   NOTE · Every MQ-series module ships with a built-in load resistor and a 5 V heater.
          The AO pin swings 0-5 V; feed it through a 2:1 divider (10k / 10k) before the
          ESP32 ADC, which tops out at ~3.3 V. Do not connect AO directly to GPIO 34/35/32.
```

**Voltage divider (per MQ analog line)**

```
   MQ AO (0-5V) ────┬──── 10 kΩ ────┬──── ESP32 GPIO (0-3.3V)
                    │               │
                   (unused)         └──── 10 kΩ ──── GND
```

### 5.4 · Power budget

| Rail | Load | Typical | Peak |
|---|---|---|---|
| 5 V | ESP32 (Wi-Fi TX) | 180 mA | 500 mA |
| 5 V | MQ-2, MQ-7, MQ-135 heaters (3 × ~150 mA) | 450 mA | 450 mA |
| 5 V | Flame + water + relay + PIR | 90 mA | 120 mA |
| 3.3 V | DHT22 | 2 mA | 2 mA |
| **Total 5 V** | — | **~720 mA** | **~1.1 A** |

Design for a **5 V, 2 A** regulated supply per pod. The MQ series draws continuous current on the heater coil, so budget for it — an under-spec adapter is the most common cause of intermittent resets in the field.

### 5.5 · Physical build

**Enclosure**
- ABS wall-mount project box, minimum interior 120 × 80 × 55 mm
- Ventilation grilles on two opposing sides so the MQ heaters draw fresh air and do not accumulate their own emissions
- Cable gland on the underside for the 5 V DC feed
- Optional acrylic window over the status LED

**Sensor placement**
- **MQ-series and flame** — mount pointing into the room, not against a wall; avoid direct HVAC airflow which flushes readings
- **DHT22** — through a small hole in the enclosure so it reads room air, not the ESP32's warm exhaust
- **PIR** — offset from the enclosure with its own dome-facing cutout, ideally at 2.2-2.5 m ceiling height for full-room coverage
- **Water probe** — routed on a separate cable to the actual water-risk zone (under a sink, along a floor drain)
- **Relay + buzzer** — inside the enclosure; add a piezo transducer on the lid for audibility

**Wiring order**
1. Populate the ESP32 header on the breadboard or PCB first
2. Establish common ground before any signal wiring
3. Bring up the 5 V and 3.3 V rails and verify with a multimeter *before* connecting any sensor
4. Land the DHT22 and PIR first — both are digital and quick to sanity-check
5. Land the analog sensors last, one at a time, verifying the ADC reads a plausible number before moving on
6. Only then connect the relay coil

**PCB variant (optional)**

For a permanent deployment, migrate the breadboard to a two-layer PCB:
- Top layer: signal traces
- Bottom layer: ground pour
- Screw terminals for every external sensor cable
- On-board 5 V → 3.3 V regulator (AMS1117-3.3 is already on the ESP32 DevKit)
- Fused input on the 5 V rail (0.5 A poly-fuse)
- JST connectors on the sensor headers so pods can be serviced without desoldering

### 5.6 · Deployment topology

Two supported topologies:

**A · Per-lab node** *(recommended)*
- One ESP32 pod per laboratory
- Each pod publishes to the same MQTT topic with its own `node_id`
- The dashboard's `payload.rooms[]` will list every room from every node

**B · Gateway aggregation**
- One ESP32 acts as a gateway
- Room-level microcontrollers (ESP32-C3 minis, ESP8266) forward readings over ESP-NOW or serial
- The gateway consolidates and publishes one MQTT packet with the full `rooms[]` array
- Matches the reference payload shape verbatim

Choose topology **A** for reliability (any single pod failure only silences one room) and **B** for cost (one MQTT/Wi-Fi client for the whole floor).

---

## 6 · MQTT contract

**Broker** — any MQTT 3.1.1 broker with WebSocket support. Public demo uses `broker.hivemq.com`.

**Transport**
- Firmware: MQTT over TCP · port 1883
- Dashboard: MQTT over secure WebSocket · `wss://broker.hivemq.com:8884/mqtt`

**Topic** — `koba-samsu` (single topic carries the whole campus)

**QoS** — 0 for telemetry, 1 optional for alarm-critical topics on a private broker

**Payload schema**

```json
{
  "node_id": "ESP32-01",
  "rssi": -54,
  "relay": "OFF",
  "button_state": "OFF",
  "sensor_state": "on",
  "wifi_state": "CONNECTED",
  "time_sent": "03:41AM",
  "rooms": [
    {
      "room_id": "lab01",
      "room_name": "IoT Lab",
      "mq2": 88,
      "mq7": 2044,
      "mq135": 1306,
      "flame": 4095,
      "water_pct": 11,
      "water_raw": 758,
      "temp": 30.5,
      "hum": 100.0,
      "pir": false,
      "state": 0,
      "state_text": "NORMAL",
      "warnings": []
    }
  ]
}
```

**Field reference**

| Field | Type | Range | Notes |
|---|---|---|---|
| `node_id` | string | — | Publisher identity |
| `rssi` | number | -100 to 0 dBm | Wi-Fi signal strength |
| `relay` | string | `"ON"` \| `"OFF"` | Current relay state |
| `button_state` | string | `"ON"` \| `"OFF"` | Push button |
| `sensor_state` | string | `"on"` \| `"off"` | Global sensor enable |
| `wifi_state` | string | `"CONNECTED"` \| `"DISCONNECTED"` | Link state |
| `time_sent` | string | `HH:MMam/pm` | Local timestamp |
| `rooms[]` | array | 1..N | Per-room telemetry |
| `rooms[].mq2/mq7/mq135/flame/water_raw` | number | 0-4095 | Raw ADC counts |
| `rooms[].water_pct` | number | 0-100 | Normalized water level |
| `rooms[].temp` | number | -40 to 80 °C | DHT22 |
| `rooms[].hum` | number | 0-100 %RH | DHT22 |
| `rooms[].pir` | boolean | — | Motion detected |
| `rooms[].state` | number | 0-3 | 0 nominal, 1 warning, 2/3 critical |
| `rooms[].state_text` | string | — | Human-readable state |
| `rooms[].warnings[]` | array of strings | — | Metric names currently over threshold |

The dashboard is tolerant of missing fields — anything absent falls back to a default and the room card simply omits that metric.

---

## 7 · Alert engine

Every reading is scored against a configurable threshold band. Rooms inherit the worst active metric.

| Metric | Normal | Warning | Critical | Direction |
|---|---|---|---|---|
| Temperature | 20 – 35 °C | 35 – 45 °C | > 45 °C | Rising |
| Humidity | 30 – 70 %RH | 70 – 85 %RH | > 85 %RH | Rising |
| Water level | < 50 % | 50 – 80 % | > 80 % | Rising |
| MQ-2 (combustible) | < 1500 ADC | 1500 – 2500 ADC | > 2500 ADC | Rising |
| MQ-7 (CO) | < 1800 ADC | 1800 – 3000 ADC | > 3000 ADC | Rising |
| MQ-135 (air quality) | < 1500 ADC | 1500 – 2500 ADC | > 2500 ADC | Rising |
| Flame | > 2000 ADC | 2000 – 1000 ADC | < 1000 ADC | **Falling** (inverted) |

Every threshold is editable live from the dashboard's Settings view. A dedup window prevents the same warning from firing repeatedly within 60 seconds; state changes always emit.

---

## 8 · Firmware setup

**Toolchain**

- VS Code + PlatformIO (recommended)
- Or Arduino IDE 2.x with the ESP32 board package installed
- Board: `esp32:esp32:esp32` (ESP32 Dev Module)

**Libraries**

```ini
lib_deps =
  knolleary/PubSubClient        ; MQTT client
  adafruit/DHT sensor library   ; DHT22
  bblanchon/ArduinoJson         ; JSON payload builder
```

**Configuration file (`config.h`)**

```cpp
// Wi-Fi
#define WIFI_SSID       "your-ssid"
#define WIFI_PASSWORD   "your-password"

// MQTT
#define MQTT_HOST       "broker.hivemq.com"
#define MQTT_PORT       1883
#define MQTT_TOPIC      "koba-samsu"
#define MQTT_CLIENT_ID  "scsrg-esp32-01"

// Identity
#define NODE_ID         "ESP32-01"

// Sampling
#define SAMPLE_MS       2000    // full room refresh every 2 s
#define PUBLISH_MS      2000    // publish on every sample
```

**Sampling loop (pseudo)**

```cpp
void loop() {
  ensureWifi();
  ensureMqtt();

  every(SAMPLE_MS, [] {
    Room r;
    r.mq2       = analogRead(PIN_MQ2);
    r.mq7       = analogRead(PIN_MQ7);
    r.mq135     = analogRead(PIN_MQ135);
    r.flame     = analogRead(PIN_FLAME);
    r.water_raw = analogRead(PIN_WATER);
    r.water_pct = map(r.water_raw, 0, 4095, 0, 100);
    r.temp      = dht.readTemperature();
    r.hum       = dht.readHumidity();
    r.pir       = digitalRead(PIN_PIR);

    scoreRoom(&r);           // fill state, state_text, warnings[]
    publishPacket(&r);       // build JSON, MQTT publish
  });
}
```

The full reference firmware is under `firmware/`.

---

## 9 · Dashboard setup

**Requirements** — none. It's one HTML file.

**Run**

```bash
# Clone the repo
git clone https://github.com/tahsan/scs-rg.git
cd scs-rg

# Open in a browser
open scs-rg.html          # macOS
xdg-open scs-rg.html      # Linux
start scs-rg.html         # Windows
```

Or simply double-click the file.

**Access**

- Landing page loads first
- Click *Live dashboard*
- Enter the access key (set in the source, see [§10](#10--configuration))
- The dashboard connects to the broker and starts rendering

**Host it**

Drop `scs-rg.html` into any static host — GitHub Pages, Vercel, Netlify, an S3 bucket, or Cloudflare Pages. No build step, no environment variables.

---

## 10 · Configuration

**Access key** — set at the top of the second `<script>` block in `scs-rg.html`:

```js
/* ############################################################
   #   DASHBOARD ACCESS PASSWORD                              #
   #   Change the value on the line below to set a new key.   #
   ############################################################ */
const DASHBOARD_PASSWORD = 'hozorolurakosabamcu569956';
```

> The password lives in plaintext in the file. It keeps casual visitors out; it does not protect against anyone who opens View Source. For real access control, wire the gate to Firebase Auth or a signed-URL flow.

**Broker** — change at runtime from *Settings → Broker* on the dashboard, or edit the defaults in `DEFAULT_CONN`:

```js
const DEFAULT_CONN = {
  host: 'broker.hivemq.com',
  port: 8884,
  path: '/mqtt',
  ssl: true,
  clientId: 'scsrg-' + Math.random().toString(16).slice(2, 10),
  username: '',
  password: '',
  topic: 'koba-samsu',
  keepalive: 30,
  reconnectPeriod: 4000,
  connectTimeout: 8000,
};
```

Runtime edits persist in the browser's `localStorage` under key `scsrg-conn`.

**Thresholds** — editable live from *Settings → Alert thresholds*, or seeded in `CONFIG.thresholds` at the top of the dashboard script.

---

## 11 · Repository layout

```
scs-rg/
├── scs-rg.html               # The whole product: landing + gate + dashboard
├── firmware/
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp
│   │   ├── config.h          # Wi-Fi + MQTT credentials
│   │   ├── sensors.cpp       # ADC + DHT + PIR sampling
│   │   ├── scoring.cpp       # Threshold classification
│   │   └── mqtt_publish.cpp  # JSON build + MQTT publish
│   └── schematics/
│       ├── wiring.png        # Full pod wiring
│       ├── pcb-top.png
│       └── pcb-bottom.png
├── docs/
│   ├── payload-schema.json
│   ├── thresholds.md
│   └── screenshots/
│       ├── landing.png
│       ├── gate.png
│       └── dashboard.png
└── README.md
```

---

## 12 · Roadmap

- [x] Live MQTT dashboard with dynamic rooms
- [x] Three-state alert engine with rolling history
- [x] Password-gated landing page with GSAP scroll choreography
- [x] Configurable broker (HiveMQ-style connection panel)
- [ ] Firebase Auth on the access gate
- [ ] Firestore-backed alert history for post-incident review
- [ ] Push notifications (web push + FCM)
- [ ] SMS escalation via Twilio for critical events
- [ ] Multi-building support with a building/floor picker
- [ ] Role-based access (viewer / operator / admin)
- [ ] Predictive anomaly detection on rolling history
- [ ] Native Android companion app

---

## 13 · Troubleshooting

**Dashboard shows "Disconnected"**
- The broker at your configured host is unreachable. Open *Settings → Broker* and verify host / port / path / TLS.
- If you're on `wss://` on a corporate network, try `ws://` on port 8000.
- Check the *MQTT Monitor* view for the exact error line.

**Charts render at zero height**
- Should not happen — the dashboard delays chart initialization until the container is visible. If it does, refresh the page after unlocking.

**Values look wrong on the ESP32 side**
- MQ-series modules need a **24-hour burn-in** the first time they're powered. Fresh readings drift heavily until the heater stabilizes.
- Verify the analog voltage divider — an MQ AO pin fed directly to a 3.3 V GPIO reads pinned at 4095 and never moves.
- ADC2 pins (GPIO 0, 2, 4, 12-15, 25-27) are unavailable while Wi-Fi is active. Only use ADC1 (GPIO 32-39).

**PIR fires constantly**
- HC-SR501 modules have a sensitivity pot and a time-delay pot. Turn sensitivity down first, then extend the delay so single readings don't retrigger.

**Password shake but no unlock**
- The access key is case-sensitive and contains only lowercase letters and digits. Verify no extra whitespace.

---

## 14 · Credits

**Project lead · Firmware · Dashboard** — [Tahsan Masum Fahim](https://github.com/tahsan)
**Ventures** — [Bots Bangla](https://botsbangla.com) *(hardware)* · [TahCreations Digital Solutions](https://tahcreations.com) *(software)*
**Institution** — Department of Electrical & Electronic Engineering, University of Frontier Technology, Bangladesh

**Built with**
- [MQTT.js](https://github.com/mqttjs/MQTT.js) — browser MQTT client
- [Chart.js](https://www.chartjs.org/) — rolling telemetry charts
- [GSAP + ScrollTrigger](https://gsap.com/) — landing-page motion
- [HiveMQ public broker](https://www.hivemq.com/mqtt/public-mqtt-broker/) — demo transport
- [PubSubClient](https://pubsubclient.knolleary.net/) — ESP32-side MQTT
- [ArduinoJson](https://arduinojson.org/) — payload serialization

---

<div align="center">

*Sense · Analyze · Prioritize · Respond*
**Safer campus. Smarter response.**

</div>
