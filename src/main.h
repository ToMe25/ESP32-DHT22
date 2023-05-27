/*
 * main.h
 *
 *  Created on: 16.11.2021
 *
 * Copyright (C) 2021 ToMe25.
 * This project is licensed under the MIT License.
 * The MIT license can be found in the project root and at https://opensource.org/licenses/MIT.
 */

#ifndef SRC_MAIN_H_
#define SRC_MAIN_H_

#include "config.h"
#if ENABLE_WEB_SERVER == 1
#include "uzlib_gzip_wrapper.h"
#include <ESPAsyncWebServer.h>
#endif
// It would be possible to always only include one, but that makes it a pain when switching between them.
// Especially when doing it repeatedly for testing.
#include <DallasTemperature.h>
#include <DHT.h>

// Includes the content of the file "wifissid.txt" in the project root.
// Make sure this file doesn't end with an empty line.
extern const char WIFI_SSID[] asm("_binary_wifissid_txt_start");
// Includes the content of the file "wifipass.txt" in the project root.
// Make sure this file doesn't end with an empty line.
extern const char WIFI_PASS[] asm("_binary_wifipass_txt_start");
// Includes the content of the file "otapass.txt" in the project root.
// Make sure this file doesn't end with an empty line.
extern const char OTA_PASS[] asm("_binary_otapass_txt_start");

#if ENABLE_WEB_SERVER == 1
extern const char INDEX_HTML[] asm("_binary_src_html_index_html_start");
extern const uint8_t MAIN_CSS_START[] asm("_binary_data_gzip_main_css_gz_start");
extern const uint8_t MAIN_CSS_END[] asm("_binary_data_gzip_main_css_gz_end");
extern const uint8_t INDEX_JS_START[] asm("_binary_data_gzip_index_js_gz_start");
extern const uint8_t INDEX_JS_END[] asm("_binary_data_gzip_index_js_gz_end");
extern const char NOT_FOUND_HTML[] asm("_binary_src_html_not_found_html_start");
extern const uint8_t NOT_FOUND_HTML_END[] asm("_binary_data_gzip_not_found_html_gz_end");
extern const uint8_t FAVICON_ICO_GZ_START[] asm("_binary_data_gzip_favicon_ico_gz_start");
extern const uint8_t FAVICON_ICO_GZ_END[] asm("_binary_data_gzip_favicon_ico_gz_end");
extern const uint8_t FAVICON_PNG_GZ_START[] asm("_binary_data_gzip_favicon_png_gz_start");
extern const uint8_t FAVICON_PNG_GZ_END[] asm("_binary_data_gzip_favicon_png_gz_end");
extern const uint8_t FAVICON_SVG_GZ_START[] asm("_binary_data_gzip_favicon_svg_gz_start");
extern const uint8_t FAVICON_SVG_GZ_END[] asm("_binary_data_gzip_favicon_svg_gz_end");

typedef std::function<uint16_t(AsyncWebServerRequest *request)> HTTPRequestHandler;
#endif

// WiFi variables
extern IPAddress localhost;
#ifdef ESP32
extern IPv6Address localhost_ipv6;
#endif

#if ENABLE_WEB_SERVER == 1
// Web Server variables
extern AsyncWebServer server;
#endif

//Sensor variables
#if SENSOR_TYPE == SENSOR_TYPE_DHT
extern DHT dht;
#elif SENSOR_TYPE == SENSOR_TYPE_DALLAS
extern OneWire wire;
extern DallasTemperature sensors;
#endif

extern float temperature;
extern float humidity;
extern uint64_t last_measurement;

// Other variables
extern std::string command;

extern uint8_t loop_iterations;

extern uint64_t start_ms;

// Methods
/**
 * Initializes the program and everything needed by it.
 */
void setup();

/**
 * Initializes everything related to WiFi, and establishes a connection to an WiFi access point, if possible.
 */
void setupWiFi();

#if ENABLE_ARDUINO_OTA == 1
/**
 * Initializes everything required for Arduino OTA.
 */
void setupOTA();
#endif

#if ENABLE_WEB_SERVER == 1
/**
 * Initializes the Web Server and the mDNS entry for the web server.
 */
void setupWebServer();
#endif

#ifdef ESP32
/**
 * The function handling the WiFi events that may occur.
 *
 * @param id	The id of the WiFi event.
 * @param info	Info about the WiFi event.
 */
void onWiFiEvent(WiFiEventId_t id, WiFiEventInfo_t info);
#elif defined(ESP8266)
/**
 * The function handling the WiFi events that may occur.
 *
 * @param id	The id of the WiFi event.
 */
void onWiFiEvent(WiFiEvent_t id);
#endif

/**
 * The core of this program, the method that gets called repeatedly as long as the program runs.
 */
void loop();

/**
 * Responds to serial input by executing actions and printing a response.
 *
 * @return	True if the input string was a valid command.
 */
bool handle_serial_input(const std::string &input);

/**
 * Reads in the sensor measurements and stores them in the correct values.
 */
