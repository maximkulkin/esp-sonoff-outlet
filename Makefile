PROGRAM = esp-sonoff-socket
PROGRAM_SRC_DIR = main

EXTRA_COMPONENTS = \
	extras/http-parser \
	extras/dhcpserver \
	extras/rboot-ota \
	$(abspath components/button) \
	$(abspath components/led-status) \
	$(abspath components/wifi-config) \
	$(abspath components/wolfssl) \
	$(abspath components/cjson) \
	$(abspath components/homekit)

FLASH_SIZE ?= 8
HOMEKIT_SPI_FLASH_BASE_ADDR = 0x7f000

EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS

include $(SDK_PATH)/common.mk

monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud 115200 --elf $(PROGRAM_OUT)
