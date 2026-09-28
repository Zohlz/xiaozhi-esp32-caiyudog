# caiyu-dog

[中文](README_zh.md) | [English] | [日本語](README_ja.md)

## 1. About

caiyu-dog is a **great-looking four-legged desktop robot dog**. It extends the XiaoZhi voice assistant with four servo legs, two WS2812 light strips and a touch pad: an ESP32-C3 runs voice and display, a 128x64 OLED is the face (a pair of blinking geometric eyes), and the legs take care of standing, sitting, walking and dancing.

The project keeps a single board implementation, **`caiyu-c3`**; every other board has been removed.

### Join the QQ group

- Group ID: 1028667976 (group 4)

![caiyu-dog](docs/caiyu-dog.png)

<!-- Photo above: the file lives at docs/caiyu-dog.png; change the path or delete the line if you want another picture -->

**What it can do**

- **Voice chat**: offline wake word via ESP-SR; one touch starts or ends a conversation, and the dog stands up when it wakes
- **Emotions**: 21 geometric eye expressions; while idle it falls asleep (half-closed eyes, breathing motion, floating Zzz)
- **Leg actions**: stand / sit / lie down / walk / turn / wave / stretch / swing / scratch and more, triggered by voice or from the web page
- **Mood moves**: small leg gestures that follow the current emotion while chatting
- **Light strip**: solid / breathe / rainbow / flow / flash, with adjustable color, brightness and speed
- **Web console**: once the device is on Wi-Fi open `http://<device-ip>/`, or long-press the touch pad for one second to show a QR code on the screen
- **Servo trim**: correct each leg by ±20° from the web console and store it in NVS

## 2. Assembly and wiring

### 2.1 Bill of materials

- **Board**: ESP32-C3 SuperMini (4 MB flash)
- **Digital microphone**: INMP441 / ICS43434
- **Amplifier**: MAX98357A
- **Speaker**: 8Ω 2~3W or 4Ω 2~3W
- **Display**: SH1106 OLED, 128x64, I2C
- **Touch module**: TTP223 style (**must be powered from 3.3 V**)
- **Light strip**: WS2812 / WS2812B, 4 LEDs
- **Servos**: 180° servos ×4 (SG90 / MG90S class)
- **Resistor**: 2kΩ ×1 (in the touch branch, as current limiting)
- **USB-C cable** and a PC that can flash the firmware

You will probably also want a multimeter and a soldering kit.

### 2.2 Pin map

| Function | GPIO | Notes |
| --- | --- | --- |
| Microphone WS / SCK / DIN | 7 / 6 / 5 | I2S input |
| Speaker LRCK / BCLK / DOUT | 4 / 3 / 2 | I2S output |
| OLED SDA / SCL | 8 / 9 | I2C, address 0x3C |
| Touch pad + light strip DIN | 0 | **shared pin**, time-multiplexed |
| Left front servo | 1 | driven by LEDC |
| Right front servo | 10 | driven by LEDC |
| Left back servo | 20 | driven by LEDC |
| Right back servo | 21 | driven by LEDC |

**Wiring caveats:**

1. **Do not power the servos from the board**: four servos moving together can pull well over 1.5 A. Use a separate supply and **connect the grounds together**.
2. **The touch module must be powered from 3.3 V**: at 5 V its OUT pin sits at 5 V, which exceeds the ESP32-C3 input rating and can damage the chip.
3. **Keep the 2kΩ resistor in the touch branch only**: never put it in series with the strip data line, it would ruin the WS2812 800 kHz timing.
4. **GPIO20 / GPIO21 are also the UART0 default pins of the C3**: once the servos are attached, read the log over the USB (Serial/JTAG) port. On a board with an on-board USB-to-UART bridge only, the log will be invisible while the servos occupy those pins.
5. **GPIO8 / GPIO9 are strapping pins** and are also the on-board WS2812 pins on many C3 boards: when you use them for the OLED, the on-board LED may flicker with I2C traffic — that is expected.

### 2.3 Mechanical assembly and calibration

Mount the four legs diagonally symmetric (left front / right front / left back / right back). The right-hand legs are mirrored in code, so "both front legs forward" is a single angle value.

If a servo horn is not centred or the legs are asymmetric, there is no need to change code: open the "Settings" tab of the console, drag the trim sliders until the legs look right and hit "Save to device" — the values survive a reboot.

## 3. Flashing

### 3.1 Local build (ESP-IDF)

ESP-IDF **v6.0.2** is required:

```sh
source /path/to/esp-idf/export.sh

python scripts/build.py caiyu-c3      # configure and build
idf.py -p <PORT> flash monitor        # flash and watch the log
```

Once the device joins Wi-Fi the log prints the console URL:

```text
I (1234) CaiyuC3Board: control page: http://192.168.1.23/
```

### 3.2 Web flashing

【Put the URL here if a web flasher is available; drop this section otherwise】

### Release notes

> Add one entry here for every firmware release.

- **Current**: voice chat, 21 eye expressions, leg actions and mood moves, five light effects, web console (Remote / Emotions / Light / Settings), on-screen QR code entry, saved servo trim

## 4. Credits and license

Based on [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32). The geometric eye expressions are derived from `esp32-sh1106-oled-emojis`; the leg actions and light effects from `zzpet`.

MIT, see [LICENSE](LICENSE).
