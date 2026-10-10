# ESPHome Anova Nano Component

An ESPHome component for integrating Anova Nano Sous Vide Cookers via Bluetooth Low Energy (BLE).

## ⚠️ Warning: Work in Progress

This component implements basic BLE communication with Anova Nano devices based on the pyanova-nano Python library. While the connection and basic framework are implemented, the data parsing logic may need refinement based on actual device behavior.

## Features (Implemented)

- Connect to Anova Nano via BLE using known UUIDs from pyanova-nano
- Read current temperature (basic implementation)
- Detect heating/cooking state from motor speed
- Expose as a Climate entity in Home Assistant
- Basic control commands (start/stop) via Home Assistant interface
- Periodic polling for sensor updates
- Asynchronous notification handling

## Installation

1. Add this repository to your ESPHome configuration:
   ```yaml
   external_components:
     - source: github://yourusername/esphome-anova-nano
       components: [anova_nano]
   ```

2. Use the component in your ESPHome YAML:
   ```yaml
   external_components:
     - source: github://yourusername/esphome-anova-nano
       components: [anova_nano]

   climate:
     - platform: anova_nano
       name: "Anova Nano"
       scan_timeout: 5  # Optional, default 5 seconds
       poll_interval: 30  # Optional, default 30 seconds
   ```

## Configuration Options

- `scan_timeout`: Time in seconds to scan for devices (default: 5)
- `poll_interval`: Interval in seconds to poll for sensor updates (default: 30)

## Development Status

- [x] Project structure created
- [x] BLE connection and service discovery
- [x] Notification handling framework
- [x] Basic sensor data parsing (temperature, heating state)
- [x] Climate entity implementation
- [x] Basic control command implementation (start/stop)
- [ ] Target temperature reading implementation
- [ ] Advanced features (timer, unit selection, etc.)
- [ ] Testing with actual Anova Nano hardware

## Protocol Reference

This component is based on research of the [pyanova-nano](https://github.com/klutchell/pyanova-nano) Python library, which has successfully reverse-engineered the Anova Nano BLE protocol.

Key UUIDs from pyanova-nano:
- Service: `0e140000-0af1-4582-a242-773e63054c68`
- Write: `0e140001-0af1-4582-a242-773e63054c68`
- Read: `0e140002-0af1-4582-a242-773e63054c68`
- Async/Notify: `0e140003-0af1-4582-a242-773e63054c68`

## Known Limitations

- Temperature parsing assumes 0.01°C precision (may need adjustment)
- Target temperature reading not yet implemented
- Error handling for malformed packets is basic
- No implementation for advanced features like timer setting or unit selection

## License

MIT License
