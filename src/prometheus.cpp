/*
 * prometheus.cpp
 *
 *  Created on: 25.12.2021
 *      Author: ToMe25
 */

#include "prometheus.h"
#include "main.h"
#include <iomanip>
#include <sstream>
#include "fallback_log.h"

#ifdef ESP32// From what I could find this seems to be impossible on a ESP8266.
uint32_t prom::used_heap = 0;
#endif
#if ENABLE_WEB_SERVER == 1 && (ENABLE_PROMETHEUS_PUSH == 1 || ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1)
std::map<String, std::map<std::pair<WebRequestMethod, uint16_t>, uint64_t>> prom::http_requests_total;
#endif
#if (ENABLE_PROMETHEUS_PUSH == 1 && ENABLE_DEEP_SLEEP_MODE != 1)
uint64_t prom::last_push = 0;
#endif
#if ENABLE_PROMETHEUS_PUSH == 1
AsyncClient *prom::tcpClient = NULL;
std::string prom::push_url;
#endif

void prom::setup() {
#if ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1
	web::registerRequestHandler("/metrics", HTTP_GET, handleMetrics);
#endif
}

void prom::loop() {
#ifdef ESP32// From what I could find this seems to be impossible on a ESP8266.
	used_heap = ESP.getHeapSize() - ESP.getFreeHeap();
#endif

#if ENABLE_PROMETHEUS_PUSH == 1
	pushMetrics();
#endif
}

void prom::connect() {
#if ENABLE_PROMETHEUS_PUSH == 1
	std::ostringstream stream;
	stream << "/metrics/job/";
	if (PROMETHEUS_PUSH_JOB_LEN > 0) {
		stream << PROMETHEUS_PUSH_JOB;
	} else {
		stream << HOSTNAME;
	}
	stream << "/instance/";
	if (PROMETHEUS_PUSH_INSTANCE_LEN > 0) {
		stream << PROMETHEUS_PUSH_INSTANCE;
	} else {
		stream << localhost.toString().c_str();
	}
	stream << "/namespace/";
	if (PROMETHEUS_PUSH_NAMESPACE_LEN > 0) {
		stream << PROMETHEUS_PUSH_NAMESPACE;
	} else {
		stream << PROMETHEUS_NAMESPACE;
	}

	push_url = stream.str();
#endif
}

