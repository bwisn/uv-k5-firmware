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
#include "app/fm.h"
#include "app/generic.h"
#include "audio.h"
#include "bsp/dp32g030/gpio.h"
#include "driver/bk1080.h"
#include "driver/eeprom.h"
#include "driver/gpio.h"
#include "functions.h"
#include "misc.h"
#include "settings.h"
#include "ui/inputbox.h"
#include "ui/ui.h"

extern void APP_StartScan(bool bFlag);

uint16_t gFM_Channels[20];
bool gFmRadioMode;
uint8_t gFmRadioCountdown;
volatile uint16_t gFmPlayCountdown = 1;
volatile int8_t gFM_Step;
bool gFM_AutoScan;
uint8_t gFM_ChannelPosition;

bool FM_CheckValidChannel(uint8_t Channel)
{
	if (Channel < 20 && (gFM_Channels[Channel] >= 760 && gFM_Channels[Channel] < 1080)) {
		return true;
	}

	return false;
}

uint8_t FM_FindNextChannel(uint8_t Channel, uint8_t Direction)
{
    for (uint8_t i = 0; i < 20; i++) {
        Channel %= 20;
        if (FM_CheckValidChannel(Channel)) {
            return Channel;
        }
        Channel += Direction;
    }
    return 0xFF;
}

int FM_ConfigureChannelState(void)
{
	uint8_t Channel;

	gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;
	if (gEeprom.FM_IsMrMode) {
		Channel = FM_FindNextChannel(gEeprom.FM_SelectedChannel, FM_CHANNEL_UP);
		if (Channel == 0xFF) {
			gEeprom.FM_IsMrMode = false;
			return -1;
		}
		gEeprom.FM_SelectedChannel = Channel;
		gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
	}

	return 0;
}

void FM_TurnOff(void)
{
	gFmRadioMode = false;
	gFM_Step = 0;
	g_2000038E = 0;
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = false;
	BK1080_Init(0, false);
	gUpdateStatus = true;
}

void FM_EraseChannels(void)
{
    uint8_t i;
    uint8_t Template[8];

    memset(Template, 0xFF, sizeof(Template));
    for (i = 0; i < 5; i++) {
        EEPROM_WriteBuffer(0x0E40 + (i * 8), Template);
    }

    memset(gFM_Channels, 0xFF, sizeof(gFM_Channels));
}

void FM_Tune(uint16_t Frequency, int8_t Step, bool bFlag)
{
	GPIO_ClearBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = false;
	if (gFM_Step == 0) {
		gFmPlayCountdown = 120;
	} else {
		gFmPlayCountdown = 10;
	}
	gScheduleFM = false;
	g_20000427 = 0;
	gAskToSave = false;
	gAskToDelete = false;
	gEeprom.FM_FrequencyPlaying = Frequency;
	if (!bFlag) {
		Frequency += Step;
		if (Frequency < gEeprom.FM_LowerLimit) {
			Frequency = gEeprom.FM_UpperLimit;
		} else if (Frequency > gEeprom.FM_UpperLimit) {
			Frequency = gEeprom.FM_LowerLimit;
		}
		gEeprom.FM_FrequencyPlaying = Frequency;
	}

	gFM_Step = Step;
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
}

void FM_PlayAndUpdate(void)
{
	gFM_Step = 0;
	if (gFM_AutoScan) {
		gEeprom.FM_IsMrMode = true;
		gEeprom.FM_SelectedChannel = 0;
	}
	FM_ConfigureChannelState();
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
	SETTINGS_SaveFM();
	gFmPlayCountdown = 0;
	gScheduleFM = false;
	gAskToSave = false;
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = true;
}

