#pragma once
#include "../App/Version.h"

// Custom assert override nonsense
#include <cassert>
#define W(x)    W_(x)
#define W_(x)   L ## x

#undef assert
#define assert(expr, message) \
    if (!(expr)) { \
        void ShowAssertDialogue(const wchar_t*); \
        ShowAssertDialogue(W_(message)); \
    }

#include <crtdbg.h>
#undef _RPT_BASE
#define _RPT_BASE(...) \
    void ShowAssertDialogue(const wchar_t*); \
    ShowAssertDialogue(nullptr);

#undef _RPT_BASE_W
#define _RPT_BASE_W(...) \
    void ShowAssertDialogue(const wchar_t*); \
    ShowAssertDialogue(nullptr);
// Done with custom assert nonsense


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

#pragma warning (disable: 26451) // Potential arithmetic overflow
#pragma warning (disable: 26812) // Unscoped enum type

#include "Memory.h"
#include "DebugUtils.h"
