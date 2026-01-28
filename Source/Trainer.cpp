#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

enum RngClass {
    Unknown = 0,
    DoNotTamper = 1,
    BirdPathing = 2,
    Rarity = 3,
    Drafting = 4,
    Items = 5,
    DogSwapper = 6,
    Trading = 7,
    Derigiblock = 8,
};

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

std::shared_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::make_shared<Trainer>();

    // I have (painstakingly) generated a bunch of sigscans for all the BluePrince code locations which are calling into the RNG.
    // They are categorized on two dimensions:
    // - First, by the function they're using. This is important for our injection; each function class has a particular expected return type that we must match.
    // - Second, by the usage. This is important for modding, since we care about some of these random values more so than others.
    // In some cases, I have also annotated the sigscan by the name of the calling function, in the (vain) hope that it will help someone, somehow.
    std::vector<SigScanTemplate> sigScans {
        // UnityEngine::Random::Random.value => [0.0, 1.0]
        // { RngClass::DoNotTamper, "41 80 7E 29 00 48 8B D8", 17 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::DoNotTamper, "41 80 7E 29 00 48 8B D8", 30 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::DoNotTamper, "41 80 7E 29 00 48 8B D8", 43 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::DoNotTamper, "0F 84 6E 02 00 00 45 33 C0 33 D2", 22 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::DoNotTamper, "0F 84 6E 02 00 00 45 33 C0 33 D2", 32 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::DoNotTamper, "0F 84 6E 02 00 00 45 33 C0 33 D2", 42 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::BirdPathing, "F3 0F 11 43 64 0F 86 19 01 00 00", 23 }, // void BirdPather::BirdPather.Update()
        // { RngClass::BirdPathing, "F3 0F 10 4B 4C 0F 2F C8 0F 86 01 01 00 00", -4 }, // void BirdPather::BirdPather.JumpForwardsTick()
        // { RngClass::Rarity,      "48 8B 7C E9 20 48 85 FF", 17 }, // void RoomDraftContext::RoomDraftContext.ResetPlans()
        // { RngClass::Drafting,    "48 8B 01 48 39 47 10 74 5C 33 C9", 12 }, // void RoomDraftHelper::RoomDraftHelper.StartDraft() -> Seems to be used for determining if the Bookshop can be spawned
        // { RngClass::Rarity,      "48 8B 7C F1 20 48 85 FF 74 78", 13 }, // void RoomDraftRound::RoomDraftRound.RunbackFilter(DraftRankRarity probs)
        // { RngClass::Rarity,      "F3 41 0F 10 76 2C EB 06", 17 }, // void OuterDraftManager::OuterDraftManager.FilterRarityOutput()
        // { RngClass::Trading,     "EB 5A 85 FF 78 2C", -4 }, // void TradeManager::TradeManager.SetTradeOffer(ItemData item)
        // { RngClass::Trading,     "48 85 F6 75 76 33 C9", 8 },  // void TradeManager::TradeManager.SetTradeOffer(ItemData item)
        // { RngClass::DoNotTamper, "0F 2F C6 76 27 33 C9", 8 }, // void BluePrince::TestActionPrompter::TestActionPrompter.Update()

        // UnityEngine::Random::Random.Range(int minInclusive, int maxExclusive) => [min, max)
        // { RngClass::DoNotTamper, "8B 57 1C 45 33 C0 8B", 12 }, // void VLB::DynamicOcclusionAbstractBase::DynamicOcclusionAbstractBase.ProcessOcclusion(DynamicOcclusionAbstractBase.ProcessOcclusionSource source)
        // { RngClass::DoNotTamper, "45 33 C0 BA 68 01 00 00 33", -4 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::DoNotTamper, "45 33 C0 BA 68 01 00 00 33", 13 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::DoNotTamper, "F3 0F 11 43 5C 41", 13 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::DoNotTamper, "B9 0C FE FF FF", 9 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        // { RngClass::DogSwapper,  "74 0E 45 33 C0 8B D6", 10 }, // void Kennel_DogSwapper::Kennel_DogSwapper.RegenerateCombinations() -> Seems to be used for knuth randomization of a list of some sort
        // { RngClass::Drafting,    "2B 4F 30 8B 50 18", 9 }, // RoomCard RoomDeck::RoomDeck.PickTop(bool reshuffle)
        // { RngClass::DoNotTamper, "0F 84 F9 00 00 00 8B 56", 15 }, // void HutongGames::PlayMaker::Actions::SetRandomMaterial::SetRandomMaterial.DoSetRandomMaterial()
        // { RngClass::DoNotTamper, "48 63 C8 3B 4B 18 73 50", -4 }, // void HutongGames::PlayMaker::Actions::SetRandomMaterial::SetRandomMaterial.DoSetRandomMaterial()
        // { RngClass::DoNotTamper, "66 0F 6E C3 0F 5B C0 66 0F 6E F8", -4 }, // void HutongGames::PlayMaker::Actions::Vector2RandomValue::Vector2RandomValue.DoRandomVector2() -> Unused
        // { RngClass::Trading,     "48 63 C8 3B 4B 18 73 31", -4 }, // ItemData TradeManager::TradeManager.PickFromTradingTier(int tier) -> Seems to be directly picking the random item to provide (from a given list)
        // { RngClass::Derigiblock, "74 48 45 33 C0 8B 53", 11 }, // Object Derigiblocks::DerigiblocksBlockDatabase::DerigiblocksBlockDatabase.GetBlock(DerigiblocksBlockType type)
        // { RngClass::DoNotTamper, "38 4B 1C 0F 95 C1", 10 }, // AudioClip SoundeR::AudioPicker::AudioPicker.GetAudioClip()

        // UnityEngine::Random::Random.Range(float minInclusive, float maxInclusive) => [min, max]
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 1E 05 00 00", -4 }, // void iTween::iTween.ApplyShakePositionTargets()
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 DA 04 00 00", -4 }, // void iTween::iTween.ApplyShakePositionTargets()
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 96 04 00 00", -4 }, // void iTween::iTween.ApplyShakePositionTargets()
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 EF 01 00 00", -4 }, // void iTween::iTween.ApplyShakeScaleTargets()
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 A7 01 00 00", -4 }, // void iTween::iTween.ApplyShakeScaleTargets()
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 5F 01 00 00", -4 }, // void iTween::iTween.ApplyShakeScaleTargets()
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 E0 02 00 00", -4 }, // void iTween::iTween.ApplyShakeRotationTargets()
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 9C 02 00 00", -4 }, // void iTween::iTween.ApplyShakeRotationTargets()
        { RngClass::DoNotTamper, "83 7F 18 02 0F 86 58 02 00 00", -4 }, // void iTween::iTween.ApplyShakeRotationTargets()
        { RngClass::DoNotTamper, "0F 29 70 E8 49 8B F8", 59 }, // Vector3 VLB::DynamicOcclusionRaycasting::DynamicOcclusionRaycasting.GetRandomVectorAround(Vector3 direction, float angleDiff)
        { RngClass::DoNotTamper, "0F 29 70 E8 49 8B F8", 78 }, // Vector3 VLB::DynamicOcclusionRaycasting::DynamicOcclusionRaycasting.GetRandomVectorAround(Vector3 direction, float angleDiff)
        { RngClass::DoNotTamper, "0F 29 70 E8 49 8B F8", 96 }, // Vector3 VLB::DynamicOcclusionRaycasting::DynamicOcclusionRaycasting.GetRandomVectorAround(Vector3 direction, float angleDiff)
        { RngClass::DoNotTamper, "45 33 C0 F3 0F 10 47 58", 9 }, // bool VLB::EffectFlicker::_CoUpdate_d__9::EffectFlicker_CoUpdate_d_9_MoveNext(EffectFlicker_CoUpdate_d_9 *this,MethodInfo *method)
        { RngClass::DoNotTamper, "45 33 C0 F3 0F 10 47 50", 9 }, // bool VLB::EffectFlicker::_CoFlicker_d__10::EffectFlicker_CoFlicker_d_10_MoveNext(EffectFlicker_CoFlicker_d_10 *this,MethodInfo *method)
        { RngClass::DoNotTamper, "F3 0F 10 4F 64", 11 }, // bool VLB::EffectFlicker::_CoFlicker_d__10::EffectFlicker_CoFlicker_d_10_MoveNext(EffectFlicker_CoFlicker_d_10 *this,MethodInfo *method)
        { RngClass::DoNotTamper, "CC F3 0F 10 49 04 45", 14 }, // float VLB::MinMaxRangeFloat::FloatRegion.Random -> Unused
        { RngClass::DoNotTamper, "41 0F 28 CA 0F 11 43 20", 13 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "41 0F 28 CA 0F 11 43 20", 37 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "41 0F 28 CA 0F 11 43 20", 57 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()


        // There are 79 of these. Strap in.


    };

    for (auto& sigScan : sigScans) {
        memory->AddSigScan(sigScan.GetScanBytes(), [memory, &sigScan](int64_t offset, int index, const std::vector<uint8_t>& data) {
            sigScan.foundAddress = offset + index + sigScan.offsetFromScan;
            sigScan.targetFunction = Memory::ReadStaticInt(offset, index + sigScan.offsetFromScan, data);
            if (sigScan.targetFunction != 0x00000000023fcc00) {
                int i = sigScan.offsetFromScan;
                for (; i < 100; i++) {
                    auto targetFunction = Memory::ReadStaticInt(offset, index + i, data);
                    if (targetFunction == 0x00000000023fcc00) break;
                }
                assert(false);
            }

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
