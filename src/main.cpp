/*
 * main.h
 *
 *  Created on: 06.11.2020
 *
 * Copyright (C) 2020-2021 ToMe25.
 * This project is licensed under the MIT License.
 * The MIT license can be found in the project root and at https://opensource.org/licenses/MIT.
 */

#include "main.h"
#include "webhandler.h"
#include "prometheus.h"
#include "mqtt.h"
#include <iomanip>
#include <sstream>
#if ENABLE_ARDUINO_OTA == 1
#include <ArduinoOTA.h>
#endif
#if defined(ESP8266)
#include <dhcpserver.h>
#endif
#include "fallback_log.h"

IPAddress localhost;
#ifdef ESP32
IPv6Address localhost_ipv6;
#endif
#if SENSOR_TYPE == SENSOR_TYPE_DHT
DHT dht(SENSOR_PIN, DHT_TYPE);
#elif SENSOR_TYPE == SENSOR_TYPE_DALLAS
OneWire wire(SENSOR_PIN);
DallasTemperature sensors(&wire);
#endif
float temperature = NAN;
float humidity = NAN;
uint64_t last_measurement = 0;
std::string command;
uint8_t loop_iterations = 0;
uint64_t start_ms = 0;

void setup() {
	start_ms = millis();
	Serial.begin(115200);

#if SENSOR_TYPE == SENSOR_TYPE_DHT
	dht.begin();
#elif SENSOR_TYPE == SENSOR_TYPE_DALLAS
	sensors.begin();
#endif

	setupWiFi();
#if ENABLE_ARDUINO_OTA == 1
	setupOTA();
#endif

	web::setup();
	prom::setup();
	mqtt::setup();

#if ENABLE_DEEP_SLEEP_MODE == 1
	measure();

	printTemperature(Serial, temperature);
	Serial.print("Humidity: ");
	Serial.print(getHumidity().c_str());
	if (!isnan(humidity)) {
		Serial.println('%');
	} else {
		Serial.println();
	}

	if (WiFi.waitForConnectResult() == WL_CONNECTED) {
#if ENABLE_PROMETHEUS_PUSH == 1
		prom::pushMetrics();
#endif
#if ENABLE_MQTT_PUBLISH == 1
		mqtt::publishMeasurements();
#endif
	} else {
		log_e("Failed to connect to WiFi!");
	}

	WiFi.disconnect(1);

#ifdef ESP32
	esp_sleep_enable_timer_wakeup(DEEP_SLEEP_MODE_MEASUREMENT_INTERVAL * 1000000 - (micros() - start_ms * 1000));
	esp_deep_sleep_start();
#elif defined(ESP8266)
	ESP.deepSleep(DEEP_SLEEP_MODE_MEASUREMENT_INTERVAL * 1000000 - (micros() - start_ms * 1000));
#endif
#endif /* ENABLE_DEEP_SLEEP_MODE */
}

void setupWiFi() {
	WiFi.mode(WIFI_STA);
#if ENABLE_DEEP_SLEEP_MODE != 1
	WiFi.disconnect(1);
#endif
	WiFi.onEvent(onWiFiEvent);

	if (STATIC_IP != IPADDR_ANY || GATEWAY != IPADDR_ANY || SUBNET != IPADDR_ANY) {
		if (!WiFi.config(STATIC_IP, GATEWAY, SUBNET)) {
			log_e("Configuring WiFi failed!");
			return;
		}

		localhost = STATIC_IP;
	}

	WiFi.begin(WIFI_SSID, WIFI_PASS);

#ifdef ESP8266
	dhcps_stop();
#endif
}

#if ENABLE_ARDUINO_OTA == 1
void setupOTA() {
	ArduinoOTA.setHostname(HOSTNAME);
#ifdef ARDUINO_OTA_PORT
	ArduinoOTA.setPort(ARDUINO_OTA_PORT);
#endif
	ArduinoOTA.setPassword(OTA_PASS);

	ArduinoOTA.onStart([]() {
		Serial.println("Start updating sketch.");
	});

	ArduinoOTA.onProgress([](uint progress, uint total) {
		Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
	});

	ArduinoOTA.onEnd([]() {
		Serial.println("\nUpdate Done.");
	});

	ArduinoOTA.onError([](ota_error_t error) {
		Serial.printf("OTA Error[%u]: ", error);
		if (error == OTA_AUTH_ERROR) {
			Serial.println("Auth Failed.");
		} else if (error == OTA_BEGIN_ERROR) {
			Serial.println("Begin Failed.");
		} else if (error == OTA_CONNECT_ERROR) {
			Serial.println("Connect Failed.");
		} else if (error == OTA_RECEIVE_ERROR) {
			Serial.println("Receive Failed.");
		} else if (error == OTA_END_ERROR) {
			Serial.println("End Failed.");
		}
	});

	ArduinoOTA.begin();
}
#endif /* ENABLE_ARDUINO_OTA */

