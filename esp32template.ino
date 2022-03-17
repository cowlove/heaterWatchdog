#include "jimlib.h"
//#include <heltec.h>	

JimWiFi jw;
EggTimer sec(100);
//HardwareSerial Serial3(2);

struct {
	int led = 2;
	int tx = 16;
	int rx = 17;
	int dummy = 21;
} pins;

void setup() {
	esp_task_wdt_init(20, true);
	esp_task_wdt_add(NULL);
	//Heltec.begin(true, true, true, true, 915E6);
	pinMode(pins.led, OUTPUT);
	pinMode(pins.rx, INPUT);
	pinMode(pins.tx, INPUT);
	digitalWrite(pins.led, 1);
	Serial.begin(921600, SERIAL_8N1);
	jw.onConnect([](void) {});
	jw.onOTA([](void) {});
	Serial2.begin(4800, SERIAL_8N1, pins.rx, pins.dummy, true);
//	Serial3.begin(4800, SERIAL_8N1, pins.tx, pins.dummy, true);
	Serial2.setTimeout(1);
//	Serial3.setTimeout(1);
}

int old_rx, old_tx, rx_transitions, tx_transitions, rx_bytes;


void hexdump(const char *in, int len, char *out) { 
	for (int n = 0; n < len; n++) { 
		sprintf(out + 2 * n, "%02x", in[n]);
	}
	out[2 * len] = '\0';
}


class SerialChunker {
	HardwareSerial &ser;
	uint8_t buf[256];
	int l = 0, timeout = 0, startTime = 0;
	uint32_t lastRead = 0;
public:
	int total = 0;
	SerialChunker(HardwareSerial &s, int ms = 50) : ser(s), timeout(ms) { }

	bool check(std::function<void(const char *, int, int)> f) { 
		uint32_t ms = millis();
		if (ser.available()) {
			int n = ser.readBytes(buf + l, sizeof(buf) - l);
			if (n > 0 && l == 0) { 
				startTime = ms;
			}
			l += n;
			total += n;
			lastRead = ms;
		}
		if (ms - lastRead > timeout && l > 0) {
			f((const char *)buf, l, startTime);
			l = 0;
		} 
	}
};

uint32_t lastRec; 

SerialChunker sc2(Serial2);
//SerialChunker sc3(Serial3);

void loop() {
	esp_task_wdt_reset();
	jw.run();
	if (sec.tick()) {
		digitalWrite(pins.led, !digitalRead(pins.led));
		std::string s = strfmt("S2: %d\tS3: %d", sc2.total, 0);//sc3.total);
		Serial.println(s.c_str());
		jw.udpDebug(s.c_str());
	}
	int rx = digitalRead(pins.rx);
	if (rx != old_rx) rx_transitions++;
	old_rx = rx;

	int tx = digitalRead(pins.tx);
	if (tx != old_tx) tx_transitions++;
	old_tx = tx;


	sc2.check([](const char *b, int l, int t) {
		char hexbuf[2048];
		hexdump(b, l, hexbuf);
		std::string s = strfmt("S2 %04d: %s", t % 10000, hexbuf);
		Serial.println(s.c_str());
		jw.udpDebug(s.c_str());
	});

/*
	sc3.check([](const char *b, int l, int t) {
		char hexbuf[2048];
		hexdump(b, l, hexbuf);
		std::string s = strfmt("S3 %04d: %s", t % 10000, hexbuf);
		Serial.println(s.c_str());
		jw.udpDebug(s.c_str());
	});
*/
#if 0
	if (Serial2.available()) {
		int ms = millis();
		if (ms - lastRec > 50) {
			Serial.printf("\n%04d: ", (int)(ms % 10000));
		}
		lastRec = ms;
		char buf[1024];
		int n = Serial2.readBytes((uint8_t *)buf, sizeof(buf));
		rx_bytes += n;
		char hexbuf[2048];
		hexdump(buf, n, hexbuf);
		std::string s = hexbuf;
		//jw.udpDebug(s.c_str());
		Serial.print(s.c_str());
	}
#endif
	//delay(1);
}

