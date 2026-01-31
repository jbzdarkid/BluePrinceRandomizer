#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

Trainer::Trainer(const std::shared_ptr<Memory>& memory) : _memory(memory) {
    bool success = FindAllRngFunctions();
    assert(success);

    success = InjectCustomRng();
    assert(success);

    // Ok, so we did a bunch of sigscans. Now what?
    // First, we need to inject over the existing functions.
    // Second, we need *our own* RNG. Let's start with something that returns statically for now.
    // success = RedirectRngFunctions({ ... some mapping of settings here ... })
    // Third... we can maybe classify the mapping somewhere less hard-code-y? Maybe an auxiliary array in memory would be sufficient?
}

bool Trainer::InjectCustomRng() {
    // There should be 3 functions to overwrite, and we have the 3 addresses for them:
    __int64 randomValue = _sigScans1[0].targetFunction; // UnityEngine::Random::Random.value => [0.0, 1.0]
    __int64 randomIntRange = _sigScans2[0].targetFunction; // UnityEngine::Random::Random.Range(int minInclusive, int maxExclusive) => [min, max)
    __int64 randomFloatRange = _sigScans3[0].targetFunction; // UnityEngine::Random::Random.Range(float minInclusive, float maxInclusive) => [min, max]

    __int64 rngSeedMemory = _memory->AllocateArray(RngClass::NumEntries * sizeof(__int64));

    std::vector<byte> intRngInstructions = {
        // Each category should have its own seed value
        // Then, we can hard-code what we want each category to *do*
        // We might choose to encode this somehow? I haven't decided on the plan. Actually, I think I got it. Allocate an array with a "type" for each classification.
        // - msvc random (https://github.com/jbzdarkid/SSRBruteForce/blob/main/State.cpp#L11)
        // - Sequential value
        // - Constant value
        // (each of these will be preset into the "seed memory"
        0x00,
        // mov rax, [seed value + category] ? don't do this. We've got all the space in the world. Make this as readable as possible.
        // switch based on hard-coded category handlers
        // case 1: constant value
        // (jump to range math)
        // case 2: sequential value
        // inc [seed value + category]
        // jmp to end
        // mov rax, [seed value]
        // inc [seed value]
        // rax = combine_hash(rax, category) <-- kill this, instead use the msvc_hash which takes an array of bytes.
        // label end:
        // modulo by the range
        // add the low value
        // return
    };

    __int64 intRngFunction = _memory->AllocateArray(intRngInstructions.size());
    _memory->WriteData<byte>({intRngFunction}, intRngInstructions);

    std::vector<byte> floatRngInstructions = {
        0x48, 0x83, 0xC4, 0x10,                                     // add rsp, 10                      ; Allocate space for our local variables, while staying fpu aligned
        0xF3, 0x0F, 0x10, 0x14, 0x24,                               // movss xmm2, [rsp]                ; Save xmm2 (we need this for scratch space)
        0x48, 0xB8, LONG_TO_BYTES(intRngFunction),                  // mov rax, intRngFunction          ; Load the address of our integer RNG function
        // TODO: Set args for int rng function
        0xFF, 0xD0,                                                 // call rax                         ; Call it with range [0, 65536)
        0x89, 0x44, 0x24, 0x08,                                     // mov [rsp+8], eax                 ; Move the return value onto the stack
        0xF3, 0x0F, 0x10, 0x54, 0x24, 0x08,                         // movss xmm2, [rsp+8]              ; so we can move it into a float
        0x0F, 0x5B, 0xD2,                                           // cvtdq2ps xmm2, xmm2              ; and convert it from an integer to a float
        0xC7, 0x44, 0x24, 0x0C, FLOAT_TO_BYTES(65536),              // mov [rsp+C], 65536f              ; Move a float into our stack
        0xF3, 0x0F, 0x5E, 0x54, 0x24, 0x0C,                         // divss xmm2, [rsp+C]              ; Divide the random value to get a value in [0.0f, 1.0f)
        0xF3, 0x0F, 0x5C, 0xC8,                                     // subss xmm1, xmm0                 ; Determine the requested float range
        0xF3, 0x0F, 0x59, 0xD1,                                     // mulss xmm2, xmm1                 ; Scale up our random value to the size of the float range
        0xF3, 0x0F, 0x58, 0xC2,                                     // addss xmm0, xmm2                 ; Add the random value to the minimum to get our final result in xmm0
        0xF3, 0x0F, 0x10, 0x14, 0x24,                               // movss xmm2, [rsp]                ; Restore xmm2 from our saved location
        0x48, 0x83, 0xEC, 0x10,                                     // sub rsp, 10                      ; Restore the stack pointer (freeing our local variables)
        0xC3,                                                       // ret                              ;
    };

    __int64 floatRngFunction = _memory->AllocateArray(sizeof(floatRngInstructions));
    _memory->WriteData<byte>({floatRngFunction}, floatRngInstructions);

    _memory->WriteData<byte>({randomValue}, {
        0xB1, 0x00,                                                 // mov cl, 0                        ; RngClass.Unknown
        SKIP(0xB1, 0x01),                                           // mov cl, 1                        ; RngClass.DoNotTamper
        SKIP(0xB1, 0x02),                                           // mov cl, 2                        ; RngClass.BirdPathing
        SKIP(0xB1, 0x03),                                           // mov cl, 3                        ; RngClass.Rarity
        SKIP(0xB1, 0x04),                                           // mov cl, 4                        ; RngClass.Drafting
        SKIP(0xB1, 0x05),                                           // mov cl, 5                        ; RngClass.Items
        SKIP(0xB1, 0x06),                                           // mov cl, 6                        ; RngClass.DogSwapper
        SKIP(0xB1, 0x07),                                           // mov cl, 7                        ; RngClass.Trading
        SKIP(0xB1, 0x08),                                           // mov cl, 8                        ; RngClass.Derigiblock
        SKIP(0xB1, 0x09),                                           // mov cl, 9                        ; RngClass.SlotMachine

        0x48, 0x83, 0xC4, 0x10,                                     // add rsp, 10                      ; Allocate space for our local variables, while staying fpu aligned
        0xC7, 0x44, 0x24, 0x00, FLOAT_TO_BYTES(0.0f),               // mov dword ptr ss:[rsp], 0.0f	    ; Store a float on the stack (floats cannot be handled as immediate values)
        0xF3, 0x0F, 0x10, 0x04, 0x24,                               // movss xmm0, [rsp]                ; Move the float into xmm0
        0xC7, 0x44, 0x24, 0x00, FLOAT_TO_BYTES(1.0f),               // mov dword ptr ss:[rsp], 0.0f	    ; Store a float on the stack (floats cannot be handled as immediate values)
        0xF3, 0x0F, 0x10, 0x04, 0x24,                               // movss xmm1, [rsp]                ; Move the float into xmm1
        0x48, 0x83, 0xEC, 0x10,                                     // sub rsp, 0x10                    ; Restore the stack pointer (freeing our local variables)
        0x48, 0xB8, LONG_TO_BYTES(floatRngFunction),                // mov rax, intRngFunction          ; Load the address of our integer RNG function
        0xFF, 0xE0,                                                 // jmp rax                          ; Jump to it (tail call elision)
    });


    __debugbreak();
    return true;
}

