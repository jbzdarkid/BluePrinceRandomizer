#pragma once

class Trainer final {
public:
    Trainer(const std::shared_ptr<Memory>& memory);

    enum RngClass : byte {
        Unknown = 0,
        DoNotTamper = 1,
        BirdPathing = 2,
        Rarity = 3,
        Drafting = 4,
        Items = 5,
        DogSwapper = 6,
        Trading = 7,
        Derigiblock = 8,
        SlotMachine = 9,

        NumEntries,
    };

private:
    bool FindAllRngFunctions();
    bool InjectCustomRng();

    struct SigScanTemplate {
        RngClass rngClass = RngClass::Unknown;
        std::string scanHex;
        int offsetFromScan = 0;
        __int64 foundAddress = 0; // Relative to the baseAddress
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


    std::shared_ptr<Memory> _memory;
    std::vector<SigScanTemplate> _sigScans1;
    std::vector<SigScanTemplate> _sigScans2;
    std::vector<SigScanTemplate> _sigScans3;
};
