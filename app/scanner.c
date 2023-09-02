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

#include "app/generic.h"
#include "app/scanner.h"
#include "audio.h"
#include "frequencies.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

DCS_CodeType_t gCS_ScannedType;
uint8_t gCS_ScannedIndex;

static void SCANNER_Key_DIGITS(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
	if (!bKeyHeld && bKeyPressed) {
		if (gScannerEditState == 1) {
			uint16_t Channel;

			gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
			INPUTBOX_Append(Key);
			gRequestDisplayScreen = DISPLAY_SCANNER;
			if (gInputBoxIndex < 3) {
				gAnotherVoiceID = (VOICE_ID_t)Key;
				return;
			}
			gInputBoxIndex = 0;
			Channel = ((gInputBox[0] * 100) + (gInputBox[1] * 10) + gInputBox[2]) - 1;
			if (IS_MR_CHANNEL(Channel)) {
				gAnotherVoiceID = (VOICE_ID_t)Key;
				gShowChPrefix = RADIO_CheckValidChannel(Channel, false, 0);
				gScanChannel = (uint8_t)Channel;
				return;
			}
		}
		gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
	}
}

static void SCANNER_Key_EXIT(bool bKeyPressed, bool bKeyHeld)
{
	if (!bKeyHeld && bKeyPressed) {
		gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

		switch (gScannerEditState) {
		case 0:
			gRequestDisplayScreen = DISPLAY_MAIN;
			gEeprom.CROSS_BAND_RX_TX = gBackupCROSS_BAND_RX_TX;
			gUpdateStatus = true;
			gFlagStopScan = true;
			gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
			g_2000039B = 1;
			gAnotherVoiceID = VOICE_ID_CANCEL;
			break;

		case 1:
			if (gInputBoxIndex) {
				gInputBoxIndex--;
				gInputBox[gInputBoxIndex] = 10;
				gRequestDisplayScreen = DISPLAY_SCANNER;
				break;
			}
			// Fallthrough

		case 2:
			gScannerEditState = 0;
			gAnotherVoiceID = VOICE_ID_CANCEL;
			gRequestDisplayScreen = DISPLAY_SCANNER;
			break;
		}
	}
}