void measure();

/**
 * Returns the last measured temperature in degrees celsius, rounded to two decimal digits.
 * Or "Unknown" if it is NAN.
 *
 * @return	The last measured temperature.
 */
std::string getTemperature();

/**
 * Returns the last measured relative humidity in percent, rounded to two decimal digits.
 * Or "Unknown" if it is NAN.
 *
 * @return the last measured relative humidity.
 */
std::string getHumidity();

#if ENABLE_WEB_SERVER == 1
/**
 * Process the templates for the index page.
 *
 * @param temp	The template to replace.
 * @return	The value replacing the template.
 */
String processIndexTemplates(const String &temp);

/**
 * The request handler for /data.json.
 * Responds with a json object containing the current temperature and humidity,
 * as well as the time since the last measurement.
 *
 * @param request	The web request to handle.
 * @return	The returned http status code.
 */
uint16_t getJson(AsyncWebServerRequest *request);

/**
 * Returns a string with the time since the last measurement formatted like this "Hour(24):Minute:Second.Millisecond".
 * Currently only used for the web server.
 *
 * @return	A formatted string representing the time since the last measurement.
 */
std::string getTimeSinceMeasurement();

/**
 * A AwsResponseFiller decompressing a file from memory using uzlib.
 *
 * @param decomp	The uzlib decompressing persistent data.
 * @param buffer	The output data to write the decompressed data to.
 * @param max_len	The max number of bytes to write to the output buffer.
 * @param index		The number of bytes already generated for this response.
 * @return	The number of bytes written to the output buffer.
 */
size_t decompressingResponseFiller(const std::shared_ptr<uzlib_gzip_wrapper> decomp,
		uint8_t *buffer, const size_t max_len, const size_t index);

/**
 * The method to be called by the AsyncWebServer to call a request handler.
 * Calls the handler and updates the prometheus web request statistics.
 *
 * @param uri		The uri to be handled by the request handler.
 * @param handler	The request handler to be wrapped by this method.
 * @param request	The request to be handled.
 */
void trackingRequestHandlerWrapper(const char *uri,
		const HTTPRequestHandler handler, AsyncWebServerRequest *request);

/**
 * A web request handler for a compressed static file.
 * If the client accepts gzip compressed files, the file is sent as is.
 * Otherwise it is decompressed on the fly.
 *
 * @param content_type	The content type of the static file.
 * @param start			A pointer to the first byte of the compressed static file.
 * @param end			A pointer to the first byte after the end of the compressed static file.
 * @param request		The request to handle.
 */
uint16_t compressedStaticHandler(const char *content_type, const uint8_t *start,
		const uint8_t *end, AsyncWebServerRequest *request);

/**
 * Registers the given handler for the web server, and increments the web requests counter
 * by one each time it is called.
 *
 * @param uri		The path on which the page can be found.
 * @param method	The HTTP request method for which to register the handler.
 * @param handler	A function responding to AsyncWebServerRequests and returning the response HTTP status code.
 */
void registerRequestHandler(const char *uri, WebRequestMethodComposite method,
		HTTPRequestHandler handler);

/**
 * Registers a request handler that returns the given content type and web page each time it is called.
 * Expects request type get.
 * Also increments the request counter.
 *
 * @param uri			The path on which the page can be found.
 * @param content_type	The content type for the page.
 * @param page			The content for the page to be sent to the client.
 */
void registerStaticHandler(const char *uri, const char *content_type,
		const char *page);

/**
 * Registers a request handler that returns the given content type and web page each time it is called.
 * Also registers the given template processor.
 * Expects request type get.
 * Also increments the request counter.
 *
 * @param uri			The path on which the page can be found.
 * @param content_type	The content type for the page.
 * @param page			The content for the page to be sent to the client.
 */
void registerProcessedStaticHandler(const char *uri, const char *content_type,
		const char *page, const AwsTemplateProcessor processor);

/**
 * Registers a request handler that returns the given content each time it is called.
 * Expects request type get.
 * Also increments the request counter.
 * Expects the content to be a gzip compressed binary.
 *
 * @param uri			The path on which the file can be found.
 * @param content_type	The content type for the file.
 * @param start			The pointer for the start of the file.
 * @param end			The pointer for the end of the file.
 */
void registerCompressedStaticHandler(const char *uri, const char *content_type,
		const uint8_t *start, const uint8_t *end);
#endif /* ENABLE_WEB_SERVER */

/**
 * Print the given temperature in degrees celsius and degrees fahrenheit.
 *
 * @param out	The print object to print to.
 * @param temp	The temperature to print. In degrees celsius.
 */
void printTemperature(Print &out, const float temp);

/**
 * Converts the given temperature from degrees celsius to degrees fahrenheit.
 *
 * @param celsius	The temperature to convert in celsius.
 * @return	The converted temperature in fahrenheit.
 */
float celsiusToFahrenheit(const float celsius);

#endif /* SRC_MAIN_H_ */