int FM_CheckFrequencyLock(uint16_t Frequency, uint16_t LowerLimit)
{
	uint16_t SNR;
	int16_t Deviation;
	uint16_t RSSI;
	int ret = -1;

	SNR = BK1080_ReadRegister(BK1080_REG_07);
	// This cast fails to extend the sign because ReadReg is guaranteed to be U16.
	Deviation = (int16_t)SNR >> 4;
	if ((SNR & 0xF) < 2) {
		goto Bail;
	}

	RSSI = BK1080_ReadRegister(BK1080_REG_10);
	if (RSSI & 0x1000 || (RSSI & 0xFF) < 10) {
		goto Bail;
	}

	if (Deviation < 280 || Deviation > 3815) {
		if ((LowerLimit < Frequency) && (Frequency - BK1080_BaseFrequency) == 1) {
			if (BK1080_FrequencyDeviation & 0x800) {
				goto Bail;
			}
			if (BK1080_FrequencyDeviation < 20) {
				goto Bail;
			}
		}
		if ((LowerLimit <= Frequency) && (BK1080_BaseFrequency - Frequency) == 1) {
			if ((BK1080_FrequencyDeviation & 0x800) == 0) {
				goto Bail;
			}
			if (4075 < BK1080_FrequencyDeviation) {
				goto Bail;
			}
		}
		ret = 0;
	}

Bail:
	BK1080_FrequencyDeviation = Deviation;
	BK1080_BaseFrequency = Frequency;

	return ret;
}

static void FM_Key_DIGITS(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
#define STATE_FREQ_MODE 0
#define STATE_MR_MODE   1
#define STATE_SAVE      2

	if (!bKeyHeld && bKeyPressed) {
		if (!gWasFKeyPressed) {
			uint8_t State;

			if (gAskToDelete) {
				gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
				return;
			}
			if (gAskToSave) {
				State = STATE_SAVE;
			} else {
				if (gFM_Step) {
					gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
					return;
				}
				if (gEeprom.FM_IsMrMode) {
					State = STATE_MR_MODE;
				} else {
					State = STATE_FREQ_MODE;
				}
			}
			INPUTBOX_Append(Key);
			gRequestDisplayScreen = DISPLAY_FM;
			if (State == STATE_FREQ_MODE) {
				if (gInputBoxIndex == 1) {
					if (gInputBox[0] > 1) {
						gInputBox[1] = gInputBox[0];
						gInputBox[0] = 0;
						gInputBoxIndex = 2;
					}
				} else if (gInputBoxIndex > 3) {
					uint32_t Frequency;

					gInputBoxIndex = 0;
					NUMBER_Get(gInputBox, &Frequency);
					Frequency = Frequency / 10000;
					if (Frequency < gEeprom.FM_LowerLimit || gEeprom.FM_UpperLimit < Frequency) {
						gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
						gRequestDisplayScreen = DISPLAY_FM;
						return;
					}
					gEeprom.FM_SelectedFrequency = (uint16_t)Frequency;
					gAnotherVoiceID = (VOICE_ID_t)Key;
					gEeprom.FM_FrequencyPlaying = gEeprom.FM_SelectedFrequency;
					BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
					gRequestSaveFM = true;
					return;
				}
			} else if (gInputBoxIndex == 2) {
				uint8_t Channel;

				gInputBoxIndex = 0;
				Channel = ((gInputBox[0] * 10) + gInputBox[1]) - 1;
				if (State == STATE_MR_MODE) {
					if (FM_CheckValidChannel(Channel)) {
						gAnotherVoiceID = (VOICE_ID_t)Key;
						gEeprom.FM_SelectedChannel = Channel;
						gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
						BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
						gRequestSaveFM = true;
						return;
					}
				} else if (Channel < 20) {
					gAnotherVoiceID = (VOICE_ID_t)Key;
					gRequestDisplayScreen = DISPLAY_FM;
					gInputBoxIndex = 0;
					gFM_ChannelPosition = Channel;
					return;
				}
				gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
				return;
			}
			gAnotherVoiceID = (VOICE_ID_t)Key;
			return;
		}
		gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
		gWasFKeyPressed = false;
		gUpdateStatus = true;
		gRequestDisplayScreen = DISPLAY_FM;
		switch (Key) {
		case KEY_0:
			FM_Switch();
			break;

		case KEY_1:
			gEeprom.FM_IsMrMode = !gEeprom.FM_IsMrMode;
			if (!FM_ConfigureChannelState()) {
				BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
				gRequestSaveFM = true;
				return;
			}
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			break;

		case KEY_2:
			APP_StartScan(true);
			break;

		case KEY_3:
			APP_StartScan(false);
			break;

		default:
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			break;
		}
	}
}

