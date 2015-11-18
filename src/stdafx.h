#pragma once

#pragma warning(push)
#pragma warning(disable:4091) // 'typedef ': ignored on left of '' when no variable is declared

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cassert>
#include <varargs.h>
#include <streambuf>

#include <PolyHook.h>

#include "targetver.h"
#include "XInput.h"

#pragma warning(pop)

#define REF(...) (void)(__VA_ARGS__)

void WriteToLog(const char str[], ...);
