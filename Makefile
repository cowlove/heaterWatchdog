BOARD=esp32doit-devkit-v1
#BOARD=heltec_wifi_lora_32
#BOARD=nodemcu-32s
VERBOSE=1
MONITOR_SPEED=921600

GIT_VERSION := "$(shell git describe --abbrev=4 --dirty --always --tags)"
BUILD_EXTRA_FLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"

backtrace:
	tr ' ' '\n' | /home/jim/.arduino15/packages/esp32/tools/xtensa-esp32-elf-gcc/*/bin/xtensa-esp32-elf-addr2line -f -i -e ${BUILD_DIR}/${MAIN_NAME}.elf
	
CHIP=esp32
OTA_ADDR=192.168.4.148
IGNORE_STATE=1

include ${HOME}/Arduino/libraries/makeEspArduino/makeEspArduino.mk

fixtty:
	stty -F ${UPLOAD_PORT} -hupcl -crtscts -echo raw  ${MONITOR_SPEED}

cat:	fixtty
	cat ${UPLOAD_PORT}

socat:  
	socat udp-recv:9000 - 
mocat:
	mosquitto_sub -h 192.168.4.1 -t "heater/#" -v   

curl: ${BUILD_DIR}/${MAIN_NAME}.bin
	curl -v --limit-rate 10k --progress-bar -F "image=@${BUILD_DIR}/${MAIN_NAME}.bin" ${OTA_ADDR}/update 


