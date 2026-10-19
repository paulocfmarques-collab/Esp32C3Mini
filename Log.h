#pragma once

#include <Arduino.h>

#define LOG_INFO(x) Serial.printf("[INFO] %s\n", x)

#define LOG_WARN(x) Serial.printf("[WARN] %s\n", x)

#define LOG_ERROR(x) Serial.printf("[ERRO] %s\n", x)