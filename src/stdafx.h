// Copyright (c) 2015, Johan Sk�ld
// License: https://opensource.org/licenses/ISC

#pragma once

#pragma warning(push)
#pragma warning(disable:4091) // 'typedef ': ignored on left of '' when no variable is declared
#pragma warning(disable:4456) // declaration of 'identifier' hides previous local declaration

// Windows headers
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
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

#pragma warning(pop)
