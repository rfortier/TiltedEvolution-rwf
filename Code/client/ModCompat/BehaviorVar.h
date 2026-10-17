#pragma once

#include <atomic>

#include <Structs/AnimationGraphDescriptorManager.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_Master_Behavior.h>
#include <Structs/Skyrim/AnimationGraphDescriptor_BHR_Master.h>

struct BehaviorVar
{
    struct Replacer
    {
        uint64_t origHash;
        uint64_t newHash;
        TiltedPhoques::String signatureVar;
        TiltedPhoques::String creatureName;
        TiltedPhoques::Vector<TiltedPhoques::String> syncBooleanVar;
        TiltedPhoques::Vector<TiltedPhoques::String> syncFloatVar;
        TiltedPhoques::Vector<TiltedPhoques::String> syncIntegerVar;
        bool operator==(const uint64_t& acRHS) const { return origHash == acRHS; }
    };

    static BehaviorVar* Get();
    const AnimationGraphDescriptor* Patch(BSAnimationGraphManager* apManager, Actor* apActor);
    bool FailListed(const uint64_t acHash);
    void FailList(const uint64_t acHash);
    // Both hashes are seeded with the unmodified keys and can be overwritten by
    // InitAsync's worker, so a reader that arrives early must see the seeded value
    // rather than a torn one. Before publication the seeded defaults are also the
    // correct answer: no modded behavior has been recognised yet.
    static bool IsDragon(const uint64_t acHash)
    {
        return !Get()->IsReady() ? acHash == AnimationGraphDescriptor_BHR_Master::m_key
                                 : acHash == Get()->m_dragonGraphDescriptorHash;
    }

    static uint64_t GetHumanoidHash()
    {
        return !Get()->IsReady() ? AnimationGraphDescriptor_Master_Behavior::m_key : Get()->m_humanoidGraphDescriptorHash;
    }

    void Init();

    /**
     * @brief Runs Init() on a worker thread.
     *
     * Init walks Data/SkyrimTogetherRebornBehaviors and opens several files per
     * subdirectory. Under Mod Organizer every one of those calls crosses the USVFS
     * hook, and World::Create calls this on the thread that is still booting the
     * game, so the cost lands directly on startup.
     *
     * Readers must not observe the pool while it is being filled: Patch() runs from
     * a game-thread hook and iterates behaviorPool, and a vector that reallocates
     * underneath it is a crash. IsReady() is the publication point - it is false
     * until the pool is complete, and Patch() falls back to the unmodified
     * descriptor while it is false.
     */
    void InitAsync();
    [[nodiscard]] bool IsReady() const noexcept { return m_ready.load(std::memory_order_acquire); }

    void Debug();

private:
    static BehaviorVar* single;
    uint64_t invocations = 0;

    // These start out with the unmodded hashes and save the modded
    // hash if behavior is modified. Humanoid is used to return from 
    // beast mode, and dragon is used for IsDragon() test.
    uint64_t m_humanoidGraphDescriptorHash {AnimationGraphDescriptor_Master_Behavior::m_key};
    uint64_t m_dragonGraphDescriptorHash   {AnimationGraphDescriptor_BHR_Master::m_key};

    void SeedAnimationVariables(
        const uint64_t acHash,
        const AnimationGraphDescriptor* acpDescriptor,
        TiltedPhoques::Map<TiltedPhoques::String, uint32_t>& acReverseMap,
        TiltedPhoques::Set<uint32_t>& aBoolVars,
        TiltedPhoques::Set<uint32_t>& aFloatVars,
        TiltedPhoques::Set<uint32_t>& aIntVars);

    const TiltedPhoques::Vector<TiltedPhoques::String> TokenizeBehaviorSig(const TiltedPhoques::String& acSignature) const;
    const AnimationGraphDescriptor* ConstructModdedDescriptor(
        const uint64_t acNewHash,
        const Replacer& acReplacer,
        TiltedPhoques::Map<TiltedPhoques::String, uint32_t>& acReverseMap);

    TiltedPhoques::Vector<std::filesystem::path> LoadDirs(const std::filesystem::path& acPath);
    Replacer* LoadReplacerFromDir(const std::filesystem::path& acDir);
    TiltedPhoques::Vector<uint64_t> SignatureMatches(const uint64_t acHash, const TiltedPhoques::String& acSignature) const;

    TiltedPhoques::Vector<Replacer> behaviorPool; // Pool for loaded behaviors
    TiltedPhoques::Map<uint64_t, std::chrono::steady_clock::time_point> failedBehaviors;

    // Release/acquire pair between InitAsync's worker and every game-thread reader.
    // Also guards m_humanoidGraphDescriptorHash and m_dragonGraphDescriptorHash,
    // which Init may overwrite.
    std::atomic<bool> m_ready{false};
};

extern const AnimationGraphDescriptor* BehaviorVarPatch(BSAnimationGraphManager* apManager, Actor* apActor);
