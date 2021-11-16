/*
 * main.h
 *
 *  Created on: 16.11.2021
 *      Author: ToMe25
 */

#ifndef SRC_MAIN_H_
#define SRC_MAIN_H_

#include <ESPAsyncWebServer.h>
#include <DHT.h>

// includes the content of the file "wifissid.txt" in the project root.
// Make sure this file doesn't end with an empty line.
extern const char WIFI_SSID[] asm("_binary_wifissid_txt_start");
// includes the content of the file "wifipass.txt" in the project root.
// Make sure this file doesn't end with an empty line.
extern const char WIFI_PASS[] asm("_binary_wifipass_txt_start");

extern const char INDEX_HTML[] asm("_binary_src_html_index_html_start");
extern const char MAIN_CSS[] asm("_binary_src_html_main_css_start");
extern const char NOT_FOUND_HTML[] asm("_binary_src_html_not_found_html_start");

static IPAddress localhost;
static IPv6Address localhost_ipv6;

static AsyncWebServer server(80);

static const uint8_t DHT_TYPE = DHT22;
static const uint8_t DHT_PIN = 5;

static DHT dht(DHT_PIN, DHT_TYPE);

static float temperature;
static float humidity;

std::string command;

uint loop_iterations = 0;

bool ipv6_enabled;

#endif /* SRC_MAIN_H_ */
