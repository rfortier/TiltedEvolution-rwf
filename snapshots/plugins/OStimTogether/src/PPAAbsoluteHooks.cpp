#include "PCH.h"
#include "PPAIntegration.h"

#include <array>
#include <cstring>

namespace OStimTogether
{
    namespace
    {
        constexpr std::uintptr_t kGetAnimationTaggerRVA = 0x0004B9B0;
        constexpr std::uintptr_t kRefreshRuntimeRVA = 0x00034F80;
        constexpr std::uintptr_t kSetInteractionRVA = 0x00057D80;
        constexpr std::uintptr_t kSetTargetRVA = 0x00058070;
        constexpr std::size_t kAbsoluteJumpBytes = 14;

        // Exact whole-instruction prefixes from the verified
        // AccuratePenetration.dll build. Both are long enough to replace the
        // entry with a 14-byte RIP-indirect absolute jump without splitting an
        // instruction.
        constexpr std::array<std::uint8_t, 16> kSetInteractionStolenBytes{
            0x40, 0x55, 0x53, 0x56, 0x57,
            0x41, 0x54,
            0x41, 0x56,
            0x41, 0x57,
            0x48, 0x8D, 0x6C, 0x24, 0xE1
        };

        constexpr std::array<std::uint8_t, 18> kSetTargetStolenBytes{
            0x40, 0x55, 0x53, 0x56, 0x57,
            0x41, 0x54,
            0x41, 0x55,
            0x41, 0x56,
            0x41, 0x57,
            0x48, 0x8D, 0x6C, 0x24, 0xE9
        };

        // PPA's native TargetMenu callback invokes this no-argument routine
        // immediately after setInteraction(). It walks the active penetration
        // instances and rematerializes the new targeting data into the live
        // solver. Merely calling the setter updates AnimationTagger storage but
        // does not update an already-running scene.
        constexpr std::array<std::uint8_t, 16> kRefreshRuntimeSignature{
            0x48, 0x83, 0xEC, 0x58,
            0x48, 0x83, 0x3D, 0x24, 0xED, 0x1E, 0x00, 0x00,
            0x0F, 0x84, 0x7C, 0x00
        };

        void* g_absoluteStubPage{ nullptr };

        void WriteAbsoluteJump(
            std::uint8_t* destination,
            std::uintptr_t target)
        {
            // jmp qword ptr [rip+0]
            // dq target
            // No register is clobbered and there is no +/-2 GiB restriction.
            constexpr std::array<std::uint8_t, 6> kPrefix{
                0xFF, 0x25, 0x00, 0x00, 0x00, 0x00
            };

            std::memcpy(
                destination,
                kPrefix.data(),
                kPrefix.size());
            std::memcpy(
                destination + kPrefix.size(),
                std::addressof(target),
                sizeof(target));
        }

        bool WriteExecutableBytes(
            std::uintptr_t destination,
            const void* source,
            std::size_t size)
        {
            if (!destination || !source || size == 0) {
                return false;
            }

            DWORD oldProtect = 0;
            if (!VirtualProtect(
                    reinterpret_cast<void*>(destination),
                    size,
                    PAGE_EXECUTE_READWRITE,
                    &oldProtect)) {
                SKSE::log::error(
                    "OSTNET PPA ABS64 VirtualProtect failed address=0x{:X} size={} error={}",
                    destination,
                    size,
                    GetLastError());
                return false;
            }

            std::memcpy(
                reinterpret_cast<void*>(destination),
                source,
                size);

            const bool flushed =
                FlushInstructionCache(
                    GetCurrentProcess(),
                    reinterpret_cast<void*>(destination),
                    size) != 0;

            DWORD ignored = 0;
            const bool restored =
                VirtualProtect(
                    reinterpret_cast<void*>(destination),
                    size,
                    oldProtect,
                    &ignored) != 0;

            if (!flushed || !restored) {
                SKSE::log::warn(
                    "OSTNET PPA ABS64 patch post-write warning address=0x{:X} flushed={} protectionRestored={} error={}",
                    destination,
                    flushed ? 1 : 0,
                    restored ? 1 : 0,
                    GetLastError());
            }

            return flushed;
        }

