# YamAI Storage System

ESP32 firmware for monitoring yam-storage temperature, humidity, and an MQ135
sensor reading. A softmax regression model classifies the environment into
four categories, displays readings and diagnosis labels on an I2C LCD, controls
a heater and humidifier relay, and writes readings to Firebase Realtime
Database.

> **Use with care:** This is an experimental storage-control project, not a
> validated agricultural diagnostic system. Treat predictions as advisory,
> verify conditions and tubers directly, and test relay behavior with the
> loads disconnected before operating equipment unattended.

## Hardware and pins

The sketch's default pin assignments are:

| Component | Pin |
| --- | --- |
| DHT11 data | GPIO 3 |
| MQ135 analog output | GPIO 1 |
| Heater relay | GPIO 4 |
| Humidifier relay | GPIO 0 |
| I2C LCD SDA | GPIO 9 |
| I2C LCD SCL | GPIO 8 |

The LCD address is `0x27` (16x2). Confirm the pin mapping against your exact
ESP32 board; available pins, boot-strapping behavior, and analog-input support
vary by board. Use suitable relay drivers and safe power isolation for the
heater and humidifier; do not connect mains loads directly to ESP32 pins.

## Arduino IDE setup

1. Open `yamAI.ino` in Arduino IDE and select the matching ESP32 board and port.
2. Install these libraries using Library Manager:
   - Firebase ESP32 Client (Mobizt; provides `FirebaseESP32.h`)
   - LiquidCrystal I2C
   - DHT sensor library (Adafruit)
3. Configure credentials as described below.
4. Upload the sketch and open Serial Monitor at `115200` baud.

`Wire` and `WiFi` are provided by the ESP32 Arduino core. The MQ135 sensor
needs a warm-up period; the firmware prints a reminder to allow at least
20–30 minutes before trusting readings.

## Local credentials

The firmware reads its Wi-Fi and Firebase settings from `secrets.h`, which is
ignored by Git:

1. `secrets.h` is present locally with the settings previously in the sketch.
   Edit it if you need to change your Wi-Fi or Firebase configuration.
2. For a fresh clone, copy `secrets.example.h` to `secrets.h` and replace the
   placeholders with your own values.
3. Keep real values out of `secrets.example.h`, `.env.example`, and all files
   you commit.

The `.env` file is a local/reference template only. Arduino firmware does not
load dotenv files, so put the actual firmware configuration in `secrets.h`.

This sketch uses a Firebase legacy token. Do not publish that token or make
your database publicly writable. Firebase legacy-token authentication may be
deprecated or disabled for your project; use a supported authentication
method and restrictive database rules for deployment. The previously
hard-coded credentials should be rotated if they were ever shared or pushed
to a remote repository.

## Model and sensor limitations

`yam_model.h` contains the trained model parameters and expected six-feature
order. `yam_types.h` defines the model's diagnosis labels.

The sensor's displayed/reported `gas` value is currently the raw ADC reading,
not a calibrated gas concentration in ppm. The MQ135 resistance/calibration
constants in the sketch are not currently used to convert it to ppm. Calibrate
the sensor and validate the model with representative storage data before
using its output to control equipment.

## Push to GitHub safely

This project may be inside a parent Git repository. Run these commands from
this folder and verify the repository root **before staging**:

```sh
git rev-parse --show-toplevel
git status --short
```

If the root is not this `yamAI` folder, create a separate repository here
before staging to avoid including files from the parent:

```sh
git init
```

Confirm the local secrets are ignored:

```sh
git check-ignore .env secrets.h
```

Stage only the intended project files (never use `git add .` from a parent
repository):

```sh
git add README.md .gitignore .env.example secrets.example.h \
  yamAI.ino yam_model.h yam_types.h
git diff --cached --name-only
```

Check that `.env` and `secrets.h` do not appear in the staged list, then
commit and push:

```sh
git commit -m "Add YamAI storage system"
git branch -M main
git remote add origin https://github.com/YOUR_USERNAME/YOUR_REPOSITORY.git
git push -u origin main
```

Replace the URL with your GitHub repository URL. If a remote named `origin`
already exists, keep it rather than adding it again. If a secret was pushed,
rotate it; removing it in a later commit does not remove it from Git history.
