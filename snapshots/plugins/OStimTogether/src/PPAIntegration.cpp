#include "PCH.h"
#include "PPAIntegration.h"

#include "OStimAPI/InterfaceExchangeMessage.h"
#include "STRPMTransport.h"

#include <cstring>

namespace OStimTogether
{
    namespace
    {
        constexpr wchar_t kSTRPMModuleName[] = L"STRPluginMessagingAPI.dll";
        constexpr char kPPAChannel[] = "ostimtogether.ppa";
        constexpr char kMarkerPath[] =
            "Data/SKSE/Plugins/OStimTogether_PPA.ini";
        constexpr char kPluginName[] = "OStimTogether";

        // Keep PPA's rel32 branch islands close to AccuratePenetration.dll,
        // rather than using CommonLib's default pool near SkyrimSE.exe.
        SKSE::Trampoline g_ppaTrampoline{ "OStimTogether PPA" };

        // Exact AccuratePenetration.dll supplied by the user for v0.33.x.
        // SHA-256:
        // 0BD68B935E54211EEA71BE064AEB628B2AA268A7F35229FF554ED65A88EED087
        constexpr std::uint32_t kSupportedPETimestamp = 0x6A633AE8;
        constexpr std::uint32_t kSupportedImageSize = 0x00239000;
        constexpr std::uintptr_t kGetAnimationTaggerRVA = 0x0004B9B0;
        constexpr std::uintptr_t kSetInteractionRVA = 0x00057D80;
        constexpr std::uintptr_t kSetTargetRVA = 0x00058070;
        constexpr std::size_t kEntryStolenBytes = 5;
        constexpr std::size_t kAbsoluteJumpBytes = 14;

        // Both setters begin with the same five whole instructions:
        //   push rbp / push rbx / push rsi / push rdi
        // The fifth byte ends exactly on an instruction boundary and contains
        // no RIP-relative operand, so it is safe to copy into our trampoline.
        constexpr std::array<std::uint8_t, kEntryStolenBytes>
            kSetterStolenBytes{ 0x40, 0x55, 0x53, 0x56, 0x57 };

        constexpr std::array<std::uint8_t, 18> kGetAnimationTaggerSignature{
            0x48, 0x83, 0xEC, 0x28, 0x8B, 0x0D, 0x8E, 0xAA, 0x1D,
            0x00, 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00
        };
        constexpr std::array<std::uint8_t, 18> kSetInteractionSignature{
            0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56,
            0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xE1, 0x48, 0x81
        };
        constexpr std::array<std::uint8_t, 18> kSetTargetSignature{
            0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55,
            0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xE9
        };

        std::optional<std::string> Field(
            std::string_view payload,
            std::string_view key)
        {
            const auto needle = fmt::format("{}=", key);
            std::size_t from = 0;
            while (from < payload.size()) {
                const auto pos = payload.find(needle, from);
                if (pos == std::string_view::npos) {
                    return std::nullopt;
                }
                if (pos == 0 || payload[pos - 1] == '|') {
                    const auto begin = pos + needle.size();
                    const auto end = payload.find('|', begin);
                    return std::string(payload.substr(
                        begin,
                        end == std::string_view::npos ?
                            payload.size() - begin :
                            end - begin));
                }
                from = pos + needle.size();
            }
            return std::nullopt;
        }

        std::optional<std::uint32_t> ParseUInt(
            std::string_view payload,
            std::string_view key)
        {
            const auto value = Field(payload, key);
            if (!value || value->empty()) {
                return std::nullopt;
            }
            try {
                return static_cast<std::uint32_t>(std::stoul(*value));
            } catch (...) {
                return std::nullopt;
            }
        }

        std::optional<std::int32_t> ParseInt(
            std::string_view payload,
            std::string_view key)
        {
            const auto value = Field(payload, key);
            if (!value || value->empty()) {
                return std::nullopt;
            }
            try {
                return static_cast<std::int32_t>(std::stol(*value));
            } catch (...) {
                return std::nullopt;
            }
        }

        template <std::size_t N>
        bool MatchSignature(
            std::uintptr_t address,
            const std::array<std::uint8_t, N>& signature)
        {
            return address != 0 &&
                   std::memcmp(
                       reinterpret_cast<const void*>(address),
                       signature.data(),
                       signature.size()) == 0;
        }

