#pragma once

class Trainer final {
public:
    static std::shared_ptr<Trainer> Create(const std::shared_ptr<Memory>& memory);

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

    enum RngBehavior : byte {
        NotSet = 0,
        Constant = 1,
        Increment = 2,
        Randomize = 3,
    };

    void SetSeed(RngClass rngClass, __int64 rngValue) { _memory->WriteData<__int64>({_rngSeedArray, rngClass*8}, {rngValue}, true); }
    void SetAllSeeds(const std::vector<__int64>& rngValues) { _memory->WriteData<__int64>({_rngSeedArray}, rngValues, true); }
    __int64 GetSeed(RngClass rngClass) { return _memory->ReadData<__int64>({_rngSeedArray, rngClass}, 1, true)[0]; }
    std::vector<__int64> GetAllSeeds() { return _memory->ReadData<__int64>({_rngSeedArray}, RngClass::NumEntries, true); }

    void SetRngBehavior(RngClass rngClass, RngBehavior rngBehavior) { _memory->WriteData<RngBehavior>({_rngBehaviors, rngClass}, {rngBehavior}, true); }
    void SetAllBehaviors(const std::vector<RngBehavior>& rngBehaviors) { _memory->WriteData<RngBehavior>({_rngSeedArray}, rngBehaviors, true); }
    RngBehavior GetRngBehavior(RngClass rngClass)  { return _memory->ReadData<RngBehavior>({_rngBehaviors, rngClass}, 1, true)[0]; }
    std::vector<RngBehavior> GetAllBehaviors() { return _memory->ReadData<RngBehavior>({_rngBehaviors}, RngClass::NumEntries, true); }

private:
    Trainer() = default;
    void InjectCustomRng();
    bool FindAllRngFunctions();
    void OverwriteRngFunctions();

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

    __int64 _rngSeedArray = 0;
    __int64 _rngBehaviors = 0;
    __int64 _intRngFunction = 0;
    __int64 _floatRngFunction = 0;
};
