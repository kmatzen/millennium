ARDUINO_CLI ?= arduino-cli
CONFIG_FILE = arduino-cli.yaml
# Set BUILD_CONFIG=0 to skip config (e.g. on macOS when arduino-cli.yaml has Linux paths)
BUILD_CONFIG ?= 1
BUILD_ARGS = $(if $(filter 0,$(BUILD_CONFIG)),,--config-file $(CONFIG_FILE))

DISPLAY_FQBN = arduino:avr:millennium_beta
KEYPAD_FQBN  = arduino:avr:millennium_alpha

# Override with DISPLAY_DEVICE=/dev/cu.usbmodemXXX on macOS if needed
DISPLAY_DEVICE ?= $(shell realpath /dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00 2>/dev/null)
KEYPAD_DEVICE ?= $(shell realpath /dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00 2>/dev/null)

.PHONY: build build_display install install_keypad install_display flash_display clean

build: build/keypad/keypad.ino.hex build/display/display.ino.hex

build_display: build/display/display.ino.hex

build/keypad/keypad.ino.hex build/keypad/keypad.ino.elf: sketches/keypad/keypad.ino
	$(ARDUINO_CLI) compile --fqbn $(KEYPAD_FQBN) sketches/keypad \
		$(BUILD_ARGS) --output-dir ./build/keypad

build/display/display.ino.hex build/display/display.ino.elf: sketches/display/display.ino
	$(ARDUINO_CLI) compile --fqbn $(DISPLAY_FQBN) sketches/display \
		$(BUILD_ARGS) --output-dir ./build/display

install_keypad: build/keypad/keypad.ino.hex
	$(ARDUINO_CLI) upload -p $(KEYPAD_DEVICE) --fqbn $(KEYPAD_FQBN) \
		sketches/keypad $(BUILD_ARGS) --input-dir ./build/keypad

install_display: build/display/display.ino.hex
	$(ARDUINO_CLI) upload -p $(DISPLAY_DEVICE) --fqbn $(DISPLAY_FQBN) \
		sketches/display $(BUILD_ARGS) --input-dir ./build/display

# Flash display with avrdude. SKIP_TRIGGER=1 if caller already did 1200-baud (e.g. deploy script).
FLASH_PORT ?= /dev/ttyACM0
flash_display: build/display/display.ino.hex
ifneq ($(SKIP_TRIGGER),1)
	@echo "Triggering bootloader (1200 baud on $(FLASH_PORT))..."
	@stty -F $(FLASH_PORT) 1200 2>/dev/null || true
	@sleep 2
endif
	@echo "Flashing..."
	avrdude -p atmega32u4 -c avr109 -P $(FLASH_PORT) -b 57600 -U flash:w:build/display/display.ino.hex:i

install: build
	$(ARDUINO_CLI) upload -p $(DISPLAY_DEVICE) --fqbn $(DISPLAY_FQBN) \
		sketches/display $(BUILD_ARGS) --input-dir ./build/display
	$(ARDUINO_CLI) upload -p $(KEYPAD_DEVICE) --fqbn $(KEYPAD_FQBN) \
		sketches/keypad $(BUILD_ARGS) --input-dir ./build/keypad

clean:
	rm -rf build/keypad build/display
