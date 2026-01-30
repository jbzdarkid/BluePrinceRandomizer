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
    SlotMachine = 9,
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
    trainer->_memory = memory;

    // I have (painstakingly) generated a bunch of sigscans for all the BluePrince code locations which are calling into the RNG.
    // They are categorized on two dimensions:
    // - First, by the function they're using. This is important for our injection; each function class has a particular expected return type that we must match.
    // - Second, by the usage. This is important for modding, since we care about some of these random values more so than others.
    // In some cases, I have also annotated the sigscan by the name of the calling function, in the (vain) hope that it will help someone, somehow.
    std::vector<SigScanTemplate> sigScans {
        /*
        // UnityEngine::Random::Random.value => [0.0, 1.0]
        { RngClass::DoNotTamper, "41 80 7E 29 00 48 8B D8", 17 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "41 80 7E 29 00 48 8B D8", 30 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "41 80 7E 29 00 48 8B D8", 43 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "0F 84 6E 02 00 00 45 33 C0 33 D2", 22 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "0F 84 6E 02 00 00 45 33 C0 33 D2", 32 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "0F 84 6E 02 00 00 45 33 C0 33 D2", 42 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::BirdPathing, "F3 0F 11 43 64 0F 86 19 01 00 00", 23 }, // void BirdPather::BirdPather.Update()
        { RngClass::BirdPathing, "F3 0F 10 4B 4C 0F 2F C8 0F 86 01 01 00 00", -4 }, // void BirdPather::BirdPather.JumpForwardsTick()
        { RngClass::Rarity,      "48 8B 7C E9 20 48 85 FF", 17 }, // void RoomDraftContext::RoomDraftContext.ResetPlans()
        { RngClass::Drafting,    "48 8B 01 48 39 47 10 74 5C 33 C9", 12 }, // void RoomDraftHelper::RoomDraftHelper.StartDraft() -> Seems to be used for determining if the Bookshop can be spawned
        { RngClass::Rarity,      "48 8B 7C F1 20 48 85 FF 74 78", 13 }, // void RoomDraftRound::RoomDraftRound.RunbackFilter(DraftRankRarity probs)
        { RngClass::Rarity,      "F3 41 0F 10 76 2C EB 06", 17 }, // void OuterDraftManager::OuterDraftManager.FilterRarityOutput()
        { RngClass::Trading,     "EB 5A 85 FF 78 2C", -4 }, // void TradeManager::TradeManager.SetTradeOffer(ItemData item)
        { RngClass::Trading,     "48 85 F6 75 76 33 C9", 8 },  // void TradeManager::TradeManager.SetTradeOffer(ItemData item)
        { RngClass::DoNotTamper, "0F 2F C6 76 27 33 C9", 8 }, // void BluePrince::TestActionPrompter::TestActionPrompter.Update()

        // UnityEngine::Random::Random.Range(int minInclusive, int maxExclusive) => [min, max)
        { RngClass::DoNotTamper, "8B 57 1C 45 33 C0 8B", 12 }, // void VLB::DynamicOcclusionAbstractBase::DynamicOcclusionAbstractBase.ProcessOcclusion(DynamicOcclusionAbstractBase.ProcessOcclusionSource source)
        { RngClass::DoNotTamper, "45 33 C0 BA 68 01 00 00 33", -4 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "45 33 C0 BA 68 01 00 00 33", 13 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "F3 0F 11 43 5C 41", 13 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "B9 0C FE FF FF", 9 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DogSwapper,  "74 0E 45 33 C0 8B D6", 10 }, // void Kennel_DogSwapper::Kennel_DogSwapper.RegenerateCombinations() -> Seems to be used for knuth randomization of a list of some sort
        { RngClass::Drafting,    "2B 4F 30 8B 50 18", 9 }, // RoomCard RoomDeck::RoomDeck.PickTop(bool reshuffle)
        { RngClass::DoNotTamper, "0F 84 F9 00 00 00 8B 56", 15 }, // void HutongGames::PlayMaker::Actions::SetRandomMaterial::SetRandomMaterial.DoSetRandomMaterial()
        { RngClass::DoNotTamper, "48 63 C8 3B 4B 18 73 50", -4 }, // void HutongGames::PlayMaker::Actions::SetRandomMaterial::SetRandomMaterial.DoSetRandomMaterial()
        { RngClass::DoNotTamper, "66 0F 6E C3 0F 5B C0 66 0F 6E F8", -4 }, // void HutongGames::PlayMaker::Actions::Vector2RandomValue::Vector2RandomValue.DoRandomVector2() -> Unused
        { RngClass::Trading,     "48 63 C8 3B 4B 18 73 31", -4 }, // ItemData TradeManager::TradeManager.PickFromTradingTier(int tier) -> Seems to be directly picking the random item to provide (from a given list)
        { RngClass::Derigiblock, "74 48 45 33 C0 8B 53", 11 }, // Object Derigiblocks::DerigiblocksBlockDatabase::DerigiblocksBlockDatabase.GetBlock(DerigiblocksBlockType type)
        { RngClass::DoNotTamper, "38 4B 1C 0F 95 C1", 10 }, // AudioClip SoundeR::AudioPicker::AudioPicker.GetAudioClip()
        */

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
        { RngClass::DoNotTamper, "45 33 C0 41 0F 28 CA 41", 12 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "45 33 C0 41 0F 28 CA 41", 46 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "45 33 C0 41 0F 28 CA 41", 76 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "45 33 C0 41 0F 28 C8 0F 57", 11 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "89 43 68 41 0F 28 CE", 12 }, // void VLB_Samples::LightGenerator::LightGenerator.Generate()
        { RngClass::DoNotTamper, "0F 28 CE 0F 57 C0 48", 10 }, // void Rewired::Demos::CustomControllerDemo_Player::CustomControllerDemo_Player.Update()
        { RngClass::DoNotTamper, "0F 28 CE 0F 57 C0 48", 27 }, // void Rewired::Demos::CustomControllerDemo_Player::CustomControllerDemo_Player.Update()
        { RngClass::DoNotTamper, "0F 28 CE 0F 57 C0 48", 45 }, // void Rewired::Demos::CustomControllerDemo_Player::CustomControllerDemo_Player.Update()
        { RngClass::DoNotTamper, "F3 0F 10 4F 1C 45 33 C0 F3", 14 }, // void ElectricArcObject::ElectricArcObject.Start()
        { RngClass::DoNotTamper, "20 F3 0F 10 49 1C 45", 18 }, // void ElectricArcObject::ElectricArcObject.ResetTimer()
        { RngClass::DoNotTamper, "48 8B 4F 28 F3 0F 11 47 48", -4 }, // void ElectricArcObject::ElectricArcObject.Fire()
        { RngClass::SlotMachine, "0F 28 F0 4C 8B 4F 30", -4 }, // void SlotMachineBrain::SlotMachineBrain.StartNewSpin() -> Used to determine the duration that a reel spins for
        { RngClass::SlotMachine, "F3 0F 10 70 20 0F 28", 16 }, // void SlotMachineWheel::SlotMachineWheel.StartSpinning() -> Used to determine how fast the slots spin
        { RngClass::SlotMachine, "0F 28 F0 4C 8B 4F 30", -4 }, // void SlotMachineWheel::SlotMachineWheel.StopSpinning() -> Used to determine what angle the slots stop at
        { RngClass::DoNotTamper, "F3 0F 10 4B 20 45 33 C0 F3", 14 }, // void UnityStandardAssets::ImageEffects::NoiseAndScratches::NoiseAndScratches.OnRenderImage(RenderTexture *source, RenderTexture *destination)
        { RngClass::DoNotTamper, "F3 0F 10 4B 20 45 33 C0 F3", 35 }, // void UnityStandardAssets::ImageEffects::NoiseAndScratches::NoiseAndScratches.OnRenderImage(RenderTexture *source, RenderTexture *destination)
        { RngClass::DoNotTamper, "48 8B 73 78 0F 28 F0", -4 }, // void HutongGames::PlayMaker::Actions::Flicker::Flicker.OnUpdate()
        { RngClass::DoNotTamper, "F3 0F 10 43 38 0F 28 C8 45", 15 }, // void HutongGames::PlayMaker::Actions::RandomFloat::RandomFloat.OnEnter()
        { RngClass::DoNotTamper, "48 8B 4F 70 0F 28 F0", 33 }, // void HutongGames::PlayMaker::Actions::Vector2RandomValue::Vector2RandomValue.DoRandomVector2()
        { RngClass::DoNotTamper, "0F 28 F0 48 85 C9 0F 84 68", 29 }, // void HutongGames::PlayMaker::Actions::Vector2RandomValue::Vector2RandomValue.DoRandomVector2()
        { RngClass::DoNotTamper, "44 0F 28 C0 0F 28 C8 0F 28 C7", 14 }, // void HutongGames::PlayMaker::Actions::Vector2RandomValue::Vector2RandomValue.DoRandomVector2()
        { RngClass::DoNotTamper, "F3 0F 11 87 88 00 00 00 45", 19 }, // void HutongGames::PlayMaker::Actions::Vector2RandomValue::Vector2RandomValue.DoRandomVector2()
        { RngClass::DoNotTamper, "F3 0F 10 4C 24 64 F3 0F 59 D0", -10 }, // void HutongGames::PlayMaker::Actions::Vector2RandomValue::Vector2RandomValue.DoRandomVector2()
        { RngClass::DoNotTamper, "0F 57 C9 F3 0F 11 43 74", -4 }, // void HutongGames::PlayMaker::Actions::RandomWait::RandomWait.OnEnter()
        { RngClass::DoNotTamper, "F3 41 0F 10 49 2C", 16 }, // void ECprojectileActor::ECprojectileActor.Fire()
        { RngClass::DoNotTamper, "45 33 C0 41 0F 28 CC 0F", 11 }, // void UnityStandardAssets::ImageEffects::NoiseAndGrain::NoiseAndGrain.DrawNoiseQuadGrid(RenderTexture source, RenderTexture dest, Material fxMaterial, Texture2D noise, int passNr)
        { RngClass::DoNotTamper, "0F 57 C0 41 0F 28 CC", 13 }, // void UnityStandardAssets::ImageEffects::NoiseAndGrain::NoiseAndGrain.DrawNoiseQuadGrid(RenderTexture source, RenderTexture dest, Material fxMaterial, Texture2D noise, int passNr)
        { RngClass::DoNotTamper, "F3 45 0F 5C D6 F3 41", -4 }, // DG::Tweening::DOTween::DOTween_Shake(...)
        { RngClass::DoNotTamper, "0F 85 3C 01 00 00 41 0F 28", 22 }, // DG::Tweening::DOTween::DOTween_Shake(...)
        { RngClass::DoNotTamper, "02 00 00 41 0F 28 C1 45 33 C0", 19 }, // DG::Tweening::DOTween::DOTween_Shake(...)
        { RngClass::DoNotTamper, "80 79 08 00 F3 0F 10 01", 19 }, // float FluffyUnderware::DevTools::FloatRegion::FloatRegion.Next()
        { RngClass::DoNotTamper, "84 05 00 00 80 78 1C 00 75 0A", 34 }, // AudioSource SoundeR::AudioEmitter::AudioEmitter.PlaySoundAtPosition(AudioCollectionAsset collection, Vector3 worldPosition, bool attachToParent)
        { RngClass::DoNotTamper, "47 05 00 00 80 78 1C 00 75 0A", 34 }, // AudioSource SoundeR::AudioEmitter::AudioEmitter.PlaySoundAtPosition(AudioCollectionAsset collection, Vector3 worldPosition, bool attachToParent)
        { RngClass::DoNotTamper, "73 05 00 00 80 78 1C 00 75 0A", 34 }, // AudioSource SoundeR::AudioEmitter::AudioEmitter.PlaySoundAtPosition(AudioCollectionAsset collection, Vector3 worldPosition, int index, bool attachToParent) 
        { RngClass::DoNotTamper, "36 05 00 00 80 78 1C 00 75 0A", 34 }, // AudioSource SoundeR::AudioEmitter::AudioEmitter.PlaySoundAtPosition(AudioCollectionAsset collection, Vector3 worldPosition, int index, bool attachToParent)
        { RngClass::DoNotTamper, "EB 2B F3 44 0F 10 84 24 90 00 00 00", -4 },
        { RngClass::DoNotTamper, "EB 2F F3 0F 10 BC 24 90 00 00 00", -4 },
        { RngClass::DoNotTamper, "44 0F 28 D8 E9 80 00 00 00", -4 },
        { RngClass::DoNotTamper, "44 0F 28 D8 EB 3A", -4 }, 
        { RngClass::DoNotTamper, "44 0F 28 D0 E9 80 00 00 00", -4 },
        { RngClass::DoNotTamper, "44 0F 28 D0 EB 3A", -4 },







        // There are 79 of these. Strap in.


    };

    for (auto& sigScan : sigScans) {
        memory->AddSigScan(sigScan.GetScanBytes(), [&sigScan](int64_t offset, int index, const std::vector<uint8_t>& data) {
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

    size_t numFailedScans = memory->ExecuteSigScans();
    if (numFailedScans != 0) return nullptr; // Sigscans failed, we'll try again later.

    // Ok, so we did a bunch of sigscans. Now what?
    // First, we need to inject over the existing functions.
    // Second, we need *our own* RNG. Let's start with something that returns statically for now.

    // There should be 3 functions to overwrite, and we have the 3 addresses for them:
    __int64 randomValue = sigScans[0].targetFunction; // UnityEngine::Random::Random.value => [0.0, 1.0]
    __int64 randomIntRange = sigScans[10].targetFunction; // UnityEngine::Random::Random.Range(int minInclusive, int maxExclusive) => [min, max)
    __int64 randomFloatRange = sigScans[20].targetFunction; // UnityEngine::Random::Random.Range(float minInclusive, float maxInclusive) => [min, max]


    constexpr byte IntRngInstructions[] = {
        // uh oh.
        0x00,
    };


    constexpr byte floatRngInstructions[] = {
        // We can compute floats as a function of ints, and just do some *math*
        // [xmm0, xmm1]
        // e.g. [0.5, 2.5] == [0, 65535] / (65536/2.0) * 
        0x00,
    };

    uintptr_t floatRngFunction = memory->AllocateArray(sizeof(floatRngInstructions));


    memory->WriteData<byte>({randomValue}, {
        0xB1, 0x00,                                                 // mov cl, 0   ; RngClass.Unknown
        SKIP(0xB1, 0x01),                                           // mov cl, 1   ; RngClass.DoNotTamper
        SKIP(0xB1, 0x02),                                           // mov cl, 2   ; RngClass.BirdPathing
        SKIP(0xB1, 0x03),                                           // mov cl, 3   ; RngClass.Rarity
        SKIP(0xB1, 0x04),                                           // mov cl, 4   ; RngClass.Drafting
        SKIP(0xB1, 0x05),                                           // mov cl, 5   ; RngClass.Items
        SKIP(0xB1, 0x06),                                           // mov cl, 6   ; RngClass.DogSwapper
        SKIP(0xB1, 0x07),                                           // mov cl, 7   ; RngClass.Trading
        SKIP(0xB1, 0x08),                                           // mov cl, 8   ; RngClass.Derigiblock
        SKIP(0xB1, 0x09),                                           // mov cl, 9   ; RngClass.SlotMachine

        // xmm0 = 0.0f
        // xmm1 = 1.0f
        // call internal rand range function
                                                                    // jmp 0x20
        0x48, 0x83, 0xEC, 0x10,                                     // sub rsp, 0x10                    ; Allocate some stack space
        0xC7, 0x44, 0x24, 0x00, FLOAT_TO_BYTES(0.0f),               // mov dword ptr ss:[rsp], 0.0f	    ; Store a float on the stack (floats cannot be handled as immediate values)
        0xF3, 0x0F, 0x10, 0x04, 0x24,                               // movss xmm0, [rsp]                ; Move the float into xmm0
        0xC7, 0x44, 0x24, 0x00, FLOAT_TO_BYTES(1.0f),               // mov dword ptr ss:[rsp], 0.0f	    ; Store a float on the stack (floats cannot be handled as immediate values)
        0xF3, 0x0F, 0x10, 0x04, 0x24,                               // movss xmm1, [rsp]                ; Move the float into xmm1
        // mov rcx floatRngFunction
        // jmp rcx
    });












    return trainer;
}

// Restore default game settings when shutting down the trainer.
Trainer::~Trainer() {
}
