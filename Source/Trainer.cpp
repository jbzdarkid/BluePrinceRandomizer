#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

enum RngClass {
    Unknown = 0,
    DoNotTamper = 1,

};

struct SigScanTemplate {
    RngClass rngClass = RngClass::Unknown;
    std::string scanHex;
    int offsetFromScan = 0;
    __int64 targetFunction = 0; // Relative to the baseAddress

    std::vector<byte> GetScanBytes() {
        std::vector<byte> bytes;
        byte b = 0x00;
        bool halfByte = false;
        for (char ch : scanHex) {
            if (ch == ' ') continue;

            static std::string HEX_CHARS = "0123456789ABCDEF";
            b *= 16;
            b += (byte)HEX_CHARS.find(ch);
            if (halfByte) bytes.push_back(b);
            halfByte = !halfByte;
        }
        assert(!halfByte);

        return bytes;
    }
};

std::shared_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::make_shared<Trainer>();

    std::vector<SigScanTemplate> sigScans {
        // UnityEngine::Random::Random.value => [0.0, 1.0]
        { RngClass::DoNotTamper, "41 80 7E 29 00 48 8B D8", 17 },
        { RngClass::DoNotTamper, "41 80 7E 29 00 48 8B D8", 30 },
        { RngClass::DoNotTamper, "41 80 7E 29 00 48 8B D8", 43 },

        // UnityEngine::Random::Random.Range(float minInclusive, float maxInclusive)
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 1E 05 00 00", -4 },
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 DA 04 00 00", -4 },
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 96 04 00 00", -4 },
        // TODO... finish this call site. Got a little bored, sadly.


    };

    for (auto& sigScan : sigScans) {
        memory->AddSigScan(sigScan.GetScanBytes(), [memory, &sigScan](int64_t offset, int index, const std::vector<uint8_t>& data) {
            sigScan.targetFunction = Memory::ReadStaticInt(offset, index + sigScan.offsetFromScan, data);
        });
    }

    // We need to save _memory before we exit, otherwise we can't destroy properly.
    trainer->_memory = memory;

    size_t numFailedScans = memory->ExecuteSigScans();
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    return trainer;
}

// Restore default game settings when shutting down the trainer.
Trainer::~Trainer() {
}
