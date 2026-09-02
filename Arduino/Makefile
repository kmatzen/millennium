ARDUINO_CLI ?= arduino-cli

# (#256) Point arduino-cli at this directory so it picks up hardware/ (the
# vendored Millennium board definitions) and libraries/. Exported rather than
# written into a config file: arduino-cli's own data directory differs per OS
# (~/Library/Arduino15 on macOS, ~/.arduino15 on Linux), and the config file
# this build used to pass hardcoded one machine's absolute paths, which made
# `make build` fail outright anywhere else.
#
# Requires the AVR core, once per machine:
#   arduino-cli core install arduino:avr
export ARDUINO_DIRECTORIES_USER = $(CURDIR)

PROJECT_VERSION := $(shell cat ../VERSION)
FIRMWARE_BUILD_ID ?= $(shell python3 ../tools/firmware_source_id.py)
BUILD_ARGS = --build-property 'compiler.cpp.extra_flags=-DMILLENNIUM_FIRMWARE_VERSION="$(PROJECT_VERSION)" -DMILLENNIUM_FIRMWARE_BUILD="$(FIRMWARE_BUILD_ID)"'
BUILD_CACHE ?= build/cache

DISPLAY_FQBN = millennium:avr:millennium_beta
KEYPAD_FQBN  = millennium:avr:millennium_alpha

# Override with DISPLAY_DEVICE=/dev/cu.usbmodemXXX on macOS if needed
DISPLAY_DEVICE ?= $(shell realpath /dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00 2>/dev/null)
KEYPAD_DEVICE ?= $(shell realpath /dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00 2>/dev/null)

.PHONY: build build_display install install_keypad install_display deploy deploy_display deploy_keypad clean

build: build/keypad/keypad.ino.hex build/display/display.ino.hex

build_display: build/display/display.ino.hex

build/keypad/keypad.ino.hex build/keypad/keypad.ino.elf: sketches/keypad/keypad.ino
	$(ARDUINO_CLI) compile --fqbn $(KEYPAD_FQBN) sketches/keypad \
		$(BUILD_ARGS) --build-path $(BUILD_CACHE)/keypad --output-dir ./build/keypad

build/display/display.ino.hex build/display/display.ino.elf: sketches/display/display.ino
	$(ARDUINO_CLI) compile --fqbn $(DISPLAY_FQBN) sketches/display \
		$(BUILD_ARGS) --build-path $(BUILD_CACHE)/display --output-dir ./build/display

install_keypad: build/keypad/keypad.ino.hex
	$(ARDUINO_CLI) upload -p $(KEYPAD_DEVICE) --fqbn $(KEYPAD_FQBN) \
		sketches/keypad $(BUILD_ARGS) --input-dir ./build/keypad

install_display: build/display/display.ino.hex
	$(ARDUINO_CLI) upload -p $(DISPLAY_DEVICE) --fqbn $(DISPLAY_FQBN) \
		sketches/display $(BUILD_ARGS) --input-dir ./build/display

# Deploy to Pi via GPIO reset (build on Mac, sync, flash on Pi).
# Requires raspi-gpio and avrdude on the Pi.
DEPLOY_HOST ?= matzen@millennium-phone.local

deploy_display: build/display/display.ino.hex
	./deploy_display.sh $(DEPLOY_HOST)

deploy_keypad: build/keypad/keypad.ino.hex
	./deploy_keypad.sh $(DEPLOY_HOST)

deploy: build
	./deploy_keypad.sh $(DEPLOY_HOST)
	./deploy_display.sh $(DEPLOY_HOST)

install: build
	$(ARDUINO_CLI) upload -p $(DISPLAY_DEVICE) --fqbn $(DISPLAY_FQBN) \
		sketches/display $(BUILD_ARGS) --input-dir ./build/display
	$(ARDUINO_CLI) upload -p $(KEYPAD_DEVICE) --fqbn $(KEYPAD_FQBN) \
		sketches/keypad $(BUILD_ARGS) --input-dir ./build/keypad

clean:
	rm -rf build/keypad build/display
