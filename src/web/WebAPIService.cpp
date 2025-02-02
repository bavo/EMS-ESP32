/*
 * EMS-ESP - https://github.com/emsesp/EMS-ESP
 * Copyright 2020  Paul Derbyshire
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

// SUrlParser from https://github.com/Mad-ness/simple-url-parser

#include "emsesp.h"

using namespace std::placeholders; // for `_1` etc

namespace emsesp {

WebAPIService::WebAPIService(AsyncWebServer * server, SecurityManager * securityManager)
    : _securityManager(securityManager)
    , _apiHandler("/api", std::bind(&WebAPIService::webAPIService_post, this, _1, _2), 256) { // for POSTS, must use 'Content-Type: application/json' in header
    server->on("/api", HTTP_GET, std::bind(&WebAPIService::webAPIService_get, this, _1));     // for GETS
    server->addHandler(&_apiHandler);
}

// HTTP GET
// GET /{device}
// GET /{device}/{entity}
void WebAPIService::webAPIService_get(AsyncWebServerRequest * request) {
    // has no body JSON so create dummy as empty input object
    StaticJsonDocument<EMSESP_JSON_SIZE_SMALL> input_doc;
    JsonObject                                 input = input_doc.to<JsonObject>();
    parse(request, input);
}

// For HTTP POSTS with an optional JSON body
// HTTP_POST | HTTP_PUT | HTTP_PATCH
// POST /{device}[/{hc|id}][/{name}]
void WebAPIService::webAPIService_post(AsyncWebServerRequest * request, JsonVariant & json) {
    // if no body then treat it as a secure GET
    if (not json.is<JsonObject>()) {
        webAPIService_get(request);
        return;
    }

    // extract values from the json. these will be used as default values
    auto && input = json.as<JsonObject>();
    parse(request, input);
}

// parse the URL looking for query or path parameters
// reporting back any errors
void WebAPIService::parse(AsyncWebServerRequest * request, JsonObject & input) {
    // check if the user has admin privileges (token is included and authorized)
    bool is_admin;
    EMSESP::webSettingsService.read([&](WebSettings & settings) {
        Authentication authentication = _securityManager->authenticateRequest(request);
        is_admin                      = settings.notoken_api | AuthenticationPredicates::IS_ADMIN(authentication);
    });

    // output json buffer
    PrettyAsyncJsonResponse * response = new PrettyAsyncJsonResponse(false, EMSESP_JSON_SIZE_XXLARGE_DYN);
    JsonObject                output   = response->getRoot();

    // call command
    uint8_t return_code = Command::process(request->url().c_str(), is_admin, input, output);

    if (return_code != CommandRet::OK) {
        char error[100];
        if (output.size()) {
            snprintf(error, sizeof(error), "Call failed with error: %s (%s)", (const char *)output["message"], Command::return_code_string(return_code).c_str());
        } else {
            snprintf(error, sizeof(error), "Call failed with error code (%s)", Command::return_code_string(return_code).c_str());
        }
        emsesp::EMSESP::logger().err(error);
    } else {
        emsesp::EMSESP::logger().debug(F("API command called successfully"));
        // if there was no json output from the call, default to the output message 'OK'.
        if (!output.size()) {
            output["message"] = "OK";
        }
    }

    // send the json that came back from the command call
    // FAIL, OK, NOT_FOUND, ERROR, NOT_ALLOWED = 400 (bad request), 200 (OK), 400 (not found), 400 (bad request), 401 (unauthorized)
    int ret_codes[5] = {400, 200, 400, 400, 401};
    response->setCode(ret_codes[return_code]);
    response->setLength();
    response->setContentType("application/json");
    request->send(response);

#if defined(EMSESP_STANDALONE)
    Serial.print(COLOR_YELLOW);
    Serial.print("web response code: ");
    Serial.println(ret_codes[return_code]);
    if (output.size()) {
        serializeJsonPretty(output, Serial);
    }
    Serial.println();
    Serial.print(COLOR_RESET);
#endif
}

} // namespace emsesp
