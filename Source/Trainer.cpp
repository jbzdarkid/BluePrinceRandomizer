#include "pch.h"
#include "Trainer.h"
#include "Panels.h"

std::shared_ptr<Trainer> Trainer::Create(const std::shared_ptr<Memory>& memory) {
    auto trainer = std::shared_ptr<Trainer>(new Trainer());
    trainer->_memory = memory;

    bool success = true;

#if DERANDOMIZE
    success = trainer->FindAllRngFunctions();
    if (!success) return nullptr;

    trainer->InjectCustomRng();
    trainer->OverwriteRngFunctions();
#endif

    trainer->InjectDraftWatcher();

    return trainer;
}

void Trainer::InjectCustomRng() {
    _rngSeedArray = _memory->AllocateArray(RngClass::NumEntries * sizeof(__int64));
    _rngBehaviors = _memory->AllocateArray(RngClass::NumEntries * sizeof(byte));

    // Each category has its own seed value.
    // Each category also has its own "behavior", which is one of these cases:
    // - Case 1: Return the seed value, unchanged
    // - Case 2: Return the seed value, then increment the seed value
    // - Case 3: Return the seed value, then PRNG shuffle the seed value
    // The PRNG is adapted from MSVC's type_traits hashing implementation, see:
    // https://github.com/microsoft/STL/blob/main/stl/inc/type_traits#L2407
    std::vector<byte> intRngInstructions = {
        0x56,                                                   // push rsi                         ; Preserve the values of rsi and rdi (we will use them as scratch registers)
        0x57,                                                   // push rdi                         ;
        0x48, 0x31, 0xC0,                                       // xor rax, rax                     ; Reset the return value to 0 (just in case)
        0x4D, 0x0F, 0xB6, 0xC0,                                 // movzx r8, r8b                    ; Clear any high bits on r8 (we used r8b to save our "category")
        0x48, 0xBE, LONG_TO_BYTES(_rngBehaviors),               // mov rsi, _rngBehaviors           ; Load in the lookup table
        IF_EQ(0x42, 0x80, 0x3C, 0x06, RngBehavior::Constant),   // cmp byte ptr [rsi + r8], 1       ; If this RNG category is using type 1 (fixed value)
        THEN(                                                   //                                  ; Case 1 {
            0x48, 0xBF, LONG_TO_BYTES(_rngSeedArray),           // mov rdi, _rngSeedArray           ;     Load in the table of RNG seeds
            0x4A, 0x8B, 0x34, 0xC7,                             // mov rsi, qword ptr [rdi + r8*8]  ;     Look up the seed for this RNG category
            0x48, 0x89, 0xF0                                    // mov rax, rsi                     ;     Save the seed as the return value (rax)
        ),                                                      //                                  ; }
        0x48, 0xBE, LONG_TO_BYTES(_rngBehaviors),               // mov rsi, _rngBehaviors           ; Load in the lookup table
        IF_EQ(0x42, 0x80, 0x3C, 0x06, RngBehavior::Increment),  // cmp byte ptr [rsi + r8], 2       ; If this RNG category is using type 2 (steadily increasing value)
        THEN(                                                   //                                  ; Case 2 {
            0x48, 0xBF, LONG_TO_BYTES(_rngSeedArray),           // mov rdi, _rngSeedArray           ;     Load in the table of RNG seeds
            0x4A, 0x8B, 0x34, 0xC7,                             // mov rsi, qword ptr [rdi + r8*8]  ;     Look up the seed for this RNG category
            0x48, 0x89, 0xF0,                                   // mov rax, rsi                     ;     Save the seed as the return value (rax)
            0x48, 0xFF, 0xC6,                                   // inc rsi                          ;     Increment the seed
            0x4A, 0x89, 0x34, 0xC7                              // mov qword ptr [rdi + r8*8], rsi  ;     Save back the incremented seed value
        ),                                                      //                                  ; }
        0x48, 0xBE, LONG_TO_BYTES(_rngBehaviors),               // mov rsi, _rngBehaviors           ; Load in the lookup table
        IF_EQ(0x42, 0x80, 0x3C, 0x06, RngBehavior::Randomize),  // cmp byte ptr [rsi + r8], 3       ; If this RNG category is using type 3 (pseudorandom value)
        THEN(                                                   //                                  ; Case 3 {
            0x48, 0xBF, LONG_TO_BYTES(_rngSeedArray),           // mov rdi, _rngSeedArray           ;     Load in the table of RNG seeds
            0x4A, 0x8B, 0x34, 0xC7,                             // mov rsi, qword ptr [rdi + r8*8]  ;     Look up the seed for this RNG category
            0x56,                                               // push rsi                         ;     Add our RNG seed data to the hash buffer
            0x48, 0xBE, LONG_TO_BYTES(14695981039346656037),    // mov rsi, 14695981039346656037    ;     rsi = _FNV_offset_basis
            0x48, 0xBF, LONG_TO_BYTES(1099511628211),           // mov rdi, 1099511628211           ;     rdi = _FNV_prime
            0x48, 0xC7, 0xC0, INT_TO_BYTES(8),                  // mov rax, 8                       ;     rax = 8                   // We pushed an 8-byte register onto the stack, so the hash buffer size is 8.
            DO_WHILE_NONZERO(                                   //                                  ;     do {
                0x40, 0x32, 0x74, 0x04, 0xF8,                   // xor sil, byte ptr [rsp+rax-8]    ;         rdi = [rsp + rax - 8] // XOR in a byte from the buffer
                0x48, 0x0F, 0xAF, 0xF7,                         // imul rsi, rdi                    ;         rsi *= rdi            // Multiply in a large prime
                0x48, 0xFF, 0xC8                                // dec rax                          ;         rax--                 // Decrement loop variable (buffer size)
            ),                                                  //                                  ;     } while (rax > 0)
            0x48, 0x83, 0xC4, 0x8,                              // add rsp, 8                       ;     Restore the stack pointer (freeing our hash buffer)
            0x48, 0xBF, LONG_TO_BYTES(_rngSeedArray),           // mov rdi, _rngSeedArray           ;     Load in the table of RNG seeds
            0x4A, 0x8B, 0x04, 0xC7,                             // mov rax, qword ptr [rdi + r8*8]  ;     Save the *previous* seed value as the return value (rax)
            0x4A, 0x89, 0x34, 0xC7                              // mov qword ptr [rdi + r8*8], rsi  ;     Save back the *new* seed value
        ),                                                      //                                  ; }
        0x89, 0xD6,                                             // mov esi, edx                     ; Copy out the upper limit into esi
        0x29, 0xCE,                                             // sub esi, ecx                     ; Subtract the lower limit to compute the range
        0x31, 0xD2,                                             // xor edx, edx                     ; Zero out edx (required for division, or in case esi is 0)
        IF_NZ(0x85, 0xF6),                                      // test esi, esi                    ; Compare esi to itself
        THEN(                                                   //                                  ; if (esi != 0) {
            0xF7, 0xF6                                          // div esi                          ;   Compute edx = (edx:eax) % esi
        ),                                                      //                                  ; }
        0x89, 0xC8,                                             // mov eax, ecx                     ; Copy the lower limit into eax
        0x01, 0xD0,                                             // add eax, edx                     ; Add the remainder into eax (our return value)
        0x5F,                                                   // pop rdi                          ; Restore our scratch registers
        0x5E,                                                   // pop rsi                          ;
        0xC3,                                                   // ret                              ;
    };

    _intRngFunction = _memory->AllocateArray(intRngInstructions.size());
    _memory->WriteData<byte>({_intRngFunction}, intRngInstructions);

    std::vector<byte> floatRngInstructions = {
        0x51,                                                   // push rcx                         ; Preserve rcx and rdx
        0x52,                                                   // push rdx                         ;
        0x48, 0x83, 0xEC, 0x10,                                 // sub rsp, 10                      ; Allocate space for our local variables, while staying fpu aligned
        0xF3, 0x0F, 0x11, 0x14, 0x24,                           // movss [rsp], xmm2                ; Save xmm2 (we need this for scratch space)
        0xB9, INT_TO_BYTES(0x0000),                             // mov ecx, 0                       ; Set the arguments for the integer RNG function
        0xBA, INT_TO_BYTES(0xFFFF),                             // mov edx, 65536                   ;
        0x48, 0xB8, LONG_TO_BYTES(_intRngFunction),             // mov rax, _intRngFunction         ; Load the address of our integer RNG function
        0xFF, 0xD0,                                             // call rax                         ; rax = Call it with range [0, 65536)
        0x89, 0x44, 0x24, 0x08,                                 // mov [rsp+8], eax                 ; Move the return value onto the stack
        0xF3, 0x0F, 0x10, 0x54, 0x24, 0x08,                     // movss xmm2, [rsp+8]              ; so we can move it into a float
        0x0F, 0x5B, 0xD2,                                       // cvtdq2ps xmm2, xmm2              ; and convert it from an integer to a float
        0xC7, 0x44, 0x24, 0x0C, INT_TO_BYTES(0x47800000),       // mov [rsp+C], 65536.0f            ; Move the max range into our stack (as a float)
        0xF3, 0x0F, 0x5E, 0x54, 0x24, 0x0C,                     // divss xmm2, [rsp+C]              ; Divide the random value to get a value in [0.0f, 1.0f)
        0xF3, 0x0F, 0x5C, 0xC8,                                 // subss xmm1, xmm0                 ; Determine the requested float range
        0xF3, 0x0F, 0x59, 0xD1,                                 // mulss xmm2, xmm1                 ; Scale up our random value to the size of the float range
        0xF3, 0x0F, 0x58, 0xC2,                                 // addss xmm0, xmm2                 ; Add the random value to the minimum to get our final result in xmm0
        0xF3, 0x0F, 0x10, 0x14, 0x24,                           // movss xmm2, [rsp]                ; Restore xmm2 from our saved location
        0x48, 0x83, 0xC4, 0x10,                                 // add rsp, 10                      ; Restore the stack pointer (freeing our local variables)
        0x5A,                                                   // pop rdx                          ; Restore rcx and rdx
        0x59,                                                   // pop rcx                          ;
        0xC3,                                                   // ret                              ;
    };

    _floatRngFunction = _memory->AllocateArray(sizeof(floatRngInstructions));
    _memory->WriteData<byte>({_floatRngFunction}, floatRngInstructions);
}

