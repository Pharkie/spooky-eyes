#pragma once
// Serial logging over the USB cable: LOG("WIFI", "Connected, IP %s", ip.c_str());

#include <Arduino.h>

#define LOG(tag, fmt, ...) Serial.printf("[%s] " fmt "\n", tag, ##__VA_ARGS__)