        template <class Fn, class Hook, std::size_t N>
        Fn InstallAbsoluteEntryDetour(
            std::uint8_t*& stubCursor,
            std::uintptr_t source,
            Hook hook,
            const std::array<std::uint8_t, N>& stolenBytes)
        {
            static_assert(N >= kAbsoluteJumpBytes);

            if (!stubCursor || !source || !hook) {
                return nullptr;
            }

            auto* originalStub = stubCursor;
            stubCursor += 64;

            // Callable original: replay all whole instructions displaced from
            // PPA, then jump absolutely to the first untouched instruction.
            std::memcpy(
                originalStub,
                stolenBytes.data(),
                stolenBytes.size());
            WriteAbsoluteJump(
                originalStub + stolenBytes.size(),
                source + stolenBytes.size());

            // Function entry: direct absolute jump to OStimTogether.dll. Fill
            // any bytes beyond the 14-byte jump with NOPs so the entire stolen
            // instruction window is replaced deterministically.
            std::array<std::uint8_t, N> patch{};
            patch.fill(0x90);
            WriteAbsoluteJump(
                patch.data(),
                reinterpret_cast<std::uintptr_t>(hook));

            if (!WriteExecutableBytes(
                    source,
                    patch.data(),
                    patch.size())) {
                return nullptr;
            }

            return reinterpret_cast<Fn>(originalStub);
        }
    }

    void PPAIntegration::SetTargetHookNetworkSafe(
        void* tagger,
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        std::uint8_t target)
    {
        // Preserve PPA's local behavior exactly. Its own caller performs the
        // native active-runtime refresh after completing the requested edits.
        if (_setTargetRawOriginal) {
            _setTargetRawOriginal(
                tagger,
                animation,
                stage,
                performerPosition,
                target);
        }

        GetSingleton().PublishSetTarget(
            animation,
            stage,
            performerPosition,
            target);
    }

    void PPAIntegration::SetInteractionHookNetworkSafe(
        void* tagger,
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        const StageInteractionRaw* interaction)
    {
        StageInteractionRaw networkSnapshot{};
        const bool valid = interaction != nullptr;
        if (valid) {
            networkSnapshot = *interaction;
        }

        // The real local setter receives PPA's bytes unchanged. In particular,
        // 0xFF remains PPA's native "no explicit target actor" sentinel.
        if (_setInteractionRawOriginal) {
            _setInteractionRawOriginal(
                tagger,
                animation,
                stage,
                performerPosition,
                interaction);
        }

        if (!valid) {
            return;
        }

        // The network protocol historically accepts participant positions 0..5.
        // PPA also uses 0xFF when hasExplicitTargetActor==0. Normalize only the
        // transmitted copy; this prevents the receiver from silently rejecting
        // ordinary Auto/Anus/Vagina menu operations while preserving local PPA.
        if (networkSnapshot.hasExplicitTargetActor == 0 &&
            networkSnapshot.targetActorPosition > 5) {
            SKSE::log::info(
                "OSTNET PPA SENTINEL NORMALIZE animation=\"{}\" stage={} performer={} actor={}=>0 explicit=0",
                animation,
                stage,
                performerPosition,
                networkSnapshot.targetActorPosition);
            networkSnapshot.targetActorPosition = 0;
        }

        GetSingleton().PublishSetInteraction(
            animation,
            stage,
            performerPosition,
            networkSnapshot);
    }

    void PPAIntegration::SetTargetRemoteOriginal(
        void* tagger,
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        std::uint8_t target)
    {
        if (!_setTargetRawOriginal || !_refreshRuntime) {
            return;
        }

        _setTargetRawOriginal(
            tagger,
            animation,
            stage,
            performerPosition,
            target);

        _refreshRuntime();

        SKSE::log::info(
            "OSTNET PPA REMOTE REFRESH kind=target animation=\"{}\" stage={} performer={} target={} refreshRva=0x{:X}",
            animation,
            stage,
            performerPosition,
            target,
            kRefreshRuntimeRVA);
    }

    void PPAIntegration::SetInteractionRemoteOriginal(
        void* tagger,
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        const StageInteractionRaw* interaction)
    {
        if (!_setInteractionRawOriginal || !_refreshRuntime) {
            return;
        }

        _setInteractionRawOriginal(
            tagger,
            animation,
            stage,
            performerPosition,
            interaction);

        _refreshRuntime();

        SKSE::log::info(
            "OSTNET PPA REMOTE REFRESH kind=interaction animation=\"{}\" stage={} performer={} target={} targetActor={} explicit={} refreshRva=0x{:X}",
            animation,
            stage,
            performerPosition,
            interaction ? interaction->target : 0,
            interaction ? interaction->targetActorPosition : 0,
            interaction && interaction->hasExplicitTargetActor ? 1 : 0,
            kRefreshRuntimeRVA);
    }

