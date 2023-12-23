## Arduino Makefile

The `Makefile` in the `Arduino/` directory simplifies building and flashing the Arduino sketches for this project.

### Prerequisites

- Install [arduino-cli](https://arduino.github.io/arduino-cli).
- Configure `arduino-cli.yaml` for your environment (e.g., library paths, board URLs).

### Custom Firmware

The Arduino devices used in this project have custom firmware flashed to uniquely identify them. This allows consistent identification of devices under `/dev/serial/by-id/`. If you’re using different Arduinos, update the `DISPLAY_DEVICE` and `KEYPAD_DEVICE` paths accordingly.

### Variables

The Makefile uses the following variables:
- **`DISPLAY_DEVICE`**: Path to the display's serial device, e.g., `/dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00`.
- **`KEYPAD_DEVICE`**: Path to the keypad's serial device, e.g., `/dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00`.
- **`DISPLAY_FQBN`**: Fully Qualified Board Name for the display (e.g., `arduino:avr:millennium_beta`).
- **`KEYPAD_FQBN`**: Fully Qualified Board Name for the keypad (e.g., `arduino:avr:millennium_alpha`).

### Makefile Targets

- **Build Targets**:
  - `build/keypad/keypad.ino.elf`: Compiles the keypad sketch.
  - `build/display/display.ino.elf`: Compiles the display sketch.

- **Install Targets**:
  - `install_keypad`: Uploads the keypad sketch to the device at `$(KEYPAD_DEVICE)`.
  - `install_display`: Uploads the display sketch to the device at `$(DISPLAY_DEVICE)`.

- **Aggregate Targets**:
  - `build`: Builds both sketches.
  - `install`: Installs both sketches.

### Usage

1. **Build the sketches**:
   ```bash
   make build
   ```

2. **Install the sketches**:
   ```bash
   make install
   ```

3. **Install individual sketches**:
   - For the keypad:
     ```bash
     make install_keypad
     ```
   - For the display:
     ```bash
     make install_display
     ```

4. **Clean up build files**:
   If necessary, manually delete the `build/` directory.

### Notes
- The custom firmware flashed onto the Arduino devices ensures unique identification via `/dev/serial/by-id/`. If you are not using the same custom firmware, you may need to manually identify and specify the correct serial devices.
- Update the `DISPLAY_FQBN` and `KEYPAD_FQBN` as necessary for your specific board configurations.
- Ensure the Arduino devices are connected and accessible before running the `install` commands.