#if ENABLE_PROMETHEUS_PUSH == 1 || ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1
String prom::getMetrics() {
#if ENABLE_WEB_SERVER == 1
	// First determine the sum of all called path lengths.
	size_t uri_len_sum = 0;
	for (std::pair<String,
			std::map<std::pair<WebRequestMethod, uint16_t>, uint64_t>> uri_stats : http_requests_total) {
		uri_len_sum += uri_stats.first.length();
	}
#endif
	// The added lengths of all the lines.
	// One float is assumed to have three digits before and after the dot.
	// An integer is assumed to be at most 20 digits.
	const size_t max_len = 99 + 43 + 37 + 94 + 40 + 34
			+ PROMETHEUS_NAMESPACE_LEN * 6 +
#ifdef ESP32
			71 + 32 + 40 +
#endif
#if ENABLE_WEB_SERVER == 1
			86 + 36 + PROMETHEUS_NAMESPACE_LEN * 2
			+ (79 + PROMETHEUS_NAMESPACE_LEN) * http_requests_total.size()
			+ uri_len_sum +
#endif
			0;

	char *buffer = new char[max_len + 1];
	buffer[0] = 0;

	// Write temperature metric.
	strcpy(buffer, "# HELP ");
	size_t len = 7;
	strcpy(buffer + len, PROMETHEUS_NAMESPACE);
	len += PROMETHEUS_NAMESPACE_LEN;
	strcpy(buffer + len,
			"_external_temperature_celsius The current measured external temperature in degrees celsius.\n");
	len += 92;
	strcpy(buffer + len, "# TYPE ");
	len += 7;
	strcpy(buffer + len, PROMETHEUS_NAMESPACE);
	len += PROMETHEUS_NAMESPACE_LEN;
	strcpy(buffer + len, "_external_temperature_celsius gauge\n");
	len += 36;
	strcpy(buffer + len, PROMETHEUS_NAMESPACE);
	len += PROMETHEUS_NAMESPACE_LEN;
	strcpy(buffer + len, "_external_temperature_celsius ");
	len += 30;
	if (!isnan(temperature)) {
		len += snprintf(buffer + len, max_len - len, "%.3f\n", temperature);
	} else {
		strcpy(buffer + len, "NAN\n");
		len += 4;
	}

	// Write humidity metric.
	strcpy(buffer + len, "# HELP ");
	len += 7;
	strcpy(buffer + len, PROMETHEUS_NAMESPACE);
	len += PROMETHEUS_NAMESPACE_LEN;
	strcpy(buffer + len,
			"_external_humidity_percent The current measured external relative humidity in percent.\n");
	len += 87;
	strcpy(buffer + len, "# TYPE ");
	len += 7;
	strcpy(buffer + len, PROMETHEUS_NAMESPACE);
	len += PROMETHEUS_NAMESPACE_LEN;
	strcpy(buffer + len, "_external_humidity_percent gauge\n");
	len += 33;
	strcpy(buffer + len, PROMETHEUS_NAMESPACE);
	len += PROMETHEUS_NAMESPACE_LEN;
	strcpy(buffer + len, "_external_humidity_percent ");
	len += 27;
	if (!isnan(humidity)) {
		len += snprintf(buffer + len, max_len - len, "%.3f\n", humidity);
	} else {
		strcpy(buffer + len, "NAN\n");
		len += 4;
	}

	// From what I could find this seems to be impossible on a ESP8266.
#ifdef ESP32
	strcpy(buffer + len,
			"# HELP process_heap_bytes The amount of heap used on the ESP in bytes.\n");
	len += 71;
	strcpy(buffer + len,
			"# TYPE process_heap_bytes gauge\nprocess_heap_bytes ");
	len += 32 + 19;
	len += snprintf(buffer + len, max_len - len, "%u\n", used_heap);
#endif

#if ENABLE_WEB_SERVER == 1
	// Write web server statistics.
	strcpy(buffer + len, "# HELP ");
	len += 7;
	strcpy(buffer + len, PROMETHEUS_NAMESPACE);
	len += PROMETHEUS_NAMESPACE_LEN;
	strcpy(buffer + len,
			"_http_requests_total The total number of HTTP requests handled by this server.\n");
	len += 79;
	strcpy(buffer + len, "# TYPE ");
	len += 7;
	strcpy(buffer + len, PROMETHEUS_NAMESPACE);
	len += PROMETHEUS_NAMESPACE_LEN;
	strcpy(buffer + len, "_http_requests_total counter\n");
	len += 29;

	for (std::pair<String,
			std::map<std::pair<WebRequestMethod, uint16_t>, uint64_t>> uri_stats : http_requests_total) {
		for (std::pair<std::pair<WebRequestMethod, uint16_t>, uint64_t> response_stats : uri_stats.second) {
			strcpy(buffer + len, PROMETHEUS_NAMESPACE);
			len += PROMETHEUS_NAMESPACE_LEN;
			strcpy(buffer + len, "_http_requests_total{method=\"");
			len += 29;
			switch (response_stats.first.first) {
			case HTTP_GET:
				strcpy(buffer + len, "get");
				len += 3;
				break;
			case HTTP_POST:
				strcpy(buffer + len, "post");
				len += 4;
				break;
			case HTTP_PUT:
				strcpy(buffer + len, "put");
				len += 3;
				break;
			case HTTP_PATCH:
				strcpy(buffer + len, "patch");
				len += 5;
				break;
			case HTTP_DELETE:
				strcpy(buffer + len, "delete");
				len += 6;
				break;
			case HTTP_HEAD:
				strcpy(buffer + len, "head");
				len += 4;
				break;
			case HTTP_OPTIONS:
				strcpy(buffer + len, "options");
				len += 7;
				break;
			default:
				strcpy(buffer + len, "unknown");
				len += 7;
				log_e("Unknown request method %u for uri \"%s\" in stats map.",
						response_stats.first.first, uri_stats.first.c_str());
				break;
			}

			len += snprintf(buffer + len, max_len - len,
					"\",code=\"%u\",path=\"%s\"} %llu\n",
					response_stats.first.second, uri_stats.first.c_str(),
					response_stats.second);
			if (len >= max_len) {
				log_e("Metrics generation buffer overflow.");
				break;
			}
		}
	}
#endif

	String metrics = buffer;
	delete[] buffer;
	return metrics;
}
#endif /* ENABLE_PROMETHEUS_PUSH == 1 || ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1 */