bool Trainer::FindAllRngFunctions() {
    // I have (painstakingly) generated a bunch of sigscans for all the BluePrince code locations which are calling into the RNG.
    // They are categorized on two dimensions:
    // - First, by the function they're using. This is important for our injection; each function class has a particular expected return type that we must match.
    // - Second, by the usage. This is important for modding, since we care about some of these random values more so than others.
    // I have also annotated the sigscan by the name of the calling function, to help justify the categorization.

    // UnityEngine::Random::Random.value => [0.0, 1.0]
    _sigScans1 = {
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
    };

    // UnityEngine::Random::Random.Range(int minInclusive, int maxExclusive) => [min, max)
    _sigScans2 = {
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
    };

    // UnityEngine::Random::Random.Range(float minInclusive, float maxInclusive) => [min, max]
    _sigScans3 = {
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
        { RngClass::DoNotTamper, "EB 2B F3 44 0F 10 84 24 90 00 00 00", -4 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.Refresh()
        { RngClass::DoNotTamper, "EB 2F F3 0F 10 BC 24 90 00 00 00", -4 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.Refresh()
        { RngClass::DoNotTamper, "44 0F 28 D8 E9 80 00 00 00", -4 }, // bool FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.AddGroupItems()
        { RngClass::DoNotTamper, "44 0F 28 D8 EB 3A", -4 }, // bool FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.AddGroupItems()
        { RngClass::DoNotTamper, "44 0F 28 D0 E9 80 00 00 00", -4 }, // bool FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.AddGroupItems()
        { RngClass::DoNotTamper, "44 0F 28 D0 EB 3A", -4 }, // bool FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.AddGroupItems()
        // 0x18208fbc9 is next



        // There are 79 of these. Strap in.


    };

    for (auto& sigScan : _sigScans1) {
        _memory->AddSigScan(sigScan.GetScanBytes(), [&sigScan](int64_t offset, int index, const std::vector<uint8_t>& data) {
            sigScan.foundAddress = offset + index + sigScan.offsetFromScan;
            sigScan.targetFunction = Memory::ReadStaticInt(offset, index + sigScan.offsetFromScan, data);
        });
    }
    for (auto& sigScan : _sigScans2) {
        _memory->AddSigScan(sigScan.GetScanBytes(), [&sigScan](int64_t offset, int index, const std::vector<uint8_t>& data) {
            sigScan.foundAddress = offset + index + sigScan.offsetFromScan;
            sigScan.targetFunction = Memory::ReadStaticInt(offset, index + sigScan.offsetFromScan, data);
        });
    }
    for (auto& sigScan : _sigScans3) {
        _memory->AddSigScan(sigScan.GetScanBytes(), [&sigScan](int64_t offset, int index, const std::vector<uint8_t>& data) {
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

    size_t numFailedScans = _memory->ExecuteSigScans();
    if (numFailedScans != 0) return false; // Sigscans failed, we'll try again later.

    // Double check that each group of sigscans hits the same target function (i.e. verifying the AOB and offsets are still lining up)
    // This also prevents re-randomization.
    for (const auto& sigScan : _sigScans1) {
        if (sigScan.targetFunction != _sigScans1[0].targetFunction) return false;
    }
    for (const auto& sigScan : _sigScans2) {
        if (sigScan.targetFunction != _sigScans2[0].targetFunction) return false;
    }
    for (const auto& sigScan : _sigScans3) {
        if (sigScan.targetFunction != _sigScans3[0].targetFunction) return false;
    }

    return true;
}
