#pragma once

#pragma warning(push)
#pragma warning(disable:4091) // 'typedef ': ignored on left of '' when no variable is declared

// Windows headers
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <shlobj.h>

// Standard headers
#include <cassert>
#include <streambuf>
#include <varargs.h>
#include <vector>

// WinSDK headers
#include <d3d11.h>

// 3rdparty headers
#include <XInput.h>
#include <cpptoml.h>
#include <udis86.h>

// Local headers
#include "targetver.h"

#pragma warning(pop)

