ARDUINO_CLI ?= arduino-cli
CONFIG_FILE = arduino-cli.yaml

DISPLAY_FQBN = arduino:avr:millennium_beta
KEYPAD_FQBN  = arduino:avr:millennium_alpha

# Override with DISPLAY_DEVICE=/dev/cu.usbmodemXXX on macOS if needed
DISPLAY_DEVICE ?= $(shell realpath /dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00 2>/dev/null)
KEYPAD_DEVICE ?= $(shell realpath /dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00 2>/dev/null)

.PHONY: build install install_keypad install_display clean

build: build/keypad/keypad.ino.hex build/display/display.ino.hex

build/keypad/keypad.ino.hex build/keypad/keypad.ino.elf: sketches/keypad/keypad.ino
	$(ARDUINO_CLI) compile --fqbn $(KEYPAD_FQBN) sketches/keypad \
		--config-file $(CONFIG_FILE) --output-dir ./build/keypad

build/display/display.ino.hex build/display/display.ino.elf: sketches/display/display.ino
	$(ARDUINO_CLI) compile --fqbn $(DISPLAY_FQBN) sketches/display \
		--config-file $(CONFIG_FILE) --output-dir ./build/display

install_keypad: build/keypad/keypad.ino.hex
	$(ARDUINO_CLI) upload -p $(KEYPAD_DEVICE) --fqbn $(KEYPAD_FQBN) \
		sketches/keypad --config-file $(CONFIG_FILE) --input-dir ./build/keypad

install_display: build/display/display.ino.hex
	$(ARDUINO_CLI) upload -p $(DISPLAY_DEVICE) --fqbn $(DISPLAY_FQBN) \
		sketches/display --config-file $(CONFIG_FILE) --input-dir ./build/display

install: build
	$(ARDUINO_CLI) upload -p $(DISPLAY_DEVICE) --fqbn $(DISPLAY_FQBN) \
		sketches/display --config-file $(CONFIG_FILE) --input-dir ./build/display
	$(ARDUINO_CLI) upload -p $(KEYPAD_DEVICE) --fqbn $(KEYPAD_FQBN) \
		sketches/keypad --config-file $(CONFIG_FILE) --input-dir ./build/keypad

clean:
	rm -rf build/keypad build/display
