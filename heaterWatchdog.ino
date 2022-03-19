#include "jimlib.h"
//#include <heltec.h>	
#include <PubSubClient.h>
#include <deque>

JimWiFi jw;
EggTimer sec(1000), sec5(5000);

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
	std::deque<std::pair<std::string, int>> v;
	void add(const char *msg, int count) { 
		v.push_back(std::pair<std::string, int>(std::string(msg), count)); 
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
	void reconnect() {
	// Loop until we're reconnected
		if (WiFi.status() != WL_CONNECTED || client.connected()) 
			return;
		
		Serial.print("Attempting MQTT connection...");
		client.setServer("192.168.4.1", 1883);
		client.setCallback(mqttCallback);
		if (client.connect(topicPrefix.c_str())) {
			Serial.println("connected");
			// Once connected, publish an announcement...
			String msg = "hello";
			client.publish((topicPrefix + "/debug").c_str(), msg.c_str());
			// ... and resubscribe
			client.subscribe("heaterin");
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
 } mqtt("192.168.4.1", "heater");

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
  Serial.println();
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


int state = 0, errCount = 0;

void loop() {
	esp_task_wdt_reset();
	jw.run();
	mqtt.run();
	if (sec.tick()) {
		digitalWrite(pins.led, !digitalRead(pins.led));
		//std::string s = strfmt("S1: %d\tS2: %d", sc1.total, sc2.total);
		//Serial.println(s.c_str());
		//jw.udpDebug(s.c_str());
	}

	sc1.check([](const char *b, int l, int t) {
		char hexbuf[2048];
		hexdump(b, l, hexbuf);

		const char *m = msgQueue.get();
		if (m != NULL) { 
			sendHex(Serial2, m);
			std::string s = std::string("QUEUE: ") + m;
			mqtt.publish("out", s.c_str());
		} else { 
			//sendHex(Serial2, "fb1b00aaffffff0100616f");
			sendHex(Serial2, hexbuf);
		}

		std::string s = strfmt("\t\t\t\t\tS1 %04d: %s", t % 10000, hexbuf);
		Serial.println(s.c_str());
		mqtt.publish("out", s.c_str());



		if (strstr(hexbuf, "fb1b0400") == hexbuf) { 
			// button push
			s = strfmt("BUTTON PUSH ") + hexbuf;
			mqtt.dprintf(s.c_str());
		}
	});


	sc2.check([](const char *b, int l, int t) {
		char hexbuf[2048];
		hexdump(b, l, hexbuf);
		std::string s = strfmt("S2 %04d: %s", t % 10000, hexbuf);
		Serial.println(s.c_str());
		mqtt.publish("out", s.c_str());
	
		if (strstr(hexbuf, "fa1b100c15") == hexbuf) {
			if (errCount++ > 20) {
				std::string s = strfmt("ERROR PACKET %s", hexbuf);
				Serial.println(s.c_str());
				mqtt.publish("out", s.c_str());

				msgQueue.add("fb1b00aaffffff0000525e", 10); // off and happy
				msgQueue.add("fb1b0301ffffff01009b67", 3);  // button push to turn on
				msgQueue.add("fb1b00aaffffff0100616f", 20); // on happy 
				msgQueue.add("fb1b0300ffffff0000edf6", 2);  // button push to turn off
				msgQueue.add("fb1b02aaffffff000032bd", 7);  // turning off
				msgQueue.add("fb1b00aaffffff0000525e", 10); // off and happy
				msgQueue.add("fb1b0301ffffff01009b67", 3);  // button push to turn on
				msgQueue.add("fb1b00aaffffff0100616f", 20); // on happy 

				errCount = 0;
			}
		}


		static int pktCount = 0;
		s = strfmt("EC %d PKT %d", errCount, pktCount++);
		Serial.println(s.c_str());
		jw.udpDebug(s.c_str());
		mqtt.publish("out", s.c_str());
	});

	delay(1);
}

