#pragma once
#include "ThreadSafeAddressMap.h"

using byte = unsigned char;

class Memory final {
public:
    Memory(const std::wstring& processName);
    ~Memory();
    void BringToFront();
    bool IsForeground();

    Memory(const Memory& memory) = delete;
    Memory& operator=(const Memory& other) = delete;

    // bytesToEOL is the number of bytes from the given index to the end of the opcode. Usually, the target address is last 4 bytes, since it's the destination of the call.
    static __int64 ReadStaticInt(__int64 offset, int index, const std::vector<byte>& data, size_t bytesToEOL = 4);
    using ScanFunc = std::function<void(__int64 offset, int index, const std::vector<byte>& data)>;
    using ScanFunc2 = std::function<bool(__int64 offset, int index, const std::vector<byte>& data)>;
    void AddSigScan(const std::vector<byte>& scanBytes, const ScanFunc& scanFunc);
    void AddSigScan2(const std::vector<byte>& scanBytes, const ScanFunc2& scanFunc);
    [[nodiscard]] size_t ExecuteSigScans();

    template<class T>
    inline std::vector<T> ReadData(const std::vector<__int64>& offsets, size_t numItems) {
        std::vector<T> data(numItems);
        if (!_handle) return data;
        ReadDataInternal(&data[0], ComputeOffset(offsets), numItems * sizeof(T));
        return data;
    }
    template<class T>
    inline std::vector<T> ReadAbsoluteData(const std::vector<__int64>& offsets, size_t numItems) {
        std::vector<T> data(numItems);
        if (!_handle) return data;
        ReadDataInternal(&data[0], ComputeOffset(offsets, true), numItems * sizeof(T));
        return data;
    }
    std::string ReadString(const std::vector<__int64>& offsets);

    template <class T>
    inline void WriteData(const std::vector<__int64>& offsets, const std::vector<T>& data) {
        WriteDataInternal(&data[0], ComputeOffset(offsets), sizeof(T) * data.size());
    }

    // This is the fully typed function -- you mostly won't need to call this.
    int CallFunction(__int64 address,
        const __int64 rcx, const __int64 rdx, const __int64 r8, const __int64 r9,
        const float xmm0, const float xmm1, const float xmm2, const float xmm3);
    int CallFunction(__int64 address, __int64 rcx) { return CallFunction(address, rcx, 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f); }
    int CallFunction(__int64 address, __int64 rcx, __int64 rdx, __int64 r8, __int64 r9) { return CallFunction(address, rcx, rdx, r8, r9, 0.0f, 0.0f, 0.0f, 0.0f); }
    int CallFunction(__int64 address, __int64 rcx, const float xmm1) { return CallFunction(address, rcx, 0, 0, 0, 0.0f, xmm1, 0.0f, 0.0f); }
    int CallFunction(__int64 address, const std::string& str, __int64 rdx);

private:
    void ReadDataInternal(void* buffer, const uintptr_t computedOffset, size_t bufferSize);
    void WriteDataInternal(const void* buffer, uintptr_t computedOffset, size_t bufferSize);
    uintptr_t ComputeOffset(std::vector<__int64> offsets, bool absolute = false);
    uintptr_t AllocateArray(__int64 size);

    // Parts of the constructor / StartHeartbeat
    std::wstring _processName;

    // Parts of Initialize
    HANDLE _handle = nullptr;
    DWORD _pid = 0;
    HWND _hwnd = NULL;
    uintptr_t _baseAddress = 0;
    uintptr_t _endOfModule = 0;

    // Parts of Read / Write / Sigscan / etc
    uintptr_t _functionPrimitive = 0;
    ThreadSafeAddressMap _computedAddresses;

    struct SigScan {
        bool found = false;
        std::vector<byte> bytes;
        ScanFunc2 scanFunc;
    };
    std::vector<SigScan> _sigScans;
};