        bool IsRel32Reachable(
            std::uintptr_t source,
            std::uintptr_t target)
        {
            constexpr std::uintptr_t kMaxForward = 0x7FFFFFFF;
            constexpr std::uintptr_t kMaxBackward = 0x80000000;
            const auto nextInstruction = source + 5;

            if (target >= nextInstruction) {
                return target - nextInstruction <= kMaxForward;
            }
            return nextInstruction - target <= kMaxBackward;
        }

        void WriteAbsoluteJump(
            std::uint8_t* destination,
            std::uintptr_t target)
        {
            if (!destination) {
                return;
            }

            // jmp qword ptr [rip+0]
            // dq target
            // This 14-byte x64 jump has no ±2 GiB restriction and does not
            // clobber RAX or any other register.
            constexpr std::array<std::uint8_t, 6> kPrefix{
                0xFF, 0x25, 0x00, 0x00, 0x00, 0x00
            };
            std::memcpy(destination, kPrefix.data(), kPrefix.size());
            std::memcpy(
                destination + kPrefix.size(),
                std::addressof(target),
                sizeof(target));
        }

        template <class Fn, class Hook>
        Fn InstallEntryDetour(
            SKSE::Trampoline& trampoline,
            std::uintptr_t source,
            Hook hook)
        {
            auto* originalStub =
                static_cast<std::uint8_t*>(trampoline.allocate(32));
            auto* hookRelay =
                static_cast<std::uint8_t*>(trampoline.allocate(16));
            if (!originalStub || !hookRelay) {
                return nullptr;
            }

            // Callable original: replay the five verified whole instructions,
            // then use an absolute x64 jump back to PPA. No rel32 is involved.
            std::memcpy(
                originalStub,
                kSetterStolenBytes.data(),
                kSetterStolenBytes.size());
            WriteAbsoluteJump(
                originalStub + kEntryStolenBytes,
                source + kEntryStolenBytes);

            // PPA itself can only spare five bytes at this entry, so its entry
            // branches to a relay allocated close to AccuratePenetration.dll.
            // The relay then makes an unrestricted absolute jump into
            // OStimTogether.dll, which may be anywhere in the 64-bit address
            // space under ASLR.
            const auto hookAddress =
                reinterpret_cast<std::uintptr_t>(hook);
            const auto relayAddress =
                reinterpret_cast<std::uintptr_t>(hookRelay);
            WriteAbsoluteJump(hookRelay, hookAddress);

            // CommonLib's write_branch<5>() reports/fails fatally when the
            // displacement is outside signed rel32 range. Check it ourselves
            // first so an unexpected Windows module layout merely disables PPA
            // integration instead of closing Skyrim.
            if (!IsRel32Reachable(source, relayAddress)) {
                SKSE::log::error(
                    "OSTNET PPA INTERNAL relay out of rel32 range source=0x{:X} relay=0x{:X} hook=0x{:X} action=disable-no-hook",
                    source,
                    relayAddress,
                    hookAddress);
                return nullptr;
            }

            trampoline.write_branch<5>(source, relayAddress);
            return reinterpret_cast<Fn>(originalStub);
        }
    }

    PPAIntegration& PPAIntegration::GetSingleton()
    {
        static PPAIntegration singleton;
        return singleton;
    }

    PPAIntegration::~PPAIntegration()
    {
        Disconnect();
    }

    const char* PPAIntegration::TargetName(std::uint8_t target) noexcept
    {
        switch (static_cast<ActionTarget>(target)) {
        case ActionTarget::Auto: return "Auto";
        case ActionTarget::None: return "None";
        case ActionTarget::Vagina: return "Vagina";
        case ActionTarget::Anus: return "Anus";
        case ActionTarget::Mouth: return "Mouth";
        case ActionTarget::Hand: return "Hand";
        case ActionTarget::LeftHand: return "L Hand";
        case ActionTarget::RightHand: return "R Hand";
        default: return "Unknown";
        }
    }

