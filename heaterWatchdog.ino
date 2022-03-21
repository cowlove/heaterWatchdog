#include "jimlib.h"
#include "crc16heater.h"
#include "crc16heater.c"
//#include <heltec.h>	
#include <PubSubClient.h>
#include <deque>

JimWiFi jw;

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

class MQTTClient { 
	WiFiClient espClient;
	String topicPrefix, server;
public:
	PubSubClient client;
	MQTTClient(const char *s, const char *t) : server(s), topicPrefix(t), client(espClient) {}
	void publish(const char *suffix, const char *m) { 
		String t = topicPrefix + "/" + suffix;
		client.publish(t.c_str(), m);
	}
	void publish(const char *suffix, const String &m) {
		 publish(suffix, m.c_str()); 
	}
	void reconnect() {
	// Loop until we're reconnected
		if (WiFi.status() != WL_CONNECTED || client.connected()) 
			return;
		
		Serial.print("Attempting MQTT connection...");
		client.setServer(server.c_str(), 1883);
		client.setCallback(mqttCallback);
		if (client.connect(topicPrefix.c_str())) {
			Serial.println("connected");
			// Once connected, publish an announcement...
			String msg = "hello";
			client.publish((topicPrefix + "/debug").c_str(), msg.c_str());
			// ... and resubscribe
			client.subscribe((topicPrefix + "/in").c_str());
			client.setCallback(mqttCallback);
		} else {
			Serial.print("failed, rc=");
			Serial.print(client.state());
		}
	}
	void dprintf(const char *format, ...) { 
		va_list args;
		va_start(args, format);
        char buf[256];
        vsnprintf(buf, sizeof(buf), format, args);
	    va_end(args);
		client.publish((topicPrefix + "/debug").c_str(), buf);
	}
	void run() { 
		reconnect();
	}
 };
 
MQTTClient mqtt("192.168.4.1", "heater");

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  std::string p;
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
	p += (char)payload[i];
  }
  msgQueue.add(p.c_str(), 1);
  mqtt.publish("heater/out", "got mqtt message");
  Serial.println();
}

void bin2hex(const char *in, int len, char *out, int olen) {
	len = min(len, olen / 2); 
	for (int n = 0; n < len; n++) { 
		sprintf(out + 2 * n, "%02x", in[n]);
	}
	out[2 * len] = '\0';
}

int hex2bin(const char *in, char *out, int outlen) { 
        int l = min((int)strlen(in), outlen / 2);
        for (const char *p = in; p < in + l ; p += 2) { 
                char b[3];
                b[0] = p[0];
                b[1] = p[1];
                b[2] = 0;
                int c;
                sscanf(b, "%x", &c);
                *(out++) = c;
        }
        return strlen(in) / 2;
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
	s.write(buf, l);
	Serial.printf(">> %04d: %s\n", millis() % 10000, out);
}

void setup() {
	esp_task_wdt_init(40, true);
	esp_task_wdt_add(NULL);
	//Heltec.begin(true, true, true, true, 915E6);
	pinMode(pins.led, OUTPUT);
	digitalWrite(pins.led, 1);
	Serial.begin(921600, SERIAL_8N1);
	jw.onConnect([](void) { });
	jw.onOTA([](void) {});
	Serial2.begin(4800, SERIAL_8N1, pins.rxHeater, pins.txHeater, false);
	Serial2.setTimeout(1);
	Serial1.begin(4800, SERIAL_8N1, pins.rxDisplay, -1, false);
	Serial1.setTimeout(1);
	Serial.println("Restart");	
}


SerialChunker sc1(Serial1);
SerialChunker sc2(Serial2);

uint32_t lastRec; 

int state = 0, errCount = 0, resetCount = 0, pktCount = 0;
EggTimer sec(1000), minute(60000);

void loop() {
	esp_task_wdt_reset();
	jw.run();
	mqtt.run();

	if (millis() > 60 * 60 * 1000) { // reboot every hour to keep OTA working? 
		ESP.restart();
	}
	if (sec.tick()) {
		digitalWrite(pins.led, !digitalRead(pins.led));

		if (0) { 
			// TMP test crc code 
			String pkt("fb1b00aaffffff0100");
			addCrc(pkt, 0xfb00);
			mqtt.publish("out", pkt + " CRC should be 616f");

			pkt = String("fa1b1209c27f8e1400000000010010");
			addCrc(pkt, 0xfa00);
			mqtt.publish("out", pkt + " CRC should be 1b71");
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
			std::string s = std::string("QUEUE: ") + m;
			mqtt.publish("out", s.c_str());
		} else { 
			//sendHex(Serial2, "fb1b00aaffffff0100616f");
			sendHex(Serial2, hexbuf);
		}

		std::string s = strfmt("S1 %04d: %s", t % 10000, hexbuf);
		Serial.println(s.c_str());
		mqtt.publish("out", s.c_str());

		if (strstr(hexbuf, "fb1b0400") == hexbuf) { 
			// button push
			s = strfmt("BUTTON PUSH ") + hexbuf;
			mqtt.dprintf(s.c_str());
		}
	});

	sc2.check([](const char *b, int l, int t) {
		char hexbuf[1024];
		bin2hex(b, l, hexbuf, sizeof(hexbuf));
		std::string s = strfmt("\t\t\t\t\tS2 %04d: %s", t % 10000, hexbuf);
		Serial.println(s.c_str());
		mqtt.publish("out", s.c_str());
	
		if (strstr(hexbuf, "fa1b100c15") == hexbuf) {
			if (errCount++ > 20) {
				std::string s = strfmt("ERROR PACKET %s", hexbuf);
				Serial.println(s.c_str());
				mqtt.publish("out", s.c_str());
				msgQueue.clear();
				msgQueue.add("fb1b00aaffffff0000525e", 10); // off and happy
				msgQueue.add("fb1b0301ffffff01009b67", 3);  // button push to turn on
				msgQueue.add("fb1b00aaffffff0100616f", 20); // on happy 
				msgQueue.add("fb1b0300ffffff0000edf6", 2);  // button push to turn off
				msgQueue.add("fb1b02aaffffff000032bd", 7);  // turning off
				msgQueue.add("fb1b00aaffffff0000525e", 10); // off and happy
				msgQueue.add("fb1b0301ffffff01009b67", 3);  // button push to turn on
				msgQueue.add(addCrc(Sfmt("fb1b0400%02x0a280100", 0x27), 0xfb00), 3);
				msgQueue.add("fb1b00aaffffff0100616f", 300); // on happy 
				msgQueue.add("fb1b0400300a28010052ce", 3);  // set to 48 deg
				errCount = 0;
				resetCount++;
			}
		}

		s = strfmt("EC %d PKT %d RESETS %d", errCount, pktCount++, resetCount);
		Serial.println(s.c_str());
		//jw.udpDebug(s.c_str());
		mqtt.publish("out", s.c_str());
	});

	delay(1);
}

