#include "jimlib.h"
//#include <heltec.h>	

JimWiFi jw;
EggTimer sec(100), sec5(5000);

struct {
	int led = 2;
	int rxHeater = 17;
	int txHeater = 16;
	int rxDisplay = 22;
	int dummy1 = 21;
	int dummy2 = 22;
} pins;

class SerialChunker {
	Stream &ser;
	uint8_t buf[512];
	int l = 0, timeout = 0, startTime = 0;
	uint32_t lastRead = 0;
public:
	int total = 0;
	SerialChunker(Stream &s, int ms = 34) : ser(s), timeout(ms) { }

	bool check(std::function<void(const char *, int, int)> f) { 
		uint32_t ms = millis();
		if (ser.available() && l < sizeof(buf)) {
			int n = ser.readBytes(buf + l, sizeof(buf) - l);
			if (n > 0 && l == 0) { 
				startTime = ms;
			}
			l += n;
			total += n;
			lastRead = ms;
		}
		if ((ms - lastRead > timeout && l > 0) || l == sizeof(buf)) {
			f((const char *)buf, l, startTime);
			l = 0;
		} 
	}
};


void setup() {
	esp_task_wdt_init(20, true);
	esp_task_wdt_add(NULL);
	//Heltec.begin(true, true, true, true, 915E6);
	pinMode(pins.led, OUTPUT);
	digitalWrite(pins.led, 1);
	Serial.begin(921600, SERIAL_8N1);
	jw.onConnect([](void) {});
	jw.onOTA([](void) {});
	Serial2.begin(4800, SERIAL_8N1, pins.rxHeater, pins.txHeater, false);
	Serial2.setTimeout(1);
	Serial1.begin(4800, SERIAL_8N1, pins.rxDisplay, -1, false);
	Serial1.setTimeout(1);
	Serial.println("Restart");
}

SerialChunker sc1(Serial1);
SerialChunker sc2(Serial2);



void hexdump(const char *in, int len, char *out) { 
	for (int n = 0; n < len; n++) { 
		sprintf(out + 2 * n, "%02x", in[n]);
	}
	out[2 * len] = '\0';
}


void sendHex(Stream &s, const char *out) { 
	int l = strlen(out);
	for (const char *p = out; p < out + l; p += 2) { 
		char b[3];
		b[0] = p[0];
		b[1] = p[1];
		b[2] = 0;
		int c;
		sscanf(b, "%x", &c);
		s.write(c);
	}
	//s.flush();
	Serial.printf(">> %04d: %s\n", millis() % 10000, out);
}

uint32_t lastRec; 


typedef struct { 
	const char *pat;
	int count;
} CharIntPair; 

CharIntPair patterns[] = {
	{"fb1b00aaffffff0000525e", 10}, // off and happy
	{"fb1b0301ffffff01009b67", 3},  // button push to turn on
	{"fb1b00aaffffff0100616f", 20}, // on happy 
	{"fb1b0300ffffff0000edf6", 2},  // button push to turn off
	{"fb1b02aaffffff000032bd", 7},  // turning off
	{"fb1b00aaffffff0000525e", 10}, // off and happy
	{"fb1b0301ffffff01009b67", 3},  // button push to turn on
	{"fb1b00aaffffff0100616f", 20}, // on happy 
};

struct ResetPatternSequencer {
	const char *getNext() { 
		const char *r = patterns[idx].pat;
		if (++count >= patterns[idx].count) { 
			idx = min(idx + 1, (int)(sizeof(patterns) / sizeof(patterns[0]) - 1));
			count = 0;
		}
		return r;
	}
	void startPattern() { idx = count = 0; }
	int idx, count;
} resetPat;

std::string lastS2;
int state = 0, errCount = 0;

void loop() {
	esp_task_wdt_reset();
	jw.run();
	if (sec.tick()) {
		digitalWrite(pins.led, !digitalRead(pins.led));
		//std::string s = strfmt("S1: %d\tS2: %d", sc1.total, sc2.total);
		//Serial.println(s.c_str());
		//jw.udpDebug(s.c_str());
	}

	sc1.check([](const char *b, int l, int t) {
		char hexbuf[2048];
		hexdump(b, l, hexbuf);
		std::string s = strfmt("\t\t\t\t\tS1 %04d: %s", t % 10000, hexbuf);
		//Serial.print(lastS2.c_str());
		Serial.println(s.c_str());
		//jw.udpDebug(s.c_str());
	});


	sc2.check([](const char *b, int l, int t) {
		char hexbuf[2048];
		hexdump(b, l, hexbuf);
		std::string s = strfmt("S2 %04d: %s", t % 10000, hexbuf);
		Serial.println(s.c_str());
		//jw.udpDebug(s.c_str());
	
		if (strstr(hexbuf, "fa1b100c15") == hexbuf) {
			if (errCount++ > 20) {
				std::string s = strfmt("ERROR PACKET %s", hexbuf);
				Serial.println(s.c_str());
				jw.udpDebug(s.c_str());
				resetPat.startPattern();
				errCount = 0;
			}
		}
		sendHex(Serial2, resetPat.getNext());

		static int pktCount = 0;
		s = strfmt("EC %d PKT %d PI %d\n", errCount, pktCount++, resetPat.idx);
		Serial.println(s.c_str());
		jw.udpDebug(s.c_str());
	});



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

