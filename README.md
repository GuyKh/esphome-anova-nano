# ESPHome Anova Nano Component

An ESPHome component for integrating Anova Nano Sous Vide Cookers via Bluetooth Low Energy (BLE).

## ⚠️ Warning: Work in Progress

This component is currently under development. The BLE communication logic needs to be completed based on the actual Anova Nano protocol.

## Features (Planned)

- Connect to Anova Nano via BLE
- Read current temperature
- Read target temperature
- Detect heating/cooking state
- Control device (start/stop) via MQTT/Home Assistant
- Expose as a Climate entity in Home Assistant

## Installation

1. Add this repository to your ESPHome configuration:
   ```yaml
   external_components:
     - source: github://yourusername/esphome-ova-nano
       components: [anova_nano]
   ```

2. Use the component in your ESPHome YAML:
   ```yaml
   external_components:
     - source: github://yourusername/esphome-ova-nano
       components: [anova_nano]

   climate:
     - platform: anova_nano
       name: "Anova Nano"
       scan_timeout: 5  # Optional, default 5 seconds
       poll_interval: 30  # Optional, default 30 seconds
   ```

## Development Status

- [x] Project structure created
- [ ] BLE connection and service discovery
- [ ] Notification handling
- [ ] Sensor data parsing (based on pyanova-nano findings)
- [ ] Climate entity implementation
- [ ] Control command implementation
- [ ] Testing with actual Anova Nano hardware

## Protocol Reference

This component is based on research of the [pyanova-nano](https://github.com/klutchell/pyanova-nano) Python library, which has successfully reverse-engineered the Anova Nano BLE protocol.

Key UUIDs from pyanova-nano:
- Service: `0e140000-0af1-4582-a242-773e63054c68`
- Write: `0e140001-0af1-4582-a242-773e63054c68`
- Read: `0e140002-0af1-4582-a242-773e63054c68`
- Async/Notify: `0e140003-0af1-4582-a242-773e63054c68`

## License

MIT License
