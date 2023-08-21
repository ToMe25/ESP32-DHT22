/*
 * AsyncMultitypeFallbackWebHandler.cpp
 *
 *  Created on: Jun 17, 2023
 *      Author: ToMe25
 */

#include <AsyncTrackingFallbackWebHandler.h>
#if ENABLE_WEB_SERVER == 1
#if ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1 || ENABLE_PROMETHEUS_PUSH == 1
#include "prometheus.h"
#endif

/**
 * A method to get the index of the Most Significant Bit set in a number.
 * Necessary because the std log2 function doesn't exist on arm hosts.
 *
 * @param number	The number to get the MSB for.
 * @return	The index of the Most Significant Bit set in the number.
 */
uint8_t get_msb(const uint32_t number) {
	uint8_t idx = 0;
	while (number >> (idx + 1)) {
		idx++;
	}
	return idx;
}

web::AsyncTrackingFallbackWebHandler::AsyncTrackingFallbackWebHandler(
		const String &uri, HTTPFallbackRequestHandler fallback) :
		_uri(uri), _fallbackHandler(fallback), _handlers(
				get_msb(HTTP_ANY) + 1) {

}

web::AsyncTrackingFallbackWebHandler::~AsyncTrackingFallbackWebHandler() {

}

web::HTTPRequestHandler web::AsyncTrackingFallbackWebHandler::_getHandler(const WebRequestMethod method) const {
	return _handlers[get_msb((WebRequestMethodComposite) method)];
}

void web::AsyncTrackingFallbackWebHandler::setHandler(const WebRequestMethodComposite methods, HTTPRequestHandler handler) {
	for (size_t i = 0; i < _handlers.size(); i++) {
		if (methods & (1 << i)) {
			_handlers[i] = handler;
		}
	}
}

void web::AsyncTrackingFallbackWebHandler::setFallbackHandler(HTTPFallbackRequestHandler fallback) {
	_fallbackHandler = fallback;
}

WebRequestMethodComposite web::AsyncTrackingFallbackWebHandler::getHandledMethods() const {
	WebRequestMethodComposite methods = 0;
	for (size_t i = 0; i < _handlers.size(); i++) {
		if (_handlers[i]) {
			methods |= 1 << i;
		}
	}
	return methods;
}

bool web::AsyncTrackingFallbackWebHandler::canHandle(AsyncWebServerRequest *request) {
	if (!_uri.length()) {
		return false;
	}

	if (_uri == request->url() || request->url().startsWith(_uri + '/')) {
		request->addInterestingHeader("ANY");
		return true;
	}
	return false;
}

void web::AsyncTrackingFallbackWebHandler::handleRequest(AsyncWebServerRequest *request) {
	const size_t start = micros();
	ResponseData response(request->beginResponse(500), 0, 500);
	HTTPRequestHandler handler = _getHandler((WebRequestMethod) request->method());
	if (handler) {
		delete response.response;
		response = handler(request);
	} else if (_fallbackHandler) {
		delete response.response;
		response = _fallbackHandler(getHandledMethods(), request);
	} else {
		log_w("The handler for uri \"%s\" didn't have a handler for request type %s, and didn't have a fallback handler.",
				_uri.c_str(), request->methodToString());
	}
#if ENABLE_PROMETHEUS_PUSH == 1 || ENABLE_PROMETHEUS_SCRAPE_SUPPORT == 1
	prom::http_requests_total[request->url()][ {
			(WebRequestMethod) request->method(), response.status_code }]++;
#endif
	const size_t mid = micros();
	request->send(response.response);
	const size_t end = micros();
	log_d("Handling a request to \"%s\" took %luus + %luus.", request->url().c_str(), mid - start, end - mid);
}

bool web::AsyncTrackingFallbackWebHandler::isRequestHandlerTrivial() {
	return false;
}
#endif /* ENABLE_WEB_SERVER == 1 */