static void FM_Key_EXIT(bool bKeyPressed, bool bKeyHeld)
{
	if (bKeyHeld) {
		return;
	}
	if (!bKeyPressed) {
		return;
	}
	gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
	if (gFM_Step == 0) {
		if (gInputBoxIndex == 0) {
			if (!gAskToSave && !gAskToDelete) {
				FM_Switch();
				return;
			}
			gAskToSave = false;
			gAskToDelete = false;
		} else {
			gInputBoxIndex--;
			gInputBox[gInputBoxIndex] = 10;
			if (gInputBoxIndex) {
				if (gInputBoxIndex != 1) {
					gRequestDisplayScreen = DISPLAY_FM;
					return;
				}
				if (gInputBox[0] != 0) {
					gRequestDisplayScreen = DISPLAY_FM;
					return;
				}
			}
			gInputBoxIndex = 0;
		}
		gAnotherVoiceID = VOICE_ID_CANCEL;
	} else {
		FM_PlayAndUpdate();
		gAnotherVoiceID = VOICE_ID_SCANNING_STOP;
	}
	gRequestDisplayScreen = DISPLAY_FM;
}

static void FM_Key_MENU(bool bKeyPressed, bool bKeyHeld)
{
	if (bKeyHeld) {
		return;
	}
	if (!bKeyPressed) {
		return;
	}

	gRequestDisplayScreen = DISPLAY_FM;
	gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;

	if (gFM_Step == 0) {
		if (!gEeprom.FM_IsMrMode) {
			if (gAskToSave) {
				gFM_Channels[gFM_ChannelPosition] = gEeprom.FM_FrequencyPlaying;
				gAskToSave = false;
				gRequestSaveFM = true;
			} else {
				gAskToSave = true;
			}
		} else {
			if (gAskToDelete) {
				gFM_Channels[gEeprom.FM_SelectedChannel] = 0xFFFF;
				FM_ConfigureChannelState();
				BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
				gRequestSaveFM = true;
				gAskToDelete = false;
			} else {
				gAskToDelete = true;
			}
		}
	} else {
		if (gFM_AutoScan || g_20000427 != 1) {
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			gInputBoxIndex = 0;
			return;
		} else if (gAskToSave) {
			gFM_Channels[gFM_ChannelPosition] = gEeprom.FM_FrequencyPlaying;
			gAskToSave = false;
			gRequestSaveFM = true;
		} else {
			gAskToSave = true;
		}
	}
}

static void FM_Key_UP_DOWN(bool bKeyPressed, bool bKeyHeld, int8_t Step)
{
	if (bKeyHeld || !bKeyPressed) {
		if (gInputBoxIndex) {
			return;
		}
		if (!bKeyPressed) {
			return;
		}
	} else {
		if (gInputBoxIndex) {
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			return;
		}
		gBeepToPlay = BEEP_1KHZ_60MS_OPTIONAL;
	}
	if (gAskToSave) {
		gRequestDisplayScreen = DISPLAY_FM;
		gFM_ChannelPosition = NUMBER_AddWithWraparound(gFM_ChannelPosition, Step, 0, 19);
		return;
	}
	if (gFM_Step) {
		if (gFM_AutoScan) {
			gBeepToPlay = BEEP_500HZ_60MS_DOUBLE_BEEP_OPTIONAL;
			return;
		}
		FM_Tune(gEeprom.FM_FrequencyPlaying, Step, false);
		gRequestDisplayScreen = DISPLAY_FM;
		return;
	}
	if (gEeprom.FM_IsMrMode) {
		uint8_t Channel;

		Channel = FM_FindNextChannel(gEeprom.FM_SelectedChannel + Step, Step);
		if (Channel == 0xFF || gEeprom.FM_SelectedChannel == Channel) {
			goto Bail;
		}
		gEeprom.FM_SelectedChannel = Channel;
		gEeprom.FM_FrequencyPlaying = gFM_Channels[Channel];
	} else {
		uint16_t Frequency;

		Frequency = gEeprom.FM_SelectedFrequency + Step;
		if (Frequency < gEeprom.FM_LowerLimit) {
			Frequency = gEeprom.FM_UpperLimit;
		} else if (Frequency > gEeprom.FM_UpperLimit) {
			Frequency = gEeprom.FM_LowerLimit;
		}
		gEeprom.FM_FrequencyPlaying = Frequency;
		gEeprom.FM_SelectedFrequency = gEeprom.FM_FrequencyPlaying;
	}
	gRequestSaveFM = true;

Bail:
	BK1080_SetFrequency(gEeprom.FM_FrequencyPlaying);
	gRequestDisplayScreen = DISPLAY_FM;
}