#ifdef ESP32
void onWiFiEvent(WiFiEventId_t id, WiFiEventInfo_t info) {
	switch (id) {
	case ARDUINO_EVENT_WIFI_STA_START:
		WiFi.setHostname(HOSTNAME);
		break;
	case ARDUINO_EVENT_WIFI_STA_CONNECTED:
		WiFi.enableIpV6();

		if (STATIC_IP != IPADDR_ANY) {
			log_i("WiFi ready %lums after start.", millis() - start_ms);
			Serial.print("Using STA IP ");
			Serial.println(localhost = WiFi.localIP());
			web::connect();
			prom::connect();
			mqtt::connect();
		}
		break;
	case ARDUINO_EVENT_WIFI_STA_GOT_IP6:
		Serial.print("Using STA IPv6 ");
		Serial.println(localhost_ipv6 = WiFi.localIPv6());
		break;
	case ARDUINO_EVENT_WIFI_STA_GOT_IP:
#if CORE_DEBUG_LEVEL == 5
		delay(10);// if not doing this the additional logging causes the next log entry to not work.
#endif
		log_i("WiFi ready %lums after start.", millis() - start_ms);
		Serial.print("Using STA IP ");
		Serial.println(localhost = WiFi.localIP());
		web::connect();
		prom::connect();
		mqtt::connect();
		break;
	case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
		WiFi.reconnect();
		break;
	case ARDUINO_EVENT_WIFI_SCAN_DONE:
		Serial.println("WiFi scan results: ");
		Serial.print("Found ");
		Serial.print((uint16_t) info.wifi_scan_done.number);
		Serial.println(" WiFi networks.");
		for (uint8_t i = 0; i < info.wifi_scan_done.number; i++) {
			String SSID;
			uint8_t encryptionType;
			int32_t RSSI;
			uint8_t *BSSID;
			int32_t channel;
			WiFi.getNetworkInfo(i, SSID, encryptionType, RSSI, BSSID, channel);
			Serial.print("network ");
			Serial.print(i + 1);
			Serial.print(": ssid = ");
			Serial.print(SSID);
			Serial.print(", rssi = ");
			Serial.print(RSSI);
			Serial.print(", channel = ");
			Serial.print(channel);
			Serial.print(", encryptionType = ");
			Serial.println(encryptionType);
		}
		break;
	default:
		break;
	}
}
#elif defined(ESP8266)
void onWiFiEvent(WiFiEvent_t id) {
	switch (id) {
	case WIFI_EVENT_STAMODE_GOT_IP:
		log_i("WiFi ready %lums after start.", (long unsigned int ) (millis() - start_ms));
		Serial.print("Using STA IP ");
		Serial.println(localhost = WiFi.localIP());
		web::connect();
		prom::connect();
		mqtt::connect();
		break;
	case WIFI_EVENT_STAMODE_DISCONNECTED:
		WiFi.reconnect();
		break;
	default:
		break;
	}
}
#endif

void loop() {
	uint64_t start = millis();

	if (loop_iterations % 4 == 0) {
		measure();

		if (loop_iterations % 20 == 0 && millis() - last_measurement < 10000) {
			printTemperature(Serial, temperature);
			if (!isnan(humidity)) {
				Serial.print("Humidity: ");
				Serial.print(getHumidity().c_str());
				Serial.println('%');
			}
		}
	}

	uint available = Serial.available();
	if (available > 0) {
		char input[available];
		Serial.readBytes(input, available);

		for (char c : input) {
			if (c == '\b') {
				Serial.print("\b \b");
				if (!command.empty()) {
					command.pop_back();
				}
			} else if (c == '\n') {
				if (!command.empty() && !handle_serial_input(command)) {
					Serial.println();
					Serial.print("Unknown Command: ");
					Serial.println(command.c_str());
					Serial.println("Use \"help\" to get a list of valid commands.");
				}
				command = "";
			} else if (!iscntrl(c)) {
				Serial.write(c);
				command += c;
			}
		}

		if (handle_serial_input(command)) {
			command = "";
		}
	}

	if (loop_iterations == 200) {
		loop_iterations = 0;
	}

#if ENABLE_ARDUINO_OTA == 1
	ArduinoOTA.handle();
#endif

	web::loop();
	prom::loop();
	mqtt::loop();

	loop_iterations++;
	uint64_t end = millis();
	delay(max(0, 500 - int16_t(end - start)));
}

