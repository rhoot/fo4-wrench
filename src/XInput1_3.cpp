// XInput1_3.cpp : Defines the exported functions for the DLL application.
//

#include "stdafx.h"

#ifdef __cplusplus
extern "C" {
#endif

DWORD WINAPI XInputGetState (DWORD         dwUserIndex,
                             XINPUT_STATE* pState) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


DWORD WINAPI XInputSetState (DWORD             dwUserIndex,
                             XINPUT_VIBRATION* pVibration) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


DWORD WINAPI XInputGetCapabilities (DWORD                dwUserIndex,
                                    DWORD                dwFlags,
                                    XINPUT_CAPABILITIES* pCapabilities) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


void WINAPI XInputEnable (BOOL enable) {
}


DWORD WINAPI XInputGetDSoundAudioDeviceGuids (DWORD dwUserIndex,
                                              GUID* pDSoundRenderGuid,
                                              GUID* pDSoundCaptureGuid) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


DWORD WINAPI XInputGetBatteryInformation (DWORD                       dwUserIndex,
                                          BYTE                        devType,
                                          XINPUT_BATTERY_INFORMATION* pBatteryInformation) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


DWORD WINAPI XInputGetKeystroke (DWORD dwUserIndex,
                                 DWORD dwReserved,
                                 PXINPUT_KEYSTROKE pKeystroke) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


// Undocumented

DWORD WINAPI XInputGetStateEx(DWORD dwUserIndex, XINPUT_STATE* pState) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


DWORD WINAPI XInputWaitForGuideButton(DWORD dwUserIndex, DWORD dwFlag, LPVOID pVoid) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


DWORD WINAPI XInputCancelGuideButtonWait(DWORD dwUserIndex) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


DWORD WINAPI XInputPowerOffController(DWORD dwUserIndex) {
    return ERROR_DEVICE_NOT_CONNECTED;
}


#ifdef __cplusplus
}
#endif