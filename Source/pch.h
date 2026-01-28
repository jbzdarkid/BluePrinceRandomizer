#pragma once

#define WIN32_LEAN_AND_MEAN 1
#define VC_EXTRALEAN 1
#define NOMINMAX
#include <windows.h>
#undef min
#undef max

#include <map>
#include <functional>
#include <cmath>
#include <ctime>
#include <csignal>
#include <iomanip>
#include <sstream>
#include <thread>
#include <cassert>

#pragma warning (disable: 26451) // Potential arithmetic overflow
#pragma warning (disable: 26812) // Unscoped enum type

#include "Memory.h"
#include "DebugUtils.h"
