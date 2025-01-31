/*
 * prometheus.cpp
 *
 *  Created on: 25.12.2021
 *
 * Copyright (C) 2021 ToMe25.
 * This project is licensed under the MIT License.
 * The MIT license can be found in the project root and at https://opensource.org/licenses/MIT.
 */

#include "prometheus.h"
#if ENABLE_DEEP_SLEEP_MODE == 1 || ENABLE_PROMETHEUS_PUSH == 1
#include "main.h"
#endif
#include "sensor_handler.h"
#include "generated/esptherm_version.h"
#include <iomanip>
#include <sstream>
#include <fallback_log.h>

#if ENABLE_WEB_SERVER == 1 && (ENABLE_PROMETHEUS_PUSH == 1 || ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1)
std::map<String, std::map<std::pair<WebRequestMethod, uint16_t>, uint64_t>> prom::http_requests_total;
#endif
#if ENABLE_PROMETHEUS_PUSH == 1 && ENABLE_DEEP_SLEEP_MODE != 1
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
String prom::getMetrics(const bool openmetrics) {
#if ENABLE_WEB_SERVER == 1
	// First determine the sum of all called path lengths.
	size_t uri_len_sum = 0;
	for (const std::pair<String,
			std::map<std::pair<WebRequestMethod, uint16_t>, uint64_t>> &uri_stats : http_requests_total) {
		uri_len_sum += uri_stats.first.length();
	}
#endif

#if defined(ESP32) || defined(ESP8266)
	const char *SDK_VERSION = ESP.getSdkVersion();
#else
	const char *SDK_VERSION = "unknown";
#endif
	const size_t SDK_VERSION_LEN = strlen(SDK_VERSION);

	// The temperature should never be more than three digits before and after the dot.
	const size_t temp_max_len = 99 + 43 + PROMETHEUS_NAMESPACE_LEN * 3
			+ (openmetrics ? 45 + PROMETHEUS_NAMESPACE_LEN : 0) + 38;
	// The relative humidity should be three digits before and after the dot at most.
	const size_t humidity_max_len = 94 + 40 + PROMETHEUS_NAMESPACE_LEN * 3
			+ (openmetrics ? 42 + PROMETHEUS_NAMESPACE_LEN : 0) + 35;
#ifdef ESP32
	// A 32 bit unsigned int has 10 digits at most, plus four characters because of the way the number will be formatted.
	const size_t heap_max_len = 71 + 32 + (openmetrics ? 32 : 0) + 34;
#else
	const size_t heap_max_len = 0;
#endif
	// Assume that the hash is always seven characters long.
	const size_t build_info_max_len = 73 + 25 + PROMETHEUS_NAMESPACE_LEN * 3
			+ (openmetrics ? -1 : 0) + 104 + MCU_TYPE_LEN + ARDUINO_VERSION_LEN + SDK_VERSION_LEN + CPP_VERSION_LEN;
#if ENABLE_WEB_SERVER == 1
	// An integer is assumed to be at most 20 digits, plus four characters because of the way they are formatted.
	const size_t web_requests_total_max_len = 86 + 36
			+ PROMETHEUS_NAMESPACE_LEN * 2
			+ (83 + PROMETHEUS_NAMESPACE_LEN) * getRequestCounts()
			+ uri_len_sum;
#else
	const size_t web_requests_total_max_len = 0;
#endif
	const size_t eof_max_len = (openmetrics ? 5 : 0);

	// The added lengths of all the lines.
	const size_t max_len = temp_max_len + humidity_max_len + heap_max_len
			+ build_info_max_len + web_requests_total_max_len + eof_max_len;

	char *buffer = new char[max_len + 1];

	// Write sensor metrics.
	size_t len = writeMetric(buffer, PROMETHEUS_NAMESPACE,
			"external_temperature", "celsius",
			"The current measured external temperature in degrees celsius.",
			"gauge", (double) sensors::SENSOR_HANDLER.getTemperature(),
			openmetrics);
	len += writeMetric(buffer + len, PROMETHEUS_NAMESPACE, "external_humidity",
			"percent",
			"The current measured external relative humidity in percent.",
			"gauge", (double) sensors::SENSOR_HANDLER.getHumidity(),
			openmetrics);

	// From what I could find this seems to be impossible on a ESP8266.
#ifdef ESP32
	const uint64_t used_heap = ESP.getHeapSize() - ESP.getFreeHeap();
	len += writeMetric(buffer + len, "process", "heap", "bytes",
			"The amount of heap used on the ESP in bytes.", "gauge",
			(double) used_heap, openmetrics);
#endif

	len += writeMetricMetadataLine(buffer + len, "HELP", PROMETHEUS_NAMESPACE,
			"build_info", "",
			"A constant 1 with compile time information as labels.");
	if (openmetrics) {
		len += writeMetricMetadataLine(buffer + len, "TYPE",
				PROMETHEUS_NAMESPACE, "build_info", "", "info");
	} else {
		len += writeMetricMetadataLine(buffer + len, "TYPE",
				PROMETHEUS_NAMESPACE, "build_info", "", "gauge");
	}
	strcpy(buffer + len, PROMETHEUS_NAMESPACE);
	len += PROMETHEUS_NAMESPACE_LEN;
	strcpy(buffer + len, "_build_info{esptherm_commit=\"");
	len += 29;
	strcpy(buffer + len, ESPTHERM_COMMIT);
	len += 7;
	strcpy(buffer + len, "\",mcu_type=\"");
	len += 12;
	strcpy(buffer + len, MCU_TYPE);
	len += MCU_TYPE_LEN;
	strcpy(buffer + len, "\",arduino_version=\"");
	len += 19;
	strcpy(buffer + len, ARDUINO_VERSION);
	len += ARDUINO_VERSION_LEN;
	strcpy(buffer + len, "\",sdk_version=\"");
	len += 15;
	strcpy(buffer + len, SDK_VERSION);
	len += SDK_VERSION_LEN;
	strcpy(buffer + len, "\",cpp_std_version=\"");
	len += 19;
	strcpy(buffer + len, CPP_VERSION);
	len += CPP_VERSION_LEN;
	strcpy(buffer + len, "\"} 1\n");
	len += 5;

#if ENABLE_WEB_SERVER == 1
	// Write web server statistics.
	len += writeMetricMetadataLine(buffer + len, "HELP", PROMETHEUS_NAMESPACE,
			"http_requests_total", "",
			"The total number of HTTP requests handled by this server.");
	len += writeMetricMetadataLine(buffer + len, "TYPE", PROMETHEUS_NAMESPACE,
			"http_requests_total", "", "counter");

	for (std::map<String,
			std::map<std::pair<WebRequestMethod, uint16_t>, uint64_t>>::const_iterator uri_stats =
			http_requests_total.cbegin();
			uri_stats != http_requests_total.cend(); uri_stats++) {
		for (std::map<std::pair<WebRequestMethod, uint16_t>, uint64_t>::const_iterator response_stats =
				uri_stats->second.cbegin();
				response_stats != uri_stats->second.cend(); response_stats++) {
			strncpy(buffer + len, PROMETHEUS_NAMESPACE,
					min(max_len - len, PROMETHEUS_NAMESPACE_LEN));
			len += PROMETHEUS_NAMESPACE_LEN;
			strncpy(buffer + len, "_http_requests_total{method=\"",
					min(max_len - len, (size_t) 29));
			len += 29;
			switch (response_stats->first.first) {
			case HTTP_GET:
				strncpy(buffer + len, "get", min(max_len - len, (size_t) 3));
				len += 3;
				break;
			case HTTP_POST:
				strncpy(buffer + len, "post", min(max_len - len, (size_t) 4));
				len += 4;
				break;
			case HTTP_PUT:
				strncpy(buffer + len, "put", min(max_len - len, (size_t) 3));
				len += 3;
				break;
			case HTTP_PATCH:
				strncpy(buffer + len, "patch", min(max_len - len, (size_t) 5));
				len += 5;
				break;
			case HTTP_DELETE:
				strncpy(buffer + len, "delete", min(max_len - len, (size_t) 6));
				len += 6;
				break;
			case HTTP_HEAD:
				strncpy(buffer + len, "head", min(max_len - len, (size_t) 4));
				len += 4;
				break;
			case HTTP_OPTIONS:
				strncpy(buffer + len, "options",
						min(max_len - len, (size_t) 7));
				len += 7;
				break;
			default:
				strncpy(buffer + len, "unknown",
						min(max_len - len, (size_t) 7));
				len += 7;
				log_e("Unknown request method %u for uri \"%s\" in stats map.",
						response_stats->first.first, uri_stats->first.c_str());
				break;
			}

			len += snprintf(buffer + len, max_len - len,
					"\",code=\"%u\",path=\"%s\"} %.3f\n",
					response_stats->first.second, uri_stats->first.c_str(),
					(double) response_stats->second);
			if (len >= max_len) {
				log_e("Metrics generation buffer overflow.");
				break;
			}
		}
	}
#endif /* ENABLE_WEB_SERVER == 1 */
	if (openmetrics) {
		strncpy(buffer + len, "# EOF\n", min(max_len - len, (size_t) 7));
		len += 6;
	}

	String metrics = buffer;
	delete[] buffer;
	return metrics;
}