bool Trainer::FindAllRngFunctions() {
    // I have (painstakingly) generated a bunch of sigscans for all the BluePrince code locations which are calling into the RNG.
    // They are categorized on two dimensions:
    // - First, by the function they're using. This is important for our injection; each function class has a particular expected return type that we must match.
    // - Second, by the usage. This is important for modding, since we care about some of these random values more so than others.
    // I have also annotated the sigscan by the name of the calling function, to help justify the categorization.

    /* TODO: Some call sites use this horrific IL2CPP behavior. I need to rescan for these and... write more injection. Eventually.
      v95 = qword_18318CDC8;
      if (!qword_18318CDC8) {
        v95 = il2cpp_resolve_icall_0("UnityEngine.Random::RandomRangeInt(System.Int32,System.Int32)");
        if (!v95) {
          v102 = sub_1802C0F70("UnityEngine.Random::RandomRangeInt(System.Int32,System.Int32)");
          sub_1802BF150(v102); // some assert
        }
        qword_18318CDC8 = v95;
      }
      v96 = v95(0, v94); // <-- actual function call here
    */

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
        { RngClass::DoNotTamper, "0F 28 F0 EB 69", -4 }, // BuildVolumeSpots FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetSpot()
        { RngClass::DoNotTamper, "0F 28 F0 EB 38", -4 }, // BuildVolumeSpots FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetSpot()
        { RngClass::DoNotTamper, "1E F3 0F 10 49 04", 14 }, // float FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetRegionNextValue()
        { RngClass::DoNotTamper, "44 0F 28 C0 EB 3A", -4 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS()
        { RngClass::DoNotTamper, "0F 28 F8 EB 39", -4 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS()
        { RngClass::DoNotTamper, "0F 29 B4 24 C0 00 00 00 89", 48 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS()
        { RngClass::DoNotTamper, "75 1D F3 0F 10 4B 60", 22 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS()
        { RngClass::DoNotTamper, "75 1D F3 0F 10 4B 6C", 22 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS()
        { RngClass::DoNotTamper, "75 1D F3 0F 10 4B 78", 22 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS()
        { RngClass::DoNotTamper, "75 23 F3 0F 10 8B B0 00 00 00", 28 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS()
        { RngClass::DoNotTamper, "75 23 F3 0F 10 8B BC 00 00 00", 28 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS()
        { RngClass::DoNotTamper, "75 20 F3 0F 10 8B C8 00 00 00", 25 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS()
        { RngClass::DoNotTamper, "75 0D F3 0F 10 4D 8B", 11 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS630()
        { RngClass::DoNotTamper, "75 46 F3 0F 10 4D 9B", 11 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS630()
        { RngClass::DoNotTamper, "F3 0F 11 46 08 66", -11 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS630()
        { RngClass::DoNotTamper, "F3 0F 11 07 66 0F 7F 75 B7", -11 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS630()
        { RngClass::DoNotTamper, "F3 0F 11 47 04 66", -11 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS630()
        { RngClass::DoNotTamper, "F3 0F 11 47 08 66", -11 }, // void FluffyUnderware::Curvy::Generator::Modules::BuildVolumeSpots::BuildVolumeSpots.GetTRS630()
    };

    for (auto& sigScan : _sigScans1) {
        _memory->AddSigScan(sigScan.scanHex, [&sigScan](int64_t offset, int index, const std::vector<uint8_t>& data) {
            sigScan.foundAddress = offset + index + sigScan.offsetFromScan;
            sigScan.targetFunction = Memory::ReadStaticInt(offset, index + sigScan.offsetFromScan, data);
        });
    }
    for (auto& sigScan : _sigScans2) {
        _memory->AddSigScan(sigScan.scanHex, [&sigScan](int64_t offset, int index, const std::vector<uint8_t>& data) {
            sigScan.foundAddress = offset + index + sigScan.offsetFromScan;
            sigScan.targetFunction = Memory::ReadStaticInt(offset, index + sigScan.offsetFromScan, data);
        });
    }
    for (auto& sigScan : _sigScans3) {
        _memory->AddSigScan(sigScan.scanHex, [&sigScan](int64_t offset, int index, const std::vector<uint8_t>& data) {
            sigScan.foundAddress = offset + index + sigScan.offsetFromScan;
            sigScan.targetFunction = Memory::ReadStaticInt(offset, index + sigScan.offsetFromScan, data);
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

void Trainer::OverwriteRngFunctions() {
    __int64 randomIntRange = _sigScans2[0].targetFunction; // UnityEngine::Random::Random.Range(int minInclusive, int maxExclusive) => [min, max)
    _memory->WriteData<byte>({randomIntRange}, {
        0x41, 0xB0, 0x00,                                           // mov r8b, 0                   ; RngClass.Unknown
        SKIP(0x41, 0xB0, 0x01),                                     // mov r8b, 1                   ; RngClass.DoNotTamper
        SKIP(0x41, 0xB0, 0x02),                                     // mov r8b, 2                   ; RngClass.BirdPathing
        SKIP(0x41, 0xB0, 0x03),                                     // mov r8b, 3                   ; RngClass.Rarity
        SKIP(0x41, 0xB0, 0x04),                                     // mov r8b, 4                   ; RngClass.Drafting
        SKIP(0x41, 0xB0, 0x05),                                     // mov r8b, 5                   ; RngClass.Items
        SKIP(0x41, 0xB0, 0x06),                                     // mov r8b, 6                   ; RngClass.DogSwapper
        SKIP(0x41, 0xB0, 0x07),                                     // mov r8b, 7                   ; RngClass.Trading
        SKIP(0x41, 0xB0, 0x08),                                     // mov r8b, 8                   ; RngClass.Derigiblock
        SKIP(0x41, 0xB0, 0x09),                                     // mov r8b, 9                   ; RngClass.SlotMachine

        0x48, 0xB8, LONG_TO_BYTES(_intRngFunction),                 // mov rax, intRngFunction      ; Load the address of the generic floating-point function, after RngClass is set
        0xFF, 0xE0,                                                 // jmp rax                      ; Jump to it (tail call elision)
    });

    for (const auto& sigScan : _sigScans2) {
        // TODO: Explain math.
        _memory->WriteData<int>({sigScan.foundAddress}, {(int)(sigScan.targetFunction - sigScan.foundAddress - 4 + 5 * sigScan.rngClass)});
    }

    __int64 randomFloatRange = _sigScans3[0].targetFunction; // UnityEngine::Random::Random.Range(float minInclusive, float maxInclusive) => [min, max]
    _memory->WriteData<byte>({randomFloatRange}, {
        0x41, 0xB0, 0x00,                                           // mov r8b, 0                   ; RngClass.Unknown
        SKIP(0x41, 0xB0, 0x01),                                     // mov r8b, 1                   ; RngClass.DoNotTamper
        SKIP(0x41, 0xB0, 0x02),                                     // mov r8b, 2                   ; RngClass.BirdPathing
        SKIP(0x41, 0xB0, 0x03),                                     // mov r8b, 3                   ; RngClass.Rarity
        SKIP(0x41, 0xB0, 0x04),                                     // mov r8b, 4                   ; RngClass.Drafting
        SKIP(0x41, 0xB0, 0x05),                                     // mov r8b, 5                   ; RngClass.Items
        SKIP(0x41, 0xB0, 0x06),                                     // mov r8b, 6                   ; RngClass.DogSwapper
        SKIP(0x41, 0xB0, 0x07),                                     // mov r8b, 7                   ; RngClass.Trading
        SKIP(0x41, 0xB0, 0x08),                                     // mov r8b, 8                   ; RngClass.Derigiblock
        SKIP(0x41, 0xB0, 0x09),                                     // mov r8b, 9                   ; RngClass.SlotMachine

        0x48, 0xB8, LONG_TO_BYTES(_floatRngFunction),               // mov rax, _floatRngFunction   ; Load the address of the generic floating-point function, after RngClass is set
        0xFF, 0xE0,                                                 // jmp rax                      ; Jump to it (tail call elision)
    });

    for (const auto& sigScan : _sigScans3) {
        _memory->WriteData<int>({sigScan.foundAddress}, {(int)(sigScan.targetFunction - sigScan.foundAddress - 4 + 5 * sigScan.rngClass)});
    }

    __int64 randomValue = _sigScans1[0].targetFunction; // UnityEngine::Random::Random.value => [0.0, 1.0]
    _memory->WriteData<byte>({randomValue}, {
        0x41, 0xB0, 0x00,                                           // mov r8b, 0                   ; RngClass.Unknown
        SKIP(0x41, 0xB0, 0x01),                                     // mov r8b, 1                   ; RngClass.DoNotTamper
        SKIP(0x41, 0xB0, 0x02),                                     // mov r8b, 2                   ; RngClass.BirdPathing
        SKIP(0x41, 0xB0, 0x03),                                     // mov r8b, 3                   ; RngClass.Rarity
        SKIP(0x41, 0xB0, 0x04),                                     // mov r8b, 4                   ; RngClass.Drafting
        SKIP(0x41, 0xB0, 0x05),                                     // mov r8b, 5                   ; RngClass.Items
        SKIP(0x41, 0xB0, 0x06),                                     // mov r8b, 6                   ; RngClass.DogSwapper
        SKIP(0x41, 0xB0, 0x07),                                     // mov r8b, 7                   ; RngClass.Trading
        SKIP(0x41, 0xB0, 0x08),                                     // mov r8b, 8                   ; RngClass.Derigiblock
        SKIP(0x41, 0xB0, 0x09),                                     // mov r8b, 9                   ; RngClass.SlotMachine

        0x48, 0x83, 0xEC, 0x10,                                     // sub rsp, 0x10                ; Allocate space for our local variables, while staying fpu aligned
        0xC7, 0x44, 0x24, 0x00, INT_TO_BYTES(0x00000000),           // mov dword ptr [rsp], 0.0f	; Store a float on the stack (floats cannot be handled as immediate values)
        0xF3, 0x0F, 0x10, 0x04, 0x24,                               // movss xmm0, [rsp]            ; Move the float into xmm0
        0xC7, 0x44, 0x24, 0x00, INT_TO_BYTES(0x3f800000),           // mov dword ptr [rsp], 1.0f	; Store a float on the stack (floats cannot be handled as immediate values)
        0xF3, 0x0F, 0x10, 0x0C, 0x24,                               // movss xmm1, [rsp]            ; Move the float into xmm1
        0x48, 0x83, 0xC4, 0x10,                                     // add rsp, 10                  ; Restore the stack pointer (freeing our local variables)
        0x48, 0xB8, LONG_TO_BYTES(_floatRngFunction),               // mov rax, _floatRngFunction   ; Load the address of the generic floating-point function, after RngClass is set
        0xFF, 0xE0,                                                 // jmp rax                      ; Jump to it (tail call elision)
    });

    for (const auto& sigScan : _sigScans1) {
        _memory->WriteData<int>({sigScan.foundAddress}, {(int)(sigScan.targetFunction - sigScan.foundAddress - 4 + 5 * sigScan.rngClass)});
    }
}

void Trainer::InjectDraftWatcher() {
    int64_t pickRoomFromSlot = 0;
    int64_t pickTop = 0;
    _memory->AddSigScan("48 8B 4C C1 20 48 85 C9 74 1D 45", [&](int64_t offset, int index, const std::vector<uint8_t>& data) {
        pickRoomFromSlot = offset + index;
        pickTop = Memory::ReadStaticInt(offset, index + 0x10, data);
    });
    int64_t getRoomByName = 0;
    int64_t createCard = 0;
    _memory->AddSigScan("75 29 48 8B 4B 10", [&](int64_t offset, int index, const std::vector<uint8_t>& data) {
        getRoomByName = Memory::ReadStaticInt(offset, index + 0x16, data);
        createCard = Memory::ReadStaticInt(offset, index + 0x24, data);
    });

    size_t numFailedScans = _memory->ExecuteSigScans();
    if (numFailedScans != 0) return;

    _buffer = _memory->AllocateArray(0x1'000'000); // This is *way* too big, but what the hell ever. We can afford to allocate 1MB to avoid having to think about running out of buffer space.
    _memory->WriteData<int64_t>({(__int64)_buffer}, {_bufferPosition}); // Write initial size to skip past the reserved initial spots

    _memory->Intercept("PickRoomFromSlot", pickRoomFromSlot, pickRoomFromSlot + 20, {
        0x51,                                       // push rcx                             ;
        0x52,                                       // push rdx                             ;
        0x56,                                       // push rsi                             ;
        0x57,                                       // push rdi                             ;
        0x41, 0x50,                                 // push r8                              ;
        0x41, 0x51,                                 // push r9                              ;
        0x41, 0x52,                                 // push r10                             ;
        0x41, 0x53,                                 // push r11                             ;
        0x41, 0x54,                                 // push r12                             ;
        0x41, 0x55,                                 // push r13                             ;
        0x41, 0x56,                                 // push r14                             ;
        0x41, 0x57,                                 // push r15                             ; Push a bunch of registers so we're free to use r8-15 as needed.
        0x48, 0x8B, 0x4C, 0xC1, 0x20,               // mov rcx,qword ptr ds:[rcx+rax*8+20]  ; rcx = RoomDeck (This is the computation the game uses to determine the correct deck.)
        0x4C, 0x8B, 0x41, 0x20,                     // mov r8,qword ptr ds:[rcx+10]         ; r8 = RoomDeck.FilteredDeck
        0x45, 0x8B, 0x48, 0x18,                     // mov r9d,qword ptr ds:[r8+18]         ; r9d = List<RoomCard>._size
        IF_GT(0x45, 0x85, 0xC9),                    // test r9d,r9d                         ;
        THEN(                                       // if (r9d > 0) {                       ; if (r9d > 0) {
          0x4D, 0x8B, 0x50, 0x10,                   //   mov r10,qword ptr ds:[r8+10]       ;   r10 = List<RoomCard>._items
          0x49, 0x83, 0xC2, 0x20,                   //   add r10,20                         ;   r10 = &_items.vector (the actual data pointer inside a List<T>)
          0x49, 0xBF, LONG_TO_BYTES(_buffer),       //   mov r15,_buffer                    ;   r15 = _buffer (a shared memory buffer, we will read from here when the game is done choosing decks
          0x4D, 0x03, 0x3F,                         //   add r15,qword ptr ds:[r15]         ;   r15 += [r15] (adjust forward to account for the current buffer size)
          DO_WHILE_NONZERO(                         //   do {                               ;   Iterate over all the cards
            0x4D, 0x8B, 0x1A,                       //     mov r11,qword ptr ds:[r10]       ;     r11 = [r10] (This loads the item at index r9, which is directly pointed to by r10)
            0x4D, 0x8B, 0x5B, 0x10,                 //     mov r11,qword ptr ds:[r11+10]    ;     r11 = RoomCard.Template
            0x4D, 0x8B, 0x5B, 0x48,                 //     mov r11,qword ptr ds:[r11+48]    ;     r11 = RoomTemplate.Headline
            0x49, 0x83, 0xC3, 0x14,                 //     add r11,14                             r11 = &Headline._firstChar
            DO_WHILE_NONZERO(                       //     do {                             ;     Iterate over all the chars
              0x66, 0x45, 0x8B, 0x23,               //       mov r12w,word ptr ds:[r11]     ;       r12w = [r11] (dereferencing the wide character in the string)
              0x66, 0x45, 0x89, 0x27,               //       mov word ptr ds:[r15],r12w     ;       [r15] = r12w (write the wide character into the buffer)
              0x49, 0x83, 0xC3, 0x02,               //       add r11,2                      ;       r11 += 2 (adjust the string by one wide character)
              0x49, 0x83, 0xC7, 0x02,               //       add r15,2                      ;       r15 += 2 (adjust the buffer pointer by one wide character)
              0x66, 0x45, 0x85, 0xE4                //       test r12w,r12w                 ;       check if we reached a null terminator
            ),                                      //     }                                ;     (done copying string)
            0x49, 0x83, 0xC2, 0x08,                 //     add r10,8                        ;     r10 += 8 (increment to the next card in the list)
            0x41, 0xFF, 0xC9                        //     dec r9d                          ;     r9d-- (decrement the number of cards remaining)
          ),                                        //   }                                  ;   (done iterating through cards)
          0x49, 0x83, 0xC7, 0x02,                   //   add r15,2                          ;   r15 += 2 (add a null terminator to indicate end of a deck)
          0x49, 0xBE, LONG_TO_BYTES(_buffer),       //   mov r14,_buffer                    ;   r14 = _buffer
          0x4D, 0x29, 0xF7,                         //   sub r15,r14                        ;   r15 -= r14 (compute the delta from the end of the buffer)
          0x4D, 0x89, 0x3E                          //   mov qword ptr ds:[r14],r15         ;   [r14] = r15 (write back the current buffer size)
        ),                                          // }                                    ; }
                                                    //                                      ;
                                                    //                                      ; This interception is not writing back the original code. As a result, we must handle it here.
                                                    //                                      ; We are also running logic here to allow for forced room choices.
                                                    //                                      ;
        0x49, 0xBF, LONG_TO_BYTES(_buffer),         // mov r15,_buffer                      ; r15 = _buffer (a shared memory buffer, we will read from here when the game is done choosing decks
        0x48, 0x8B, 0x8C, 0x24, INT_TO_BYTES(0x1A0),// mov rcx,qword ptr ss:[rsp+1A0]       ; rcx = [rsp + 0x1A0] (saved stack value of RoomDraftContext)
        0x8B, 0x49, 0x40,                           // mov ecx,qword ptr ds:[rcx+40]        ; ecx = RoomDraftContext.CurrentSlot
        0x4D, 0x8D, 0x3C, 0xCF,                     // lea r15,qword ptr ds:[r15+rcx*8]     ; r15 += rcx*8 (r15 = _buffer[currentSlot * 8])
        0x4D, 0x8B, 0x37,                           // mov r14,qword ptr ds:[r15]           ; r14 = [r15] (check to see if there's a card override at this slot)
        IF_NZ(0x4D, 0x85, 0xF6),                    // test r14,r14                         ;
        THEN(                                       // if (r14 != 0) {                      ; if (r14 != 0) {
          0x48, 0x8B, 0x8C, 0x24, INT_TO_BYTES(0x1A0),// mov rcx,qword ptr ss:[rsp+1A0]     ;   rcx = [rsp + 0x1A0] (saved stack value of RoomDraftContext)
          0x48, 0x8B, 0x49, 0x10,                   //   mov rcx,qword ptr ds:[rcx+10]      ;   rcx = RoomDraftContext.Database
          0x4C, 0x89, 0xF2,                         //   mov rdx,r14                        ;   rdx = r14 (our room name)
          0x49, 0xBB, LONG_TO_BYTES(getRoomByName), //   mov r11,getRoomByName              ;   r11 = &RoomDatabase.GetRoomByName
          0x41, 0xFF, 0xD3,                         //   call r11                           ;   RoomTemplate rax = RoomDatabase.GetRoomByName(database, name);
          IF_NZ(0x48, 0x85, 0xC0),                  //   test rax,rax                       ;   
          THEN(                                     //   if (rax != 0) {                    ;   if (rax != 0) {
            0x48, 0x8B, 0x8C, 0x24, INT_TO_BYTES(0x1A0),// mov rcx,qword ptr ss:[rsp+1A0]   ;     rcx = [rsp + 0x1A0] (saved stack value of RoomDraftContext)
            0x48, 0x89, 0xC2,                       //     mov rdx,rax                      ;     rdx = rax (pass the room template as arg 2)
            0x49, 0xBB, LONG_TO_BYTES(createCard),  //     mov r11,createCard               ;     r11 = &RoomDraftContext.CreateCard
            0x41, 0xFF, 0xD3                        //     call r11                         ;     RoomCard rax = RoomDraftContext.CreateCard(roomDraftContext, template);
          )                                         //   }                                  ;   } (We're done -- rax is now our return value)
        ),                                          // }                                    ; } (Done with override behavior)
        IF_Z(0x4D, 0x85, 0xF6),                     // test r14, r14                        ;
        THEN(                                       // if (r14 == 0) {                      ; if (r14 == 0) {
          0x45, 0x8B, 0x48, 0x18,                   //   mov r9d,qword ptr ds:[r8+18]       ;   r9d = List<RoomCard>._size (note: r8 is unmodified from our math above)
          IF_NE(0x45, 0x85, 0xC9),                  //   test r9d,r9d                       ;
          THEN(                                     //   if (r9d == 0) {                    ;   if (r9d == 0) {
            0x48, 0x31, 0xC0                        //     xor rax,rax                      ;     rax = 0 (set our return value to null)
          ),                                        //   }                                  ;   }
          IF_GT(0x45, 0x85, 0xC9),                  //   test r9d,r9d                       ;
          THEN(                                     //   if (r9d > 0) {                     ;   if (r9d > 0) {
            0x49, 0xBB, LONG_TO_BYTES(pickTop),     //     mov r11, RoomDeck::PickTop()     ;     r11 = &RoomDeck::PickTop (load an absolute pointer to the function)
            0x41, 0xFF, 0xD3                        //     call r11                         ;     rax = RoomDeck::PickTop() (call the function to get our return value)
          )                                         //   }                                  ;   }
        ),                                          // }                                    ; }
        0x41, 0x5F,                                 // pop r15                              ; Pop all our used registers to clean up.
        0x41, 0x5E,                                 // pop r14                              ;
        0x41, 0x5D,                                 // pop r13                              ;
        0x41, 0x5C,                                 // pop r12                              ;
        0x41, 0x5B,                                 // pop r11                              ;
        0x41, 0x5A,                                 // pop r10                              ;
        0x41, 0x59,                                 // pop r9                               ;
        0x41, 0x58,                                 // pop r8                               ;
        0x5F,                                       // pop rdi                              ;
        0x5E,                                       // pop rsi                              ;
        0x5A,                                       // pop rdx                              ;
        0x59,                                       // pop rcx                              ;
    }, /*writeOriginalCode*/ false);
}

std::vector<wchar_t> Trainer::ReadBuffer() {
    if (_buffer == 0) return {};
    int64_t newBufferPosition = _memory->ReadData<int64_t>({_buffer}, {0x8})[0];
    if (newBufferPosition == _bufferPosition) return {};
    std::vector<wchar_t> rawChars = _memory->ReadData<wchar_t>({_buffer + _bufferPosition}, (newBufferPosition - _bufferPosition) / 2);
    _bufferPosition = newBufferPosition;

    return rawChars;
}

std::vector<std::vector<std::wstring>> Trainer::GetDecks() {
    std::vector<wchar_t> buffer = ReadBuffer();
    size_t i = 0;

    std::vector<std::vector<std::wstring>> decks;
    while (i < buffer.size()) {
        std::vector<std::wstring> deck;
        while (buffer[i] != L'\0') {
            std::wstring card;
            while (buffer[i] != L'\0') {
                card.push_back(buffer[i]);
                i++;
            }
            deck.push_back(card);
            i++;
        }
        decks.push_back(deck);
        i++;
    }

    return decks;
}

void Trainer::ForceRoomDraft(const std::wstring& name, int slot) {
    if (slot < 1 || slot > 3) return;
    if (name.size() == 0) {
        // Clear the override if we write an empty string
        _memory->WriteData<int64_t>({_buffer + 0x8 * slot}, {0});
        return;
    }
    // Annoyingly, we have to allocate a C# String here, which has some extra nonsense.
    // TODO: I am not actually allocating the vtable pointer. Hopefully C# doesn't mind. If not, I can copy it from a known string offset (?)
    std::vector<byte> stringBytes = {
        LONG_TO_BYTES(0), // vtable
        LONG_TO_BYTES(0), // unused
        INT_TO_BYTES(name.size()), // size of string
    };
    for (wchar_t ch : name) {
        stringBytes.push_back((ch & 0x00FF) >> 0x00);
        stringBytes.push_back((ch & 0xFF00) >> 0x08);
    }

    // Append a null terminator
    stringBytes.push_back(0x00);
    stringBytes.push_back(0x00);

    __int64 addr = _memory->AllocateArray(stringBytes.size());
    _memory->WriteData<byte>({addr}, stringBytes);
    _memory->WriteData<int64_t>({_buffer + 0x8 * slot}, {addr});
}
