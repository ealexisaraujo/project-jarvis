ARDUINO_CLI := arduino-cli
CONFIG := arduino-cli.yaml
SKETCH := firmware/project_jarvis
BUILD_DIR := build
FQBN := esp32:esp32:waveshare_esp32_s3_touch_lcd_185:CDCOnBoot=cdc,PartitionScheme=app3M_fat9M_16MB
NO_SECRETS ?= 0

ifeq ($(NO_SECRETS),1)
SECRET_BUILD_PROPERTY := --clean --build-property "compiler.cpp.extra_flags=-MMD -c -DPROJECT_JARVIS_DISABLE_LOCAL_SECRETS"
endif

.PHONY: bootstrap board-list build test upload monitor screenshot clean

bootstrap:
	./scripts/bootstrap.sh

board-list:
	$(ARDUINO_CLI) board list --config-file $(CONFIG)

build:
	$(ARDUINO_CLI) compile \
		--config-file $(CONFIG) \
		--fqbn $(FQBN) \
		$(SECRET_BUILD_PROPERTY) \
		--output-dir $(BUILD_DIR) \
		$(SKETCH)

test:
	@rtc_test_bin="$$(mktemp -t jarvis-rtc-test.XXXXXX)"; \
	trap 'rm -f "$$rtc_test_bin"' EXIT; \
	$(CXX) -std=c++11 -Wall -Wextra -Werror \
		-Ifirmware/project_jarvis \
		tests/rtc_datetime_test.cpp \
		firmware/project_jarvis/rtc_datetime.cpp \
		-o "$$rtc_test_bin"; \
	"$$rtc_test_bin"

upload:
	@test -n "$(PORT)" || (echo "Usage: make upload PORT=/dev/cu.usbmodem..." >&2; exit 1)
	$(ARDUINO_CLI) upload \
		--config-file $(CONFIG) \
		--fqbn $(FQBN) \
		--port $(PORT) \
		--input-dir $(BUILD_DIR) \
		$(SKETCH)

monitor:
	@test -n "$(PORT)" || (echo "Usage: make monitor PORT=/dev/cu.usbmodem..." >&2; exit 1)
	$(ARDUINO_CLI) monitor \
		--config-file $(CONFIG) \
		--port $(PORT) \
		--config baudrate=115200 \
		--config dtr=off \
		--config rts=off

screenshot:
	@test -n "$(PORT)" || (echo "Usage: make screenshot PORT=/dev/cu.usbmodem... [TILE=0..5] [OUTPUT=/path/file.png]" >&2; exit 1)
	python3 scripts/capture_screen.py \
		--port "$(PORT)" \
		$(if $(strip $(TILE)),--tile "$(TILE)",) \
		$(if $(strip $(OUTPUT)),--output "$(OUTPUT)",)

clean:
	rm -rf $(BUILD_DIR)