template<size_t ns_l, size_t nm_l, size_t u_l, size_t dc_l, size_t tp_l>
size_t prom::writeMetric(char *buffer, const char (&metric_namespace)[ns_l],
		const char (&metric_name)[nm_l], const char (&metric_unit)[u_l],
		const char (&metric_description)[dc_l], const char (&metric_type)[tp_l],
		const double value, const bool openmetrics) {
	size_t written = writeMetricMetadataLine(buffer, "HELP", metric_namespace,
			metric_name, metric_unit, metric_description);
	written += writeMetricMetadataLine(buffer + written, "TYPE",
			metric_namespace, metric_name, metric_unit, metric_type);
	if (openmetrics) {
		written += writeMetricMetadataLine(buffer + written, "UNIT",
				metric_namespace, metric_name, metric_unit, metric_unit);
	}

	if (ns_l > 1) {
		strcpy(buffer + written, metric_namespace);
		written += ns_l - 1;
		buffer[written++] = '_';
	}
	strcpy(buffer + written, metric_name);
	written += nm_l - 1;
	if (u_l > 1) {
		buffer[written++] = '_';
		strcpy(buffer + written, metric_unit);
		written += u_l - 1;
	}

	if (!std::isnan(value)) {
		written += sprintf(buffer + written, " %.3f\n", value);
	} else {
		strcpy(buffer + written, " NAN\n");
		written += 5;
	}
	buffer[written] = 0;

	return written;
}

