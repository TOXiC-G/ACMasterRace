#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "types.h"

// Initialize HTTP server routes and CORS handlers
void initWebServer(WebServer& server, GlobalState& state);