#if ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1
web::ResponseData prom::handleMetrics(AsyncWebServerRequest *request) {
	const String metrics = getMetrics();
	AsyncWebServerResponse *response = request->beginResponse(200, "text/plain",
			metrics);
	response->addHeader("Cache-Control", "no-cache");
	return web::ResponseData(response, metrics.length(), 200);
}
#endif

#if ENABLE_PROMETHEUS_PUSH == 1
void prom::pushMetrics() {
	if (!WiFi.isConnected()) {
		delay(20);
		return;
	}

	if (tcpClient) { // Don't try pushing if tcpClient isn't NULL.
		if (tcpClient->connected() || tcpClient->connecting()) {
			tcpClient->close(true);
		}
		delay(20);
		return;
	}

#if ENABLE_DEEP_SLEEP_MODE != 1
	uint64_t now = millis();
	if (now - last_push >= PROMETHEUS_PUSH_INTERVAL * 1000) {
#endif
		tcpClient = new AsyncClient();

		if (!tcpClient) {
			log_e("Failed to allocate Async TCP Client!");
			return;
		}

		tcpClient->setAckTimeout(PROMETHEUS_PUSH_INTERVAL * 0.75);
		tcpClient->setRxTimeout(PROMETHEUS_PUSH_INTERVAL * 0.75);
		tcpClient->onError([](void *arg, AsyncClient *cli, int error) {
			log_e("Connecting to the metrics server failed!");
			log_e("Connection Error: %d", error);
			if (tcpClient != NULL) {
				tcpClient = NULL;
				delete cli;
			}
		}, NULL);

		tcpClient->onConnect([](void *arg, AsyncClient *cli) {

			cli->onDisconnect([](void *arg, AsyncClient *c) {
				if (tcpClient != NULL) {
					tcpClient = NULL;
					log_e("Connection to prometheus pushgateway server was closed while reading or writing.");
					delete c;
				}
			}, NULL);

			std::shared_ptr<size_t> read = std::make_shared<size_t>(0);
			cli->onData([read](void *arg, AsyncClient *c, void *data, size_t len) mutable {
				uint8_t *d = (uint8_t*) data;

				for (size_t i = 0; i < len; i++) {
					if (*read > 8 && isDigit(d[i]) && len > i + 2) {
						char status_code[4] { 0 };
						for (uint8_t j = 0; j < 3; j++) {
							status_code[j] = d[i + j];
						}

						uint32_t code = atoi(status_code);
						if (code == 200) {
#if ENABLE_DEEP_SLEEP_MODE != 1
							uint64_t now = millis();

							if (now - last_push >= (PROMETHEUS_PUSH_INTERVAL + 10) * 1000) {
								log_i("Successfully pushed again after %lums.", now - last_push);
							}

							last_push = now;
#endif
						} else {
							log_w("Received http status code %d when trying to push metrics.", code);
						}

						if (tcpClient != NULL) {
							tcpClient = NULL;
							if (c->connected()) {
								c->close(true);
							}
							delete c;
						}
						return;
					}
					(*read)++;
				}
			}, NULL);

			cli->write("POST ");
			cli->write(push_url.c_str());
			cli->write(" HTTP/1.0\r\nHost: ");
			cli->write(PROMETHEUS_PUSH_ADDR);
			cli->write("\r\n");
			String metrics = getMetrics();
			cli->write("Content-Type: application/x-www-form-urlencoded\r\n");
			cli->write("Content-Length: ");
			std::ostringstream converter;
			converter << metrics.length();
			cli->write(converter.str().c_str());
			cli->write("\r\n\r\n");
			cli->write(metrics.c_str());
			cli->write("\r\n\r\n");
		}, NULL);

		if (!tcpClient->connect(PROMETHEUS_PUSH_ADDR, PROMETHEUS_PUSH_PORT)) {
			log_e("Connecting to the metrics server failed!");
			if (tcpClient != NULL) {
				AsyncClient *cli = tcpClient;
				tcpClient = NULL;
				delete cli;
			}
		}

#if ENABLE_DEEP_SLEEP_MODE == 1
		while (tcpClient != NULL) {
			delay(10);
		}
#else
	}
#endif
}
#endif /* ENABLE_PROMETHEUS_PUSH == 1 */