    std::string PPAIntegration::HexEncode(std::string_view value)
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(value.size() * 2);
        for (const auto ch : value) {
            const auto u = static_cast<unsigned char>(ch);
            out.push_back(kHex[(u >> 4) & 0x0F]);
            out.push_back(kHex[u & 0x0F]);
        }
        return out;
    }

    std::optional<std::string> PPAIntegration::HexDecode(
        std::string_view value)
    {
        if ((value.size() % 2) != 0) {
            return std::nullopt;
        }
        const auto nibble = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
            if (ch >= 'A' && ch <= 'F') return 10 + ch - 'A';
            return -1;
        };
        std::string out;
        out.reserve(value.size() / 2);
        for (std::size_t i = 0; i < value.size(); i += 2) {
            const int hi = nibble(value[i]);
            const int lo = nibble(value[i + 1]);
            if (hi < 0 || lo < 0) {
                return std::nullopt;
            }
            out.push_back(static_cast<char>((hi << 4) | lo));
        }
        return out;
    }

    bool PPAIntegration::IsOptionalIntegrationInstalled() const
    {
        std::error_code ec;
        return std::filesystem::exists(kMarkerPath, ec) && !ec;
    }

    bool PPAIntegration::ConnectOStim()
    {
        auto* messaging = SKSE::GetMessagingInterface();
        const auto module = GetModuleHandleW(L"OStim.dll");
        if (!messaging || !module) {
            return false;
        }

        OStim::InterfaceExchangeMessage exchange{};
        if (!messaging->Dispatch(
                OStim::InterfaceExchangeMessage::MESSAGE_TYPE,
                &exchange,
                sizeof(exchange),
                nullptr) ||
            !exchange.interfaceMap) {
            return false;
        }

        auto* base = exchange.interfaceMap->queryInterface(
            OStim::ThreadInterface::NAME);
        _threads = base ?
            static_cast<OStim::ThreadInterface*>(base) :
            nullptr;

        const auto requestThread =
            reinterpret_cast<OStimModAPI::Thread::RequestAPI>(
                reinterpret_cast<void*>(GetProcAddress(
                    module,
                    "RequestPluginAPI_Thread")));

        auto* declaration = SKSE::PluginDeclaration::GetSingleton();
        const auto version = declaration ?
            declaration->GetVersion() :
            REL::Version{ 0, 33, 1, 0 };
        const auto pluginName = declaration ?
            std::string(declaration->GetName()) :
            std::string(kPluginName);

        _threadControl = requestThread ?
            requestThread(
                OStimModAPI::Thread::InterfaceVersion::V1,
                pluginName.c_str(),
                version) :
            nullptr;

        if (!_threads || !_threadControl) {
            SKSE::log::warn(
                "OSTNET PPA OStim bridge unavailable: thread APIs missing");
            return false;
        }

        SKSE::log::info(
            "OSTNET PPA OSTIM READY threadsVersion={}",
            _threads->getVersion());
        return true;
    }

    bool PPAIntegration::ValidateExactPPABuild(HMODULE module) const
    {
        if (!module) {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
            return false;
        }

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
            base + static_cast<std::uintptr_t>(dos->e_lfanew));
        if (nt->Signature != IMAGE_NT_SIGNATURE ||
            nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
            return false;
        }

        if (nt->FileHeader.TimeDateStamp != kSupportedPETimestamp ||
            nt->OptionalHeader.SizeOfImage != kSupportedImageSize) {
            SKSE::log::warn(
                "OSTNET PPA INTERNAL unsupported build timestamp=0x{:08X} imageSize=0x{:X} expectedTimestamp=0x{:08X} expectedImageSize=0x{:X}",
                nt->FileHeader.TimeDateStamp,
                nt->OptionalHeader.SizeOfImage,
                kSupportedPETimestamp,
                kSupportedImageSize);
            return false;
        }

        const bool getterOK = MatchSignature(
            base + kGetAnimationTaggerRVA,
            kGetAnimationTaggerSignature);
        const bool interactionOK = MatchSignature(
            base + kSetInteractionRVA,
            kSetInteractionSignature);
        const bool targetOK = MatchSignature(
            base + kSetTargetRVA,
            kSetTargetSignature);

        if (!getterOK || !interactionOK || !targetOK) {
            SKSE::log::warn(
                "OSTNET PPA INTERNAL unsupported signatures getter={} setInteraction={} setTarget={} action=disable-no-hook",
                getterOK ? 1 : 0,
                interactionOK ? 1 : 0,
                targetOK ? 1 : 0);
            return false;
        }
        return true;
    }

    bool PPAIntegration::InstallPPAHooks(HMODULE module)
    {
        if (_hooksInstalled) {
            return true;
        }
        if (!ValidateExactPPABuild(module)) {
            return false;
        }

        const auto base = reinterpret_cast<std::uintptr_t>(module);
        _getAnimationTagger = reinterpret_cast<GetAnimationTaggerFn>(
            base + kGetAnimationTaggerRVA);

        // Allocate an independent executable trampoline close to the PPA
        // module. Only the five-byte PPA-entry -> relay jump needs rel32 range;
        // both relay destinations use absolute x64 jumps.
        g_ppaTrampoline.create(256, module);

        _setInteractionOriginal = InstallEntryDetour<SetInteractionFn>(
            g_ppaTrampoline,
            base + kSetInteractionRVA,
            &PPAIntegration::SetInteractionHook);
        _setTargetOriginal = InstallEntryDetour<SetTargetFn>(
            g_ppaTrampoline,
            base + kSetTargetRVA,
            &PPAIntegration::SetTargetHook);

        if (!_getAnimationTagger ||
            !_setInteractionOriginal ||
            !_setTargetOriginal) {
            SKSE::log::error(
                "OSTNET PPA INTERNAL hook installation failed getter={} interactionOriginal={} targetOriginal={} action=disable-integration",
                _getAnimationTagger ? 1 : 0,
                _setInteractionOriginal ? 1 : 0,
                _setTargetOriginal ? 1 : 0);
            return false;
        }

        _hooksInstalled = true;
        SKSE::log::info(
            "OSTNET PPA INTERNAL READY timestamp=0x{:08X} imageSize=0x{:X} getterRva=0x{:X} setInteractionRva=0x{:X} setTargetRva=0x{:X} stolenBytes={} trampoline=near-ppa-relay-abs64 exactBuild=1 targetWrite=1",
            kSupportedPETimestamp,
            kSupportedImageSize,
            kGetAnimationTaggerRVA,
            kSetInteractionRVA,
            kSetTargetRVA,
            kEntryStolenBytes);
        return true;
    }

    bool PPAIntegration::ConnectPPA()
    {
        const auto module = GetModuleHandleW(
            AccuratePenetration::API::kPluginDLL);
        if (!module) {
            SKSE::log::warn(
                "OSTNET PPA unavailable: AccuratePenetration.dll is not loaded");
            return false;
        }

        using GetAPIFn =
            const AccuratePenetration::API::InterfaceV1*(__cdecl*)();
        const auto getAPI = reinterpret_cast<GetAPIFn>(GetProcAddress(
            module,
            AccuratePenetration::API::kGetAPIFunctionNameV1));
        const auto* api = getAPI ? getAPI() : nullptr;

        if (!api ||
            api->version != AccuratePenetration::API::kVersion ||
            api->size < sizeof(AccuratePenetration::API::InterfaceV1)) {
            SKSE::log::warn(
                "OSTNET PPA public API unavailable/incompatible expectedVersion={}",
                AccuratePenetration::API::kVersion);
            return false;
        }

        if (!InstallPPAHooks(module)) {
            return false;
        }

        _ppaModule = module;
        _ppaAPI = api;
        return true;
    }

    bool PPAIntegration::ConnectTransport()
    {
        const auto module = GetModuleHandleW(kSTRPMModuleName);
        if (!module) {
            return false;
        }

        const auto query = reinterpret_cast<STRPMApi::QueryInterfaceFn>(
            GetProcAddress(module, STRPMApi::kQueryInterfaceExportName));
        const STRPMApi::Interface* api = nullptr;
        const auto queryResult = query ?
            query(STRPMApi::kInterfaceVersion, &api) :
            STRPMApi::Result::kNotAvailable;
        if (queryResult != STRPMApi::Result::kOk ||
            !api || !api->registerChannel ||
            !api->unregisterChannel || !api->send) {
            return false;
        }

        STRPMApi::ListenerHandle listener{};
        const auto result = api->registerChannel(
            kPPAChannel,
            &PPAIntegration::OnTransportMessage,
            this,
            &listener);
        if (result != STRPMApi::Result::kOk) {
            return false;
        }

        _transportAPI = api;
        _transportListener = listener;
        SKSE::log::info(
            "OSTNET PPA TRANSPORT READY channel={} routing=active-ostim-participants-only",
            kPPAChannel);
        return true;
    }

    bool PPAIntegration::Initialize()
    {
        if (_enabled.load()) {
            return true;
        }
        if (!IsOptionalIntegrationInstalled()) {
            SKSE::log::info(
                "OSTNET PPA integration disabled: optional FOMOD component not installed");
            return false;
        }
        if (!ConnectOStim() || !ConnectPPA() || !ConnectTransport()) {
            Disconnect();
            return false;
        }

        _enabled.store(true);
        SKSE::log::info(
            "OSTNET PPA integration READY authority=real-local-player targetSiteSync=1 explicitTargetActorSync=1 remoteApply=internal-setter exactBuildFailClosed=1");
        return true;
    }

    void PPAIntegration::Disconnect()
    {
        _enabled.store(false);
        if (_transportAPI &&
            _transportListener.value != 0 &&
            _transportAPI->unregisterChannel) {
            _transportAPI->unregisterChannel(_transportListener);
        }
        _transportListener = {};
        _transportAPI = nullptr;
        _ppaAPI = nullptr;
        _ppaModule = nullptr;
        _threads = nullptr;
        _threadControl = nullptr;
        // Entry detours intentionally remain installed until process exit.
        // With _enabled=false they simply call the preserved PPA original.
    }

    void PPAIntegration::Reset()
    {
        // No per-save persistent state. The hooks are process-lifetime hooks.
    }

    bool PPAIntegration::IsLocalPlayerPerformer(
        std::uint8_t performerPosition) const
    {
        if (!_enabled.load() || !_threads || !_threadControl ||
            performerPosition == 0) {
            return false;
        }
        const auto tid = _threadControl->GetPlayerThreadID();
        if (!_threadControl->IsThreadValid(tid)) {
            return false;
        }
        auto* thread = _threads->getThread(static_cast<std::int32_t>(tid));
        if (!thread || performerPosition > thread->getActorCount()) {
            return false;
        }
        auto* ta = thread->getActor(performerPosition - 1);
        auto* actor = ta ?
            static_cast<RE::Actor*>(ta->getGameActor()) :
            nullptr;
        return actor && actor == RE::PlayerCharacter::GetSingleton();
    }

    std::vector<STRPMApi::ConnectionID>
        PPAIntegration::SnapshotRemoteParticipants() const
    {
        std::vector<STRPMApi::ConnectionID> result;
        if (!_threads || !_threadControl) {
            return result;
        }
        const auto tid = _threadControl->GetPlayerThreadID();
        if (!_threadControl->IsThreadValid(tid)) {
            return result;
        }
        auto* thread = _threads->getThread(static_cast<std::int32_t>(tid));
        if (!thread) {
            return result;
        }

        std::unordered_set<STRPMApi::ConnectionID> unique;
        for (std::uint32_t i = 0; i < thread->getActorCount(); ++i) {
            auto* ta = thread->getActor(i);
            auto* actor = ta ? static_cast<RE::Actor*>(ta->getGameActor()) : nullptr;
            if (!actor || actor->IsPlayerRef()) {
                continue;
            }
            const auto connection = STRPMTransport::GetSingleton()
                .ResolveConnection(actor->GetFormID());
            if (connection && *connection != 0 &&
                unique.insert(*connection).second) {
                result.push_back(*connection);
            }
        }
        return result;
    }

    bool PPAIntegration::ValidateRemoteSender(
        STRPMApi::ConnectionID senderConnectionID,
        std::uint8_t performerPosition) const
    {
        if (!_threads || !_threadControl ||
            senderConnectionID == 0 || performerPosition == 0) {
            return false;
        }
        const auto proxyFormID = STRPMTransport::GetSingleton()
            .ResolveProxy(senderConnectionID);
        if (!proxyFormID) {
            return false;
        }
        const auto tid = _threadControl->GetPlayerThreadID();
        if (!_threadControl->IsThreadValid(tid)) {
            return false;
        }
        auto* thread = _threads->getThread(static_cast<std::int32_t>(tid));
        if (!thread || performerPosition > thread->getActorCount()) {
            return false;
        }
        auto* ta = thread->getActor(performerPosition - 1);
        auto* actor = ta ? static_cast<RE::Actor*>(ta->getGameActor()) : nullptr;
        return actor && actor->GetFormID() == *proxyFormID;
    }

    bool PPAIntegration::ValidateTargetActorPosition(
        std::uint8_t targetActorPosition,
        bool explicitTarget) const
    {
        if (!explicitTarget) {
            return true;
        }
        if (!_threads || !_threadControl || targetActorPosition == 0) {
            return false;
        }
        const auto tid = _threadControl->GetPlayerThreadID();
        if (!_threadControl->IsThreadValid(tid)) {
            return false;
        }
        auto* thread = _threads->getThread(static_cast<std::int32_t>(tid));
        return thread && targetActorPosition <= thread->getActorCount();
    }

    bool PPAIntegration::SendToParticipants(std::string_view payload)
    {
        if (!_transportAPI || !_transportAPI->send || payload.empty()) {
            return false;
        }
        const auto participants = SnapshotRemoteParticipants();
        bool allOK = !participants.empty();
        for (const auto connectionID : participants) {
            STRPMApi::Target target{};
            target.kind = STRPMApi::TargetKind::kPlayer;
            target.connectionID = connectionID;
            const auto result = _transportAPI->send(
                kPPAChannel,
                target,
                payload.data(),
                payload.size(),
                STRPMApi::kMessageReliable |
                    STRPMApi::kMessageOrdered);
            if (result != STRPMApi::Result::kOk) {
                allOK = false;
            }
        }
        return allOK;
    }

    void PPAIntegration::SetTargetHook(
        void* tagger,
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        std::uint8_t target)
    {
        if (_setTargetOriginal) {
            _setTargetOriginal(
                tagger, animation, stage, performerPosition, target);
        }
        GetSingleton().PublishSetTarget(
            animation, stage, performerPosition, target);
    }

    void PPAIntegration::SetInteractionHook(
        void* tagger,
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        const StageInteractionRaw* interaction)
    {
        StageInteractionRaw snapshot{};
        const bool valid = interaction != nullptr;
        if (valid) {
            snapshot = *interaction;
        }
        if (_setInteractionOriginal) {
            _setInteractionOriginal(
                tagger, animation, stage, performerPosition, interaction);
        }
        if (valid) {
            GetSingleton().PublishSetInteraction(
                animation, stage, performerPosition, snapshot);
        }
    }

    void PPAIntegration::PublishSetTarget(
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        std::uint8_t target)
    {
        if (!IsLocalPlayerPerformer(performerPosition) ||
            animation.empty() || animation.size() > 512 ||
            stage < 1 || stage > 64 ||
            target > static_cast<std::uint8_t>(ActionTarget::RightHand)) {
            return;
        }
        const auto payload = fmt::format(
            "SETTARGET|v=1|animation={}|stage={}|performer={}|target={}",
            HexEncode(animation), stage, performerPosition, target);
        const bool sent = SendToParticipants(payload);
        SKSE::log::info(
            "OSTNET PPA SETTARGET TX animation=\"{}\" stage={} performer={} target={}({}) sent={}",
            animation, stage, performerPosition, target,
            TargetName(target), sent ? 1 : 0);
    }

    void PPAIntegration::PublishSetInteraction(
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        const StageInteractionRaw& interaction)
    {
        const bool explicitTarget =
            interaction.hasExplicitTargetActor != 0;
        if (!IsLocalPlayerPerformer(performerPosition) ||
            animation.empty() || animation.size() > 512 ||
            stage < 1 || stage > 64 ||
            interaction.target >
                static_cast<std::uint8_t>(ActionTarget::RightHand) ||
            !ValidateTargetActorPosition(
                interaction.targetActorPosition,
                explicitTarget)) {
            return;
        }
        const auto payload = fmt::format(
            "SETINTERACTION|v=1|animation={}|stage={}|performer={}|target={}|actor={}|explicit={}",
            HexEncode(animation), stage, performerPosition,
            interaction.target, interaction.targetActorPosition,
            explicitTarget ? 1 : 0);
        const bool sent = SendToParticipants(payload);
        SKSE::log::info(
            "OSTNET PPA SETINTERACTION TX animation=\"{}\" stage={} performer={} target={}({}) targetActor={} explicit={} sent={}",
            animation, stage, performerPosition,
            interaction.target, TargetName(interaction.target),
            interaction.targetActorPosition,
            explicitTarget ? 1 : 0, sent ? 1 : 0);
    }

    void __cdecl PPAIntegration::OnTransportMessage(
        const STRPMApi::Message* message,
        void* userData)
    {
        if (!message || !userData || !message->data ||
            message->size == 0 || message->sender.connectionID == 0) {
            return;
        }
        std::string payload(
            static_cast<const char*>(message->data),
            message->size);
        const auto senderConnection = message->sender.connectionID;
        if (auto* tasks = SKSE::GetTaskInterface()) {
            tasks->AddTask(
                [userData, senderConnection,
                 payload = std::move(payload)]() mutable {
                    STRPMApi::Message copy{};
                    copy.data = payload.data();
                    copy.size = payload.size();
                    copy.sender.connectionID = senderConnection;
                    static_cast<PPAIntegration*>(userData)
                        ->HandleTransportMessage(copy);
                });
        }
    }

    void PPAIntegration::HandleTransportMessage(
        const STRPMApi::Message& message)
    {
        if (!_enabled.load()) {
            return;
        }
        const std::string_view payload(
            static_cast<const char*>(message.data), message.size);

        const auto version = ParseUInt(payload, "v");
        const auto animationHex = Field(payload, "animation");
        const auto stage = ParseInt(payload, "stage");
        const auto performer = ParseUInt(payload, "performer");
        const auto target = ParseUInt(payload, "target");
        if (!version || *version != 1 || !animationHex ||
            !stage || !performer || !target ||
            *performer == 0 || *performer > 5 ||
            *stage < 1 || *stage > 64 ||
            *target > static_cast<std::uint32_t>(ActionTarget::RightHand)) {
            return;
        }

        const auto animation = HexDecode(*animationHex);
        if (!animation || animation->empty() || animation->size() > 512) {
            return;
        }
        const auto performerPosition =
            static_cast<std::uint8_t>(*performer);
        if (!ValidateRemoteSender(
                message.sender.connectionID,
                performerPosition)) {
            SKSE::log::warn(
                "OSTNET PPA RX rejected connection={} performer={} reason=sender-not-performer-in-current-thread",
                message.sender.connectionID,
                performerPosition);
            return;
        }

        if (payload.starts_with("SETTARGET|")) {
            const bool applied = ApplyRemoteSetTarget(
                *animation, *stage, performerPosition,
                static_cast<std::uint8_t>(*target));
            SKSE::log::info(
                "OSTNET PPA SETTARGET RX connection={} animation=\"{}\" stage={} performer={} target={}({}) applied={}",
                message.sender.connectionID, *animation, *stage,
                performerPosition, *target,
                TargetName(static_cast<std::uint8_t>(*target)),
                applied ? 1 : 0);
            return;
        }

        if (payload.starts_with("SETINTERACTION|")) {
            const auto actor = ParseUInt(payload, "actor");
            const auto explicitValue = ParseUInt(payload, "explicit");
            if (!actor || !explicitValue || *actor > 5 ||
                *explicitValue > 1) {
                return;
            }
            StageInteractionRaw interaction{};
            interaction.target = static_cast<std::uint8_t>(*target);
            interaction.targetActorPosition =
                static_cast<std::uint8_t>(*actor);
            interaction.hasExplicitTargetActor =
                static_cast<std::uint8_t>(*explicitValue);
            if (!ValidateTargetActorPosition(
                    interaction.targetActorPosition,
                    interaction.hasExplicitTargetActor != 0)) {
                return;
            }
            const bool applied = ApplyRemoteSetInteraction(
                *animation, *stage, performerPosition, interaction);
            SKSE::log::info(
                "OSTNET PPA SETINTERACTION RX connection={} animation=\"{}\" stage={} performer={} target={}({}) targetActor={} explicit={} applied={}",
                message.sender.connectionID, *animation, *stage,
                performerPosition, interaction.target,
                TargetName(interaction.target),
                interaction.targetActorPosition,
                interaction.hasExplicitTargetActor ? 1 : 0,
                applied ? 1 : 0);
        }
    }

    bool PPAIntegration::ApplyRemoteSetTarget(
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        std::uint8_t target)
    {
        if (!CanApplyRemoteTargets()) {
            return false;
        }
        auto* tagger = _getAnimationTagger();
        if (!tagger) {
            return false;
        }
        // Calling the preserved original stub bypasses our entry hook, so a
        // remote write cannot echo back onto the network.
        _setTargetOriginal(
            tagger, animation, stage, performerPosition, target);
        return true;
    }

    bool PPAIntegration::ApplyRemoteSetInteraction(
        const std::string& animation,
        std::int32_t stage,
        std::uint8_t performerPosition,
        const StageInteractionRaw& interaction)
    {
        if (!CanApplyRemoteTargets()) {
            return false;
        }
        auto* tagger = _getAnimationTagger();
        if (!tagger) {
            return false;
        }
        _setInteractionOriginal(
            tagger,
            animation,
            stage,
            performerPosition,
            std::addressof(interaction));
        return true;
    }
}
