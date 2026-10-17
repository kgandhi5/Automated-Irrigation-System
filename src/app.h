// Application-wide state, owned by main.cpp and shared with the console.
#pragma once

#include <irrigation_core.h>

extern irr::Settings g_settings;
extern irr::PumpBank g_pumps;
extern irr::Controller g_controller;
extern irr::SensorChannel g_sensors[irr::kZones];
extern irr::Reading g_readings[irr::kZones];
extern irr::Scheduler g_scheduler;
extern irr::UptimeClock g_uptime;
extern uint16_t g_loopMaxUs;
extern bool g_settingsFromEeprom;