void FM_ProcessKeys(KEY_Code_t Key, bool bKeyPressed, bool bKeyHeld)
{
    if (Key >= KEY_0 && Key <= KEY_9) {
        FM_Key_DIGITS(Key, bKeyPressed, bKeyHeld);
    } else {
        switch (Key) {
            case KEY_MENU:
                FM_Key_MENU(bKeyPressed, bKeyHeld);
                break;
            case KEY_UP:
                FM_Key_UP_DOWN(bKeyPressed, bKeyHeld, 1);
                break;
            case KEY_DOWN:
                FM_Key_UP_DOWN(bKeyPressed, bKeyHeld, -1);
                break;
            case KEY_EXIT:
                FM_Key_EXIT(bKeyPressed, bKeyHeld);
                break;
            case KEY_F:
                GENERIC_Key_F(bKeyPressed, bKeyHeld);
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

void FM_Play(void)
{
	if (!FM_CheckFrequencyLock(gEeprom.FM_FrequencyPlaying, gEeprom.FM_LowerLimit)) {
		if (!gFM_AutoScan) {
			gFmPlayCountdown = 0;
			g_20000427 = 1;
			if (!gEeprom.FM_IsMrMode) {
				gEeprom.FM_SelectedFrequency = gEeprom.FM_FrequencyPlaying;
			}
			GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
			gEnableSpeaker = true;
		} else {
			if (gFM_ChannelPosition < 20) {
				gFM_Channels[gFM_ChannelPosition++] = gEeprom.FM_FrequencyPlaying;
				if (gEeprom.FM_UpperLimit > gEeprom.FM_FrequencyPlaying) {
					FM_Tune(gEeprom.FM_FrequencyPlaying, gFM_Step, false);
				} else {
					FM_PlayAndUpdate();
				}
			} else {
				FM_PlayAndUpdate();
			}
		}
	} else if (gFM_AutoScan) {
		if (gEeprom.FM_UpperLimit > gEeprom.FM_FrequencyPlaying) {
			FM_Tune(gEeprom.FM_FrequencyPlaying, gFM_Step, false);
		} else {
			FM_PlayAndUpdate();
		}
	} else {
		FM_Tune(gEeprom.FM_FrequencyPlaying, gFM_Step, false);
	}

	GUI_SelectNextDisplay(DISPLAY_FM);
}

void FM_Start(void)
{
	gFmRadioMode = true;
	gFM_Step = 0;
	g_2000038E = 0;
	BK1080_Init(gEeprom.FM_FrequencyPlaying, true);
	GPIO_SetBit(&GPIOC->DATA, GPIOC_PIN_AUDIO_PATH);
	gEnableSpeaker = true;
	gUpdateStatus = true;
}

void FM_Switch(void)
{
	if (gCurrentFunction != FUNCTION_TRANSMIT && gCurrentFunction != FUNCTION_MONITOR) {
		if (gFmRadioMode) {
			FM_TurnOff();
			gInputBoxIndex = 0;
			g_200003B6 = 0x50;
			g_20000398 = 1;
			gRequestDisplayScreen = DISPLAY_MAIN;
			return;
		}
		RADIO_ConfigureTX();
		RADIO_SetupRegisters(true);
		FM_Start();
		gInputBoxIndex = 0;
		gRequestDisplayScreen = DISPLAY_FM;
	}
}
