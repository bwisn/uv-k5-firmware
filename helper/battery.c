/* Copyright 2023 Dual Tachyon
 * https://github.com/DualTachyon
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

#include "battery.h"

#include "driver/backlight.h"
#include "misc.h"
#include "ui/battery.h"
#include "ui/menu.h"
#include "ui/ui.h"

uint16_t gBatteryCalibration[6];
uint16_t gBatteryCurrentVoltage;
uint16_t gBatteryCurrent;
uint16_t gBatteryVoltages[4];
uint16_t gBatteryVoltageAverage;

uint8_t gBatteryDisplayLevel;

bool gChargingWithTypeC;
bool gLowBattery;
bool gLowBatteryBlink;

volatile uint16_t gBatterySave;

void BATTERY_GetReadings(bool bDisplayBatteryLevel)
{
    uint16_t Voltage = (gBatteryVoltages[0] + gBatteryVoltages[1] + gBatteryVoltages[2] + gBatteryVoltages[3]) / 4;
    uint8_t PreviousBatteryLevel = gBatteryDisplayLevel;

    for (gBatteryDisplayLevel = 6; gBatteryDisplayLevel > 0; gBatteryDisplayLevel--) {
        if (Voltage > gBatteryCalibration[gBatteryDisplayLevel - 1]) break;
    }

    gBatteryVoltageAverage = (Voltage * 760) / gBatteryCalibration[3];

    g_20000370 = (gScreenToDisplay == DISPLAY_MENU && gMenuCursor == MENU_VOL) ? 1 : g_20000370;

    gChargingWithTypeC = (gBatteryCurrent >= 501);
    if (gChargingWithTypeC) {
        BACKLIGHT_TurnOn();
        gUpdateStatus = true;
    }

    if (PreviousBatteryLevel != gBatteryDisplayLevel) {
        gLowBattery = (gBatteryDisplayLevel < 2);
        gLowBatteryCountdown = 0;
        if (!gLowBattery && bDisplayBatteryLevel) {
            UI_DisplayBattery(gBatteryDisplayLevel);
        }
    }
}
