// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>

#define REF(...) (void)(__VA_ARGS__)

// TODO: reference additional headers your program requires here
#include <varargs.h>
#include <streambuf>

#include "XInput.h"

void WriteToLog(const char str[], ...);