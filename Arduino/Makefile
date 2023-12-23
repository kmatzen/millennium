DISPLAY_DEVICE=$(realpath /dev/serial/by-id/usb-Arduino_LLC_Millennium_Beta-if00)
KEYPAD_DEVICE=$(realpath /dev/serial/by-id/usb-Arduino_LLC_Millennium_Alpha-if00)
DISPLAY_FQBN='arduino:avr:millennium_beta'
KEYPAD_FQBN='arduino:avr:millennium_alpha'

build/keypad/keypad.ino.elf: sketches/keypad/keypad.ino
	arduino-cli compile --fqbn $(KEYPAD_FQBN) sketches/keypad --config-file arduino-cli.yaml --output-dir ./build/keypad

build/display/display.ino.elf: sketches/display/display.ino
	arduino-cli compile --fqbn $(DISPLAY_FQBN) sketches/display --config-file arduino-cli.yaml --output-dir ./build/display

install_keypad: build/keypad/keypad.ino.elf
	arduino-cli upload -p $(KEYPAD_DEVICE) --fqbn $(KEYPAD_FQBN) sketches/keypad --config-file arduino-cli.yaml --input-dir ./build/keypad

install_display: build/display/display.ino.elf
	arduino-cli upload -p $(DISPLAY_DEVICE) --fqbn $(DISPLAY_FQBN) sketches/display --config-file arduino-cli.yaml --input-dir ./build/display

install: build 
	arduino-cli upload -p $(DISPLAY_DEVICE) --fqbn $(DISPLAY_FQBN) sketches/display --config-file arduino-cli.yaml --input-dir ./build/display
	arduino-cli upload -p $(KEYPAD_DEVICE) --fqbn $(KEYPAD_FQBN) sketches/keypad --config-file arduino-cli.yaml --input-dir ./build/keypad

build: build/keypad/keypad.ino.elf build/display/display.ino.elf

