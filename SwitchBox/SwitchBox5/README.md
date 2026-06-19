# Cab Switchbox 5

ESP32-based 16-input network switchbox for the AgOpenGPS Rate Controller.

## Repository contents

- `firmware/ESP32_Cab_Switchbox/` — Arduino firmware source.
- `release/` — precompiled ESP32 firmware files.
- `docs/` — user manual, printable HTML, screenshots, and manual generator.
- `hardware/SwitchBox5_Terminals_PCB_01/` — Rev 01 PCB production files.

## Hardware target

- ESP32 board: ESP32 DevKit / DOIT ESP32 DEVKIT V1
- Arduino FQBN: `esp32:esp32:esp32doit-devkit-v1`
- PCB: `SwitchBox5-Terminals-PCB-01`
- Recommended enclosure: Hammond `E1555K2GY`, NEMA 4X
- Board supply: 12-24 VDC
- Onboard buzzer: 12 V only; use a 12 V supply if buzzer operation is required

## Build

Install Arduino CLI and the Espressif ESP32 Arduino core, then run from the
repository root:

```powershell
arduino-cli compile `
  --fqbn esp32:esp32:esp32doit-devkit-v1 `
  --output-dir build `
  firmware/ESP32_Cab_Switchbox
```

## Flashing

The easiest full-device image is:

```text
release/ESP32_Cab_Switchbox.ino.merged.bin
```

Flash the merged image at address `0x0` with an ESP32 flashing tool.

Arduino CLI can also compile and upload the source directly:

```powershell
arduino-cli upload `
  -p COMx `
  --fqbn esp32:esp32:esp32doit-devkit-v1 `
  firmware/ESP32_Cab_Switchbox
```

Replace `COMx` with the switchbox serial port.

## Documentation

See `docs/CAB_SWITCHBOX_MANUAL.pdf` for setup, terminal assignments, network
configuration, wiring, buzzer behavior, and troubleshooting.

## Current build

Compiled June 19, 2026:

- Program storage: 1,045,571 bytes (79%)
- Dynamic memory: 50,504 bytes (15%)

