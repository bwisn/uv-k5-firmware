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

#include <string.h>
#include "bitmaps.h"
#include "driver/st7565.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/rssi.h"
#include "ui/ui.h"

static void Render(uint8_t RssiLevel, uint8_t VFO)
{
    if (gCurrentFunction == FUNCTION_TRANSMIT || gScreenToDisplay != DISPLAY_MAIN) {
        return;
    }

    uint8_t *pLine = (VFO == 0) ? gFrameBuffer[2] : gFrameBuffer[6];
    uint8_t Line = (VFO == 0) ? 3 : 7;
    bool bIsClearMode;

    memset(pLine, 0, 23);

    if (RssiLevel == 0) {
        bIsClearMode = true;
        pLine = NULL;
    } else {
        static const uint8_t *antennaLevels[] = {
            BITMAP_AntennaLevel1, BITMAP_AntennaLevel2, BITMAP_AntennaLevel3,
            BITMAP_AntennaLevel4, BITMAP_AntennaLevel5, BITMAP_AntennaLevel6};

        memcpy(pLine, BITMAP_Antenna, 5);

        for (uint8_t i = 0; i < RssiLevel && i < sizeof(antennaLevels) / sizeof(antennaLevels[0]); ++i) {
            memcpy(pLine + 5 + (3 * i), antennaLevels[i], 3);
        }

        bIsClearMode = false;
    }

    ST7565_DrawLine(0, Line, 23, pLine, bIsClearMode);
}

void UI_UpdateRSSI(uint16_t RSSI)
{
	uint8_t Level;

	if (RSSI >= gEEPROM_RSSI_CALIB[gRxInfo->Band][3]) {
		Level = 6;
	} else if (RSSI >= gEEPROM_RSSI_CALIB[gRxInfo->Band][2]) {
		Level = 4;
	} else if (RSSI >= gEEPROM_RSSI_CALIB[gRxInfo->Band][1]) {
		Level = 2;
	} else if (RSSI >= gEEPROM_RSSI_CALIB[gRxInfo->Band][0]) {
		Level = 1;
	} else {
		Level = 0;
	}

	if (gVFO_RSSI_Level[gEeprom.RX_CHANNEL] != Level) {
		gVFO_RSSI_Level[gEeprom.RX_CHANNEL] = Level;
		Render(Level, gEeprom.RX_CHANNEL);
	}
}