    bool PPAIntegration::PrepareAbsoluteHooks()
    {
        if (_hooksInstalled) {
            return true;
        }

        // Do not modify PPA merely because it is installed. The FOMOD marker
        // remains the user's explicit opt-in for this exact-build integration.
        // Return true here so main.cpp does not emit a scary hook-failure warning
        // when the optional component was intentionally left unchecked.
        if (!IsOptionalIntegrationInstalled()) {
            SKSE::log::info(
                "OSTNET PPA ABS64 skipped: optional FOMOD component not installed");
            return true;
        }

        const auto module = GetModuleHandleW(
            AccuratePenetration::API::kPluginDLL);
        if (!module) {
            SKSE::log::warn(
                "OSTNET PPA ABS64 unavailable: AccuratePenetration.dll is not loaded");
            return false;
        }

        // Reuse the exact timestamp/image/signature validation already owned by
        // PPAIntegration. Unsupported future PPA builds remain fail-closed.
        if (!ValidateExactPPABuild(module)) {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        _getAnimationTagger = reinterpret_cast<GetAnimationTaggerFn>(
            base + kGetAnimationTaggerRVA);

        const auto refreshAddress = base + kRefreshRuntimeRVA;
        if (std::memcmp(
                reinterpret_cast<const void*>(refreshAddress),
                kRefreshRuntimeSignature.data(),
                kRefreshRuntimeSignature.size()) != 0) {
            SKSE::log::error(
                "OSTNET PPA ABS64 refresh signature mismatch rva=0x{:X} action=disable-no-hook",
                kRefreshRuntimeRVA);
            return false;
        }

        _refreshRuntime = reinterpret_cast<RefreshRuntimeFn>(
            refreshAddress);

        if (!g_absoluteStubPage) {
            g_absoluteStubPage = VirtualAlloc(
                nullptr,
                4096,
                MEM_COMMIT | MEM_RESERVE,
                PAGE_EXECUTE_READWRITE);
        }

        if (!g_absoluteStubPage) {
            SKSE::log::error(
                "OSTNET PPA ABS64 stub allocation failed error={} action=disable-no-hook",
                GetLastError());
            return false;
        }

        auto* cursor = static_cast<std::uint8_t*>(g_absoluteStubPage);

        const auto interactionRawOriginal =
            InstallAbsoluteEntryDetour<SetInteractionFn>(
                cursor,
                base + kSetInteractionRVA,
                &PPAIntegration::SetInteractionHookNetworkSafe,
                kSetInteractionStolenBytes);
        if (!interactionRawOriginal) {
            SKSE::log::error(
                "OSTNET PPA ABS64 setInteraction install failed action=disable-integration");
            return false;
        }
        _setInteractionRawOriginal = interactionRawOriginal;

        const auto targetRawOriginal =
            InstallAbsoluteEntryDetour<SetTargetFn>(
                cursor,
                base + kSetTargetRVA,
                &PPAIntegration::SetTargetHookNetworkSafe,
                kSetTargetStolenBytes);
        if (!targetRawOriginal) {
            SKSE::log::error(
                "OSTNET PPA ABS64 setTarget install failed action=disable-integration");
            return false;
        }
        _setTargetRawOriginal = targetRawOriginal;

        // Existing ApplyRemoteSet*() code calls these two pointers. Route those
        // calls through wrappers that reproduce the native post-menu runtime
        // refresh, while local hook forwarding continues to use the raw stubs.
        _setInteractionOriginal =
            &PPAIntegration::SetInteractionRemoteOriginal;
        _setTargetOriginal =
            &PPAIntegration::SetTargetRemoteOriginal;

        _hooksInstalled = true;

        SKSE::log::info(
            "OSTNET PPA ABS64 READY getterRva=0x{:X} refreshRva=0x{:X} setInteractionRva=0x{:X} setTargetRva=0x{:X} stolenInteraction={} stolenTarget={} detour=direct-rip-indirect-abs64 rel32=0 sentinelNormalize=1 remoteRefresh=1 exactBuild=1 targetWrite=1",
            kGetAnimationTaggerRVA,
            kRefreshRuntimeRVA,
            kSetInteractionRVA,
            kSetTargetRVA,
            kSetInteractionStolenBytes.size(),
            kSetTargetStolenBytes.size());

        return true;
    }
}
