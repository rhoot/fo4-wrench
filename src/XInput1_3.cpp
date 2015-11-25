// Copyright (c) 2015, Johan Sköld
// License: https://opensource.org/licenses/ISC

#include "stdafx.h"

#include "config.h"
#include "util.h"

extern "C" {

    ///
    // Undocumented calls
    ///

    DWORD WINAPI XInputGetStateEx (DWORD dwUserIndex, XINPUT_STATE* pState);
    DWORD WINAPI XInputWaitForGuideButton (DWORD dwUserIndex, DWORD dwFlag, LPVOID pVoid);
    DWORD WINAPI XInputCancelGuideButtonWait (DWORD dwUserIndex);
    DWORD WINAPI XInputPowerOffController (DWORD dwUserIndex);


    ///
    // XInput
    ///

    static bool s_loaded;
    static HMODULE s_xinput;

    static decltype(XInputGetState) * s_getState;
    static decltype(XInputSetState) * s_setState;
    static decltype(XInputGetCapabilities) * s_getCapabilities;
    static decltype(XInputEnable) * s_enable;
    static decltype(XInputGetDSoundAudioDeviceGuids) * s_getDSoundAudioDeviceGuids;
    static decltype(XInputGetBatteryInformation) * s_getBatteryInformation;
    static decltype(XInputGetKeystroke) * s_getKeystroke;
    static decltype(XInputGetStateEx) * s_getStateEx;
    static decltype(XInputWaitForGuideButton) * s_waitForGuideButton;
    static decltype(XInputCancelGuideButtonWait) * s_cancelGuideButtonWait;
    static decltype(XInputPowerOffController) * s_powerOffController;

    static void UnloadXInput ()
    {
        s_getState = nullptr;
        s_setState = nullptr;
        s_getCapabilities = nullptr;
        s_enable = nullptr;
        s_getDSoundAudioDeviceGuids = nullptr;
        s_getBatteryInformation = nullptr;
        s_getKeystroke = nullptr;
        s_getStateEx = nullptr;
        s_waitForGuideButton = nullptr;
        s_cancelGuideButtonWait = nullptr;
        s_powerOffController = nullptr;

        if (s_xinput) {
            CloseHandle(s_xinput);
            s_xinput = nullptr;
        }
    }

    static void LoadXInput ()
    {
        if (s_loaded) {
            return;
        }

        wchar_t path[MAX_PATH] = {0};
        const auto configured = config::Get({"XInput", "Path"});

        if (configured) {
            wchar_t str[MAX_PATH];
            MultiByteToWideChar(CP_UTF8,
                                MB_PRECOMPOSED | MB_ERR_INVALID_CHARS,
                                configured,
                                -1,
                                str,
                                (int)ArraySize(str));
            ExpandEnvironmentStringsW(str, path, (DWORD)ArraySize(path));
        } else {
            const auto str = L"%WINDIR%\\system32\\XInput1_3.dll";
            ExpandEnvironmentStringsW(str, path, (DWORD)ArraySize(path));
        }

        if (*path) {
            s_xinput = LoadLibraryW(path);

            if (s_xinput) {
                atexit(UnloadXInput);

                s_getState = (decltype(s_getState))GetProcAddress(s_xinput, "XInputGetState");
                s_setState = (decltype(s_setState))GetProcAddress(s_xinput, "XInputSetState");
                s_getCapabilities = (decltype(s_getCapabilities))GetProcAddress(s_xinput, "XInputGetCapabilities");
                s_enable = (decltype(s_enable))GetProcAddress(s_xinput, "XInputEnable");
                s_getDSoundAudioDeviceGuids = (decltype(s_getDSoundAudioDeviceGuids))GetProcAddress(s_xinput, "XInputGetDSoundAudioDeviceGuids");
                s_getBatteryInformation = (decltype(s_getBatteryInformation))GetProcAddress(s_xinput, "XInputGetBatteryInformation");
                s_getKeystroke = (decltype(s_getKeystroke))GetProcAddress(s_xinput, "XInputGetKeystroke");

                // The undocumented functions must be imported through ordinal as they don't have names
                s_getStateEx = (decltype(s_getStateEx))GetProcAddress(s_xinput, (const char*)100);
                s_waitForGuideButton = (decltype(s_waitForGuideButton))GetProcAddress(s_xinput, (const char*)101);
                s_cancelGuideButtonWait = (decltype(s_cancelGuideButtonWait))GetProcAddress(s_xinput, (const char*)102);
                s_powerOffController = (decltype(s_powerOffController))GetProcAddress(s_xinput, (const char*)103);
            }
        }

        // We should always flag it as loaded, even if it failed. If not, we will just keep trying
        // over and over for no reason.
        s_loaded = true;
    }


    ///
    // Wrapper
    ///

    DWORD WINAPI XInputGetState (DWORD         dwUserIndex,
                                 XINPUT_STATE* pState)
    {
        LoadXInput();

        return s_getState
               ? s_getState(dwUserIndex, pState)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD WINAPI XInputSetState (DWORD             dwUserIndex,
                                 XINPUT_VIBRATION* pVibration)
    {
        LoadXInput();

        return s_setState
               ? s_setState(dwUserIndex, pVibration)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD WINAPI XInputGetCapabilities (DWORD                dwUserIndex,
                                        DWORD                dwFlags,
                                        XINPUT_CAPABILITIES* pCapabilities)
    {
        LoadXInput();

        return s_getCapabilities
               ? s_getCapabilities(dwUserIndex, dwFlags, pCapabilities)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

    void WINAPI XInputEnable (BOOL enable)
    {
        LoadXInput();

        if (s_enable) {
            s_enable(enable);
        }
    }

    DWORD WINAPI XInputGetDSoundAudioDeviceGuids (DWORD dwUserIndex,
                                                  GUID* pDSoundRenderGuid,
                                                  GUID* pDSoundCaptureGuid)
    {
        LoadXInput();

        return s_getDSoundAudioDeviceGuids
               ? s_getDSoundAudioDeviceGuids(dwUserIndex, pDSoundRenderGuid, pDSoundCaptureGuid)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD WINAPI XInputGetBatteryInformation (DWORD                       dwUserIndex,
                                              BYTE                        devType,
                                              XINPUT_BATTERY_INFORMATION* pBatteryInformation)
    {
        LoadXInput();

        return s_getBatteryInformation
               ? s_getBatteryInformation(dwUserIndex, devType, pBatteryInformation)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD WINAPI XInputGetKeystroke (DWORD             dwUserIndex,
                                     DWORD             dwReserved,
                                     PXINPUT_KEYSTROKE pKeystroke)
    {
        LoadXInput();

        return s_getKeystroke
               ? s_getKeystroke(dwUserIndex, dwReserved, pKeystroke)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD WINAPI XInputGetStateEx (DWORD dwUserIndex, XINPUT_STATE* pState)
    {
        LoadXInput();

        return s_getStateEx
               ? s_getStateEx(dwUserIndex, pState)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD WINAPI XInputWaitForGuideButton (DWORD dwUserIndex, DWORD dwFlag, LPVOID pVoid)
    {
        LoadXInput();

        return s_waitForGuideButton
               ? s_waitForGuideButton(dwUserIndex, dwFlag, pVoid)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD WINAPI XInputCancelGuideButtonWait (DWORD dwUserIndex)
    {
        LoadXInput();

        return s_cancelGuideButtonWait
               ? s_cancelGuideButtonWait(dwUserIndex)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

    DWORD WINAPI XInputPowerOffController (DWORD dwUserIndex)
    {
        LoadXInput();

        return s_powerOffController
               ? s_powerOffController(dwUserIndex)
               : ERROR_DEVICE_NOT_CONNECTED;
    }

} // extern "C"
