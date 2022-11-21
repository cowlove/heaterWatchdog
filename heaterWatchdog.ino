#include "jimlib.h"
#include "crc16heater.h"
#include "crc16heater.c"
//#include <heltec.h>	
#ifndef UBUNTU
#include <PubSubClient.h>
#endif
#include <deque>

JStuff j;

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
	SerialChunker(Stream &s, int ms = 10) : ser(s), timeout(ms) { }

	void check(std::function<void(const char *, int, int)> f) { 
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

struct MsgQueue { 
	std::deque<std::pair<String, int>> v;
	void clear() { v.clear(); } 
	void add(const String &msg, int count) { return add(msg.c_str(), count); }  
	void add(const char *msg, int count) { 
		v.push_back(std::pair<String, int>(String(msg), count)); 
	}
	const char *get() {
		if (!v.empty() && (v.front()).second == 0) { // left at zero, remove 
			v.pop_front(); 
		}
		if (v.empty()) {
			return NULL;
		} else {
			v.front().second--; // decrement count, possibly leaving it zero 
			return v.front().first.c_str();
		}
	}
} msgQueue;
void mqttCallback(char* topic, byte* payload, unsigned int length);



std::vector<std::string> split(const std::string& text, const std::string& delims)
{
    std::vector<std::string> tokens;
    std::size_t start = text.find_first_not_of(delims), end = 0;

    while((end = text.find_first_of(delims, start)) != std::string::npos)
    {
        tokens.push_back(text.substr(start, end - start));
        start = text.find_first_not_of(delims, end);
    }
    if(start != std::string::npos)
        tokens.push_back(text.substr(start));

    return tokens;
}


String addCrc(const String &s, uint16_t crc) {
	char buf[1024];
	int l = hex2bin(s.c_str(), buf, sizeof(buf));
	crc = crc16heater_byte(crc, buf, l);
	return s + strfmt("%04x", (int)crc).c_str();
}

void sendHex(Stream &s, const char *out) {
	char buf[1024];
	int l = hex2bin(out, buf, sizeof(buf)); 
	s.write((uint8_t *)buf, l);
	Serial.printf(">> %04d: %s\n", millis() % 10000, out);
}

void setup() {
	esp_task_wdt_init(40, true);
	esp_task_wdt_add(NULL);
	Serial2.begin(4800, SERIAL_8N1, pins.rxHeater, pins.txHeater, false);
	Serial2.setTimeout(1);
	Serial1.begin(4800, SERIAL_8N1, pins.rxDisplay, -1, false);
	Serial1.setTimeout(1);
}

SerialChunker sc1(Serial1);
SerialChunker sc2(Serial2);

uint32_t lastRec; 

int state = 0, errCount = 0, resetCount = 0, pktCount = 0;
EggTimer sec(1000), minute(60000), min2(60*1000*2); 

int cmdTemp = 0x25; 
void loop() {
	esp_task_wdt_reset();
	j.run();

	if (millis() > 60 * 60 * 1000) { // reboot every hour to keep OTA working? 
		ESP.restart();
	}
	if (sec.tick()) {
		digitalWrite(pins.led, !digitalRead(pins.led));

		if (0) { 
			// TMP test crc code 
			String pkt("fb1b00aaffffff0100");
			addCrc(pkt, 0xfb00);
			OUT("out", pkt + " CRC should be 616f");

			pkt = String("fa1b1209c27f8e1400000000010010");
			addCrc(pkt, 0xfa00);
			OUT("out", pkt + " CRC should be 1b71");
		}
	}
	
	//fb1b0400230a2801005365//
	// just press some buttons every minute to see if it prevents En errors	
	if (0 && /*it doesn't*/ minute.tick()) {  
		msgQueue.add("fb1b0400230a280100b846", 3);  // set to 35 deg
		msgQueue.add("fb1b0400300a28010052ce", 3);  // set to 48 deg
	}

	sc1.check([](const char *b, int l, int t) {
		char hexbuf[1024];
		bin2hex(b, l, hexbuf, sizeof(hexbuf));

		const char *m = msgQueue.get();
		if (m != NULL) { 
			sendHex(Serial2, m);
			OUT("QUEUE: %s", m);
		} else { 
			//sendHex(Serial2, "fb1b00aaffffff0100616f");
			sendHex(Serial2, hexbuf);
		}

		OUT("S1 %04d: %s", t % 10000, hexbuf);

		if (strstr(hexbuf, "fb1b0400") == hexbuf) { 
			// button push
			OUT("BUTTON PUSH %s", hexbuf);
		}
	});

	sc2.check([](const char *b, int l, int t) {
		char hexbuf[1024];
		bin2hex(b, l, hexbuf, sizeof(hexbuf));
		std::string s = strfmt("\t\t\t\t\tS2 %04d: %s", t % 10000, hexbuf);
		OUT(s.c_str());
	
		if (strstr(hexbuf, "fa1b100c15") == hexbuf || // Error, shut down 
			strstr(hexbuf, "fa1b100c35") == hexbuf || // Error, shutdown in progress
			strstr(hexbuf, "fa1b100c1004") == hexbuf //   heater off with water flow 
			) { 
			OUT("ERROR #%d PACKET %s", errCount + 1, hexbuf);

			if (errCount++ > 20) {
				msgQueue.clear();
				msgQueue.add("fb1b00aaffffff0000525e", 10); // off and happy
				msgQueue.add("fb1b0301ffffff01009b67", 3);  // button push to turn on
				msgQueue.add("fb1b00aaffffff0100616f", 20); // on happy 
				msgQueue.add("fb1b0300ffffff0000edf6", 2);  // button push to turn off
				msgQueue.add("fb1b02aaffffff000032bd", 7);  // turning off
				msgQueue.add("fb1b00aaffffff0000525e", 10); // off and happy
				msgQueue.add("fb1b0301ffffff01009b67", 3);  // button push to turn on
				// Have to send a temp command at least 200 times to make it stick
				cmdTemp = 37; 
				msgQueue.add(addCrc(Sfmt("fb1b0400%02x0a280100", cmdTemp), 0xfb00), 220); // set temp to 35
				min2.reset();
				errCount = 0;
				resetCount++;
			}
		}
		OUT("EC %d PKT %d RESETS %d QSIZE %d TEMP %d", errCount, pktCount++, resetCount,
			msgQueue.v.size(), cmdTemp);
	});
	if (min2.tick()) { 
		if (cmdTemp < 55) {
			cmdTemp++;
			msgQueue.add(addCrc(Sfmt("fb1b0400%02x0a280100", cmdTemp), 0xfb00), 220); 
		}
		OUT("Set temp to %d", cmdTemp);
	}

	delay(10);
}