template<size_t fnm_l, size_t ns_l, size_t nm_l, size_t u_l, size_t vl_l>
size_t prom::writeMetricMetadataLine(char *buffer,
		const char (&field_name)[fnm_l], const char (&metric_namespace)[ns_l],
		const char (&metric_name)[nm_l], const char (&metric_unit)[u_l],
		const char (&value)[vl_l]) {
	size_t written = 0;
	buffer[written++] = '#';
	buffer[written++] = ' ';
	strcpy(buffer + written, field_name);
	written += fnm_l - 1;
	buffer[written++] = ' ';

	if (ns_l > 1) {
		strcpy(buffer + written, metric_namespace);
		written += ns_l - 1;
		buffer[written++] = '_';
	}
	strcpy(buffer + written, metric_name);
	written += nm_l - 1;
	if (u_l > 1) {
		buffer[written++] = '_';
		strcpy(buffer + written, metric_unit);
		written += u_l - 1;
	}

	buffer[written++] = ' ';
	strcpy(buffer + written, value);
	written += vl_l - 1;
	buffer[written++] = '\n';
	buffer[written] = 0;

	return written;
}

#if ENABLE_WEB_SERVER == 1
size_t prom::getRequestCounts() {
	size_t count = 0;
	for (const std::pair<String,
			std::map<std::pair<WebRequestMethod, uint16_t>, uint64_t>> &uri_stats : http_requests_total) {
		count += uri_stats.second.size();
	}
	return count;
}
#endif
#endif /* ENABLE_PROMETHEUS_PUSH == 1 || ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1 */

#if ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1
web::ResponseData prom::handleMetrics(AsyncWebServerRequest *request) {
	const bool openmetrics = request->hasHeader("Accept")
			&& web::csvHeaderContains(request->header("Accept").c_str(),
					"application/openmetrics-text");

	if (openmetrics) {
		log_d("Client accepts openmetrics.");
	} else {
		log_d("Client doesn't accept openmetrics.");
	}

	const String metrics = getMetrics(openmetrics);
	AsyncWebServerResponse *response =
			request->beginResponse(200,
					(openmetrics ?
							"application/openmetrics-text; version=1.0.0; charset=utf-8" :
							"text/plain; version=0.0.4; charset=utf-8"),
					metrics);
	response->addHeader("Cache-Control", web::CACHE_CONTROL_NOCACHE);
	response->addHeader("Vary", "Accept");
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
	const uint64_t now = (uint64_t) esp_timer_get_time() / 1000;
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
							const uint64_t now = (uint64_t) esp_timer_get_time() / 1000;

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
