#include "pch.h"
#include <iostream>
#include <ImageHlp.h>
#include <Psapi.h>
#include "DebugUtils.h"

#pragma push_macro("DebugPrint")
#undef DebugPrint
void DebugUtils::DebugPrint(const std::string& text) {
    OutputDebugStringA(text.c_str());
    if (text[text.size()-1] != '\n') {
        OutputDebugStringA("\n");
    }
}

void DebugUtils::DebugPrint(const std::wstring& text) {
    OutputDebugStringW(text.c_str());
    if (text[text.size()-1] != L'\n') {
        OutputDebugStringW(L"\n");
    }
}
#pragma pop_macro("DebugPrint")

void SetCurrentThreadName(const wchar_t* name) {
    HMODULE module = GetModuleHandleA("Kernel32.dll");
    if (!module) return;

    typedef HRESULT (WINAPI *TSetThreadDescription)(HANDLE, PCWSTR);
    auto setThreadDescription = (TSetThreadDescription)GetProcAddress(module, "SetThreadDescription");
    if (!setThreadDescription) return;

    setThreadDescription(GetCurrentThread(), name);
}

std::pair<uint64_t, uint64_t> DebugUtils::GetModuleBounds(HANDLE process) {
    DWORD requiredBytes = sizeof(HMODULE);
    std::vector<HMODULE> modules(1, nullptr);

    EnumProcessModules(process, &modules[0], sizeof(HMODULE) * (DWORD)modules.size(), &requiredBytes);
    modules.resize(requiredBytes / sizeof(HMODULE));
    EnumProcessModules(process, &modules[0], sizeof(HMODULE) * (DWORD)modules.size(), &requiredBytes);

    std::string baseName(512, '\0');
    for (const auto& module : modules)
    {
        GetModuleBaseNameA(process, module, &baseName[0], sizeof(baseName));
        if (strcmp(baseName.c_str(), "GameAssembly.dll") != 0) continue;

        MODULEINFO moduleInfo;
        GetModuleInformation(process, module, &moduleInfo, sizeof(moduleInfo));

        uint64_t startOfModule = reinterpret_cast<uint64_t>(moduleInfo.lpBaseOfDll);
        uint64_t endOfModule = startOfModule + moduleInfo.SizeOfImage;
        return {startOfModule, endOfModule};
    }

    assert(false);
    return {};
}