static void SCANNER_Key_MENU(bool bKeyPressed, bool bKeyHeld)
{
	uint8_t Channel;

	if (bKeyHeld) {
		return;
	}
	if (!bKeyPressed) {
		return;
	}
	if (gScanState == 0 && g_20000458 == 0) {
		gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
		return;
	}

	if (gScanState == 1) {
		if (g_20000458 == 1) {
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			return;
		}
	}

	if (gScanState == 3) {
		gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
		return;
	}

	gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

	switch (gScannerEditState) {
	case 0:
		if (g_20000458 == 0) {
			uint32_t Freq250;
			uint32_t Freq625;
			int16_t Delta250;
			int16_t Delta625;

			Freq250 = FREQUENCY_FloorToStep(gScanFrequency, 250, 0);
			Freq625 = FREQUENCY_FloorToStep(gScanFrequency, 625, 0);
			Delta250 = (short)gScanFrequency - (short)Freq250;
			if (125 < Delta250) {
				Delta250 = 250 - Delta250;
				Freq250 += 250;
			}
			Delta625 = (short)gScanFrequency - (short)Freq625;
			if (312 < Delta625) {
				Delta625 = 625 - Delta625;
				Freq625 += 625;
			}
			if (Delta625 < Delta250) {
				gStepSetting = STEP_6_25kHz;
				gScanFrequency = Freq625;
			} else {
				gStepSetting = STEP_2_5kHz;
				gScanFrequency = Freq250;
			}
		}
		if (IS_MR_CHANNEL(gTxInfo->CHANNEL_SAVE)) {
			gScannerEditState = 1;
			gScanChannel = gTxInfo->CHANNEL_SAVE;
			gShowChPrefix = RADIO_CheckValidChannel(gTxInfo->CHANNEL_SAVE, false, 0);
		} else {
			gScannerEditState = 2;
		}
		gScanState = 2;
		gAnotherVoiceID = VOICE_ID_MEMORY_CHANNEL;
		gRequestDisplayScreen = DISPLAY_SCANNER;
		break;

	case 1:
		if (gInputBoxIndex == 0) {
			gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
			gRequestDisplayScreen = DISPLAY_SCANNER;
			gScannerEditState = 2;
		}
		break;

	case 2:
		if (g_20000458 == 0) {
			RADIO_InitInfo(gTxInfo, gTxInfo->CHANNEL_SAVE, FREQUENCY_GetBand(gScanFrequency), gScanFrequency);
			if (g_2000045C == 1) {
				gTxInfo->ConfigRX.CodeType = gCS_ScannedType;
				gTxInfo->ConfigRX.Code = gCS_ScannedIndex;
			}
			gTxInfo->ConfigTX = gTxInfo->ConfigRX;
			gTxInfo->STEP_SETTING = gStepSetting;
		} else {
			RADIO_ConfigureChannel(0, 2);
			RADIO_ConfigureChannel(1, 2);
			gTxInfo->ConfigRX.CodeType = gCS_ScannedType;
			gTxInfo->ConfigRX.Code = gCS_ScannedIndex;
			gTxInfo->ConfigTX.CodeType = gCS_ScannedType;
			gTxInfo->ConfigTX.Code = gCS_ScannedIndex;
		}

		if (IS_MR_CHANNEL(gTxInfo->CHANNEL_SAVE)) {
			Channel = gScanChannel;
			gEeprom.MrChannel[gEeprom.TX_CHANNEL] = Channel;
		} else {
			Channel = gTxInfo->Band + FREQ_CHANNEL_FIRST;
			gEeprom.FreqChannel[gEeprom.TX_CHANNEL] = Channel;
		}
		gTxInfo->CHANNEL_SAVE = Channel;
		gEeprom.ScreenChannel[gEeprom.TX_CHANNEL] = Channel;
		gAnotherVoiceID = VOICE_ID_CONFIRM;
		gRequestDisplayScreen = DISPLAY_SCANNER;
		gRequestSaveChannel = 2;
		gScannerEditState = 0;
		break;

	default:
		gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
		break;
	}
}

static void SCANNER_Key_STAR(bool bKeyPressed, bool bKeyHeld)
{
	if ((!bKeyHeld) && (bKeyPressed)) {
		gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
		gFlagStartScan = true;
	}
	return;
}

static void SCANNER_Key_UP_DOWN(bool bKeyPressed, bool pKeyHeld, int8_t Direction)
{
	if (pKeyHeld) {
		if (!bKeyPressed) {
			return;
		}
	} else {
		if (!bKeyPressed) {
			return;
		}
		gInputBoxIndex = 0;
		gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
	}
	if (gScannerEditState == 1) {
		gScanChannel = NUMBER_AddWithWraparound(gScanChannel, Direction, 0, 199);
		gShowChPrefix = RADIO_CheckValidChannel(gScanChannel, false, 0);
		gRequestDisplayScreen = DISPLAY_SCANNER;
	} else {
		gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
	}
}

void SCANNER_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (Key >= KEY_0 && Key <= KEY_9) {
        SCANNER_Key_DIGITS(Key, bKeyPressed, bKeyHeld);
    } else {
        switch (Key) {
            case KEY_MENU:
                SCANNER_Key_MENU(bKeyPressed, bKeyHeld);
                break;
            case KEY_UP:
                SCANNER_Key_UP_DOWN(bKeyPressed, bKeyHeld, 1);
                break;
            case KEY_DOWN:
                SCANNER_Key_UP_DOWN(bKeyPressed, bKeyHeld, -1);
                break;
            case KEY_EXIT:
                SCANNER_Key_EXIT(bKeyPressed, bKeyHeld);
                break;
            case KEY_STAR:
                SCANNER_Key_STAR(bKeyPressed, bKeyHeld);
                break;
            case KEY_PTT:
                GENERIC_Key_PTT(bKeyPressed);
                break;
            default:
                if (!bKeyHeld && bKeyPressed) {
                    gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
                }
                break;
        }
    }
}