bool handle_serial_input(const std::string &input) {
	if (input == "temperature" || input == "temp") {
		Serial.println();
		printTemperature(Serial, temperature);
		return true;
	} else if (input == "humidity") {
		Serial.println();
		Serial.print("Relative humidity: ");
		Serial.print(getHumidity().c_str());
		if (!isnan(humidity)) {
			Serial.println('%');
		} else {
			Serial.println();
		}
		return true;
	} else if (input == "ip") {
		Serial.println();
		Serial.println("IP Address: ");
#ifdef ESP32
		Serial.print("IPv6: ");
		Serial.println(localhost_ipv6);
#endif
		Serial.print("IPv4: ");
		Serial.println(localhost);
		return true;
	} else if (input == "scan") {
		Serial.println();
#ifdef ESP32
		Serial.println("Starting WiFi scan...");
		WiFi.scanNetworks(true, true);
#elif defined(ESP8266)
		Serial.println("WiFi scanning is not currently supported on ESP8266 hardware.");
#endif
		return true;
	} else if (input == "help") {
		Serial.println();
		Serial.println("ESP-WiFi-Thermometer help:");
		Serial.println("temperature (or temp): Prints the last measured temperature in °C and °F.");
		Serial.println("humidity:              Prints the relative humidity in %.");
		Serial.println("ip:                    Prints the current IPv4 and IPv6 address of this device.");
#ifdef ESP32
		Serial.println("scan:                  Scans for WiFi networks in the area and prints the result.");
#endif
		Serial.println("help:                  Prints this help text.");
		return true;
	} else {
		return false;
	}
}

void measure() {
#if SENSOR_TYPE == SENSOR_TYPE_DHT
	float temp = dht.readTemperature();
	if (!isnan(temp)) {
		temperature = temp;
	}

	float humid = dht.readHumidity();
	if (!isnan(humid)) {
		humidity = humid;
	}

	if (!isnan(temp) && !isnan(humid)) {
		last_measurement = millis();
	}
#elif SENSOR_TYPE == SENSOR_TYPE_DALLAS
	if (sensors.getDeviceCount() == 0) {
		sensors.begin();
	}
	sensors.requestTemperaturesByIndex(0);
	float temp = sensors.getTempCByIndex(0);
	if (temp != DEVICE_DISCONNECTED_C) {
		temperature = temp;
		last_measurement = millis();
	}
#endif
}

std::string getTemperature() {
	if (isnan(temperature)) {
		return "Unknown";
	}
	std::ostringstream converter;
	converter << std::setprecision(temperature > 10 ? 4 : 3) << temperature;
	return converter.str();
}

std::string getHumidity() {
	if (isnan(humidity)) {
		return "Unknown";
	}
	std::ostringstream converter;
	converter << std::setprecision(humidity > 10 ? 4 : 3) << humidity;
	return converter.str();
}

std::string getTimeSinceMeasurement() {
	std::ostringstream stream;
	uint64_t now = millis();
	stream << std::internal << std::setfill('0') << std::setw(2);
	stream << (now - last_measurement) / 3600000 % 24;
	stream << ':';
	stream << std::internal << std::setfill('0') << std::setw(2);
	stream << (now - last_measurement) / 60000 % 60;
	stream << ':';
	stream << std::internal << std::setfill('0') << std::setw(2);
	stream << (now - last_measurement) / 1000 % 60;
	stream << '.';
	stream << std::internal << std::setfill('0') << std::setw(3);
	stream << (now - last_measurement) % 1000;
	return stream.str();
}

void printTemperature(Print &out, const float temp) {
	out.print("Temperature: ");
	if (isnan(temp)) {
		out.println("Unknown");
	} else {
		std::ostringstream converter;
		converter << std::setprecision(temp > 10 ? 4 : 3) << temp;
		out.print(converter.str().c_str());
		out.print("°C, ");
		converter.str("");
		converter.clear();
		const float tempF = celsiusToFahrenheit(temp);
		converter << std::setprecision(tempF > 10 ? 4 : 3) << tempF;
		out.print(converter.str().c_str());
		out.println("°F");
	}
}

float celsiusToFahrenheit(const float celsius) {
	return celsius * 1.8 + 32;
}
