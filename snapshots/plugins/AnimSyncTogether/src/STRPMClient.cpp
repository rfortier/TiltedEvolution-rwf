#include "AnimSyncTogether/STRPMClient.h"

#include "AnimSyncTogether/AnimationClipProbe.h"
#include "AnimSyncTogether/AnimationProbe.h"
#include "AnimSyncTogether/OARClient.h"
#include "AnimSyncTogether/SyncRules.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <span>
#include <string>

namespace AnimSyncTogether
{
    namespace
    {
        constexpr char kAnimationChannel[] = "caelvanost.animsynctogether.anim.v1";
        constexpr std::uint32_t kSyncPacketVersion = 3;
        constexpr std::uint32_t kStateHasGPMAAnimationType = 1u << 0;
        constexpr std::uint32_t kStateHasGPMAOffsetType = 1u << 1;

        enum class SyncPacketKind : std::uint32_t
        {
            kAnimationEvent = 1,
            kGraphVariable = 2
        };

        struct SyncPacket
        {
            std::uint32_t version{ kSyncPacketVersion };
            std::uint32_t kind{ static_cast<std::uint32_t>(SyncPacketKind::kAnimationEvent) };
            std::uint32_t stateFlags{ 0 };
            std::uint32_t graphVariableType{ 0 };
            std::int32_t gpmaAnimationType{ 0 };
            std::int32_t gpmaOffsetType{ 0 };
            std::int32_t graphVariableInt{ 0 };
            float graphVariableFloat{ 0.0F };
            std::uint32_t graphVariableBool{ 0 };
            std::array<char, 64> tag{};
            std::array<char, 128> payload{};
        };

        void CopyField(std::span<char> destination, std::string_view source)
        {
            if (destination.empty()) {
                return;
            }

            const auto count = (std::min)(source.size(), destination.size() - 1);
            std::memcpy(destination.data(), source.data(), count);
            destination[count] = '\0';
        }

        STRPM::Target AllPlayersTarget()
        {
            return STRPM::Target{
                STRPM::TargetKind::kAllPlayers,
                0,
                nullptr
            };
        }
    }

    STRPMClient* STRPMClient::GetSingleton()
    {
        static STRPMClient singleton;
        return std::addressof(singleton);
    }

    bool STRPMClient::Initialize()
    {
        OARClient::GetSingleton()->Initialize();
        SyncRules::GetSingleton()->Initialize();

        if (!messaging_) {
            messaging_ = STRPM::LoadFromModule();
            if (!messaging_) {
                SKSE::log::warn("STRPMClient: messaging API is not available");
                return false;
            }
        }

        if (!channelRegistered_) {
            const auto result = messaging_->registerChannel(
                kAnimationChannel,
                &STRPMClient::OnSyncMessage,
                this,
                &animationListener_);
            if (result != STRPM::Result::kOk) {
                SKSE::log::error(
                    "STRPMClient: sync channel registration failed ({})",
                    STRPM::ResultToString(result));
                return false;
            }
            channelRegistered_ = true;
            SKSE::log::info(
                "STRPMClient: sync channel registered '{}'; packetVersion={} profiles={}",
                kAnimationChannel,
                kSyncPacketVersion,
                SyncRules::GetSingleton()->GetProfileCount());
        }

        if (!resolver_) {
            resolver_ = STRPM::LoadProxyResolverFromModule();
            if (!resolver_) {
                SKSE::log::warn("STRPMClient: ProxyResolver API is not available");
                return false;
            }
        }

        if (!listenerRegistered_) {
            const auto result = resolver_->registerListener(&STRPMClient::OnProxyMappingChanged, this);
            if (result != STRPM::Result::kOk) {
                SKSE::log::error(
                    "STRPMClient: registerListener failed ({})",
                    STRPM::ResultToString(result));
                resolver_ = nullptr;
                return false;
            }

            listenerRegistered_ = true;
            SKSE::log::info("STRPMClient: ProxyResolver listener registered");
        }

        return true;
    }

    bool STRPMClient::IsAvailable() const noexcept
    {
        return messaging_ != nullptr && channelRegistered_ && resolver_ != nullptr && listenerRegistered_;
    }

    std::optional<STRPM::ConnectionID> STRPMClient::FindConnectionID(RE::FormID formID) const
    {
        const auto it = formIDToConnection_.find(formID);
        if (it == formIDToConnection_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool STRPMClient::SendAnimationEvent(
        std::string_view tag,
        std::string_view payload,
        std::int32_t gpmaAnimationType,
        bool hasGPMAAnimationType,
        std::int32_t gpmaOffsetType,
        bool hasGPMAOffsetType)
    {
        if (!messaging_ || !channelRegistered_) {
            return false;
        }

        SyncPacket packet{};
        packet.kind = static_cast<std::uint32_t>(SyncPacketKind::kAnimationEvent);
        if (hasGPMAAnimationType) {
            packet.stateFlags |= kStateHasGPMAAnimationType;
            packet.gpmaAnimationType = gpmaAnimationType;
        }
        if (hasGPMAOffsetType) {
            packet.stateFlags |= kStateHasGPMAOffsetType;
            packet.gpmaOffsetType = gpmaOffsetType;
        }
        CopyField(packet.tag, tag);
        CopyField(packet.payload, payload);

        const auto target = AllPlayersTarget();
        const auto result = messaging_->send(
            kAnimationChannel,
            target,
            &packet,
            sizeof(packet),
            STRPM::kMessageReliable | STRPM::kMessageOrdered);

        if (result != STRPM::Result::kOk) {
            SKSE::log::warn(
                "STRPMClient: animation send failed event='{}' result={}",
                tag,
                STRPM::ResultToString(result));
            return false;
        }

        SKSE::log::info(
            "AnimTxInput event='{}' stateFlags=0x{:X} animationType={} offsetType={}",
            tag,
            packet.stateFlags,
            packet.gpmaAnimationType,
            packet.gpmaOffsetType);
        return true;
    }

    bool STRPMClient::SendGraphVariableBool(std::string_view name, bool value)
    {
        if (!messaging_ || !channelRegistered_) {
            return false;
        }

        SyncPacket packet{};
        packet.kind = static_cast<std::uint32_t>(SyncPacketKind::kGraphVariable);
        packet.graphVariableType = static_cast<std::uint32_t>(GraphVariableType::kBool);
        packet.graphVariableBool = value ? 1u : 0u;
        CopyField(packet.tag, name);

        const auto target = AllPlayersTarget();
        return messaging_->send(
                   kAnimationChannel,
                   target,
                   &packet,
                   sizeof(packet),
                   STRPM::kMessageReliable | STRPM::kMessageOrdered) == STRPM::Result::kOk;
    }

    bool STRPMClient::SendGraphVariableInt(std::string_view name, std::int32_t value)
    {
        if (!messaging_ || !channelRegistered_) {
            return false;
        }

        SyncPacket packet{};
        packet.kind = static_cast<std::uint32_t>(SyncPacketKind::kGraphVariable);
        packet.graphVariableType = static_cast<std::uint32_t>(GraphVariableType::kInt);
        packet.graphVariableInt = value;
        CopyField(packet.tag, name);

        const auto target = AllPlayersTarget();
        return messaging_->send(
                   kAnimationChannel,
                   target,
                   &packet,
                   sizeof(packet),
                   STRPM::kMessageReliable | STRPM::kMessageOrdered) == STRPM::Result::kOk;
    }

    bool STRPMClient::SendGraphVariableFloat(std::string_view name, float value)
    {
        if (!messaging_ || !channelRegistered_) {
            return false;
        }

        SyncPacket packet{};
        packet.kind = static_cast<std::uint32_t>(SyncPacketKind::kGraphVariable);
        packet.graphVariableType = static_cast<std::uint32_t>(GraphVariableType::kFloat);
        packet.graphVariableFloat = value;
        CopyField(packet.tag, name);

        const auto target = AllPlayersTarget();
        return messaging_->send(
                   kAnimationChannel,
                   target,
                   &packet,
                   sizeof(packet),
                   STRPM::kMessageReliable | STRPM::kMessageOrdered) == STRPM::Result::kOk;
    }

    void STRPM_CALL STRPMClient::OnSyncMessage(
        const STRPM::Message* message,
        void* userData)
    {
        if (!message || !userData || !message->data || message->size != sizeof(SyncPacket)) {
            return;
        }

        const auto* packet = static_cast<const SyncPacket*>(message->data);
        if (packet->version != kSyncPacketVersion) {
            return;
        }

        const std::string tag(packet->tag.data(), strnlen_s(packet->tag.data(), packet->tag.size()));
        const auto kind = static_cast<SyncPacketKind>(packet->kind);
        auto* self = static_cast<STRPMClient*>(userData);

        if (kind == SyncPacketKind::kAnimationEvent) {
            const std::string payload(
                packet->payload.data(),
                strnlen_s(packet->payload.data(), packet->payload.size()));
            self->QueueAnimationMessage(
                message->sender.connectionID,
                tag,
                payload,
                packet->stateFlags,
                packet->gpmaAnimationType,
                packet->gpmaOffsetType);
        } else if (kind == SyncPacketKind::kGraphVariable) {
            self->QueueGraphVariableMessage(
                message->sender.connectionID,
                tag,
                packet->graphVariableType,
                packet->graphVariableBool != 0,
                packet->graphVariableInt,
                packet->graphVariableFloat);
        }
    }

    void STRPMClient::QueueAnimationMessage(
        STRPM::ConnectionID senderConnectionID,
        std::string tag,
        std::string payload,
        std::uint32_t stateFlags,
        std::int32_t gpmaAnimationType,
        std::int32_t gpmaOffsetType)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            SKSE::log::error("STRPMClient: SKSE task interface unavailable; dropping animation message");
            return;
        }

        tasks->AddTask([
            this,
            senderConnectionID,
            tag = std::move(tag),
            payload = std::move(payload),
            stateFlags,
            gpmaAnimationType,
            gpmaOffsetType]() {
            ApplyAnimationMessage(
                senderConnectionID,
                tag,
                payload,
                stateFlags,
                gpmaAnimationType,
                gpmaOffsetType);
        });
    }

    void STRPMClient::QueueGraphVariableMessage(
        STRPM::ConnectionID senderConnectionID,
        std::string name,
        std::uint32_t variableType,
        bool boolValue,
        std::int32_t intValue,
        float floatValue)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            SKSE::log::error("STRPMClient: SKSE task interface unavailable; dropping graph variable message");
            return;
        }

        tasks->AddTask([
            this,
            senderConnectionID,
            name = std::move(name),
            variableType,
            boolValue,
            intValue,
            floatValue]() {
            ApplyGraphVariableMessage(
                senderConnectionID,
                name,
                variableType,
                boolValue,
                intValue,
                floatValue);
        });
    }

    void STRPMClient::ApplyAnimationMessage(
        STRPM::ConnectionID senderConnectionID,
        const std::string& tag,
        const std::string& payload,
        std::uint32_t stateFlags,
        std::int32_t gpmaAnimationType,
        std::int32_t gpmaOffsetType)
    {
        (void)payload;

        if (!resolver_) {
            return;
        }

        STRPM::ProxyFormID formID{};
        const auto result = resolver_->resolve(senderConnectionID, &formID);
        if (result != STRPM::Result::kOk || formID == STRPM::kInvalidProxyFormID) {
            SKSE::log::info(
                "AnimRxInput unresolved sender={} event='{}' result={}",
                senderConnectionID,
                tag,
                STRPM::ResultToString(result));
            return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (!actor) {
            SKSE::log::info(
                "AnimRxInput sender={} proxy={:08X} actor='<missing>' event='{}' replay=false",
                senderConnectionID,
                formID,
                tag);
            return;
        }

        if (!SyncRules::GetSingleton()->ShouldSyncEvent(tag)) {
            SKSE::log::info(
                "AnimRxInput sender={} proxy={:08X} actor='{}' event='{}' replay=false reason='not in sync rules'",
                senderConnectionID,
                formID,
                actor->GetName(),
                tag);
            return;
        }

        const RE::BSFixedString animationTypeVariable{ "iGPMAAnimationType" };
        const RE::BSFixedString offsetTypeVariable{ "iGPMAOffsetType" };

        if (tag == "OffsetGPMA") {
            bool animationTypeApplied = false;
            bool offsetTypeApplied = false;

            if ((stateFlags & kStateHasGPMAAnimationType) != 0) {
                animationTypeApplied = actor->SetGraphVariableInt(animationTypeVariable, gpmaAnimationType);
            }
            if ((stateFlags & kStateHasGPMAOffsetType) != 0) {
                offsetTypeApplied = actor->SetGraphVariableInt(offsetTypeVariable, gpmaOffsetType);
            }

            SKSE::log::info(
                "GPMAStateApply actor={:08X} event='OffsetGPMA' animationTypePresent={} animationType={} animationTypeApplied={} offsetTypePresent={} offsetType={} offsetTypeApplied={}",
                formID,
                (stateFlags & kStateHasGPMAAnimationType) != 0,
                gpmaAnimationType,
                animationTypeApplied,
                (stateFlags & kStateHasGPMAOffsetType) != 0,
                gpmaOffsetType,
                offsetTypeApplied);

            AnimationClipProbe::ArmActor(formID, "remote OffsetGPMA with GPMA state");
        }

        // Graph-variable packets are delivered on the same reliable ordered
        // channel before the matching custom event. Refresh OAR immediately before
        // replay so its replacement conditions see the synchronized proxy state.
        OARClient::GetSingleton()->ClearConditionStateData(actor);

        const RE::BSFixedString inputEvent{ tag.c_str() };
        const bool replayed = actor->NotifyAnimationGraph(inputEvent);

        SKSE::log::info(
            "AnimRxInput sender={} proxy={:08X} actor='{}' event='{}' replay=true result={} stateFlags=0x{:X} animationType={} offsetType={}",
            senderConnectionID,
            formID,
            actor->GetName(),
            tag,
            replayed,
            stateFlags,
            gpmaAnimationType,
            gpmaOffsetType);

        if (tag == "OffsetGPMAStop") {
            const bool reset = actor->SetGraphVariableInt(animationTypeVariable, 0);
            SKSE::log::info(
                "GPMAStateReset actor={:08X} variable='iGPMAAnimationType' value=0 result={} source='network stop'",
                formID,
                reset);
        }
    }

    void STRPMClient::ApplyGraphVariableMessage(
        STRPM::ConnectionID senderConnectionID,
        const std::string& name,
        std::uint32_t variableType,
        bool boolValue,
        std::int32_t intValue,
        float floatValue)
    {
        if (!resolver_) {
            return;
        }

        const auto type = static_cast<GraphVariableType>(variableType);
        if (!SyncRules::GetSingleton()->ShouldSyncGraphVariable(name, type)) {
            SKSE::log::warn(
                "GraphVarRx rejected sender={} name='{}' type={} reason='not in sync rules'",
                senderConnectionID,
                name,
                variableType);
            return;
        }

        STRPM::ProxyFormID formID{};
        const auto result = resolver_->resolve(senderConnectionID, &formID);
        if (result != STRPM::Result::kOk || formID == STRPM::kInvalidProxyFormID) {
            SKSE::log::info(
                "GraphVarRx unresolved sender={} name='{}' result={}",
                senderConnectionID,
                name,
                STRPM::ResultToString(result));
            return;
        }

        auto* actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (!actor) {
            SKSE::log::info(
                "GraphVarRx sender={} proxy={:08X} actor='<missing>' name='{}' applied=false",
                senderConnectionID,
                formID,
                name);
            return;
        }

        const RE::BSFixedString variableName{ name.c_str() };
        bool applied = false;

        switch (type) {
        case GraphVariableType::kBool:
            applied = actor->SetGraphVariableBool(variableName, boolValue);
            SKSE::log::info(
                "GraphVarRx sender={} proxy={:08X} actor='{}' name='{}' type=bool value={} applied={}",
                senderConnectionID,
                formID,
                actor->GetName(),
                name,
                boolValue,
                applied);
            break;

        case GraphVariableType::kInt:
            applied = actor->SetGraphVariableInt(variableName, intValue);
            SKSE::log::info(
                "GraphVarRx sender={} proxy={:08X} actor='{}' name='{}' type=int value={} applied={}",
                senderConnectionID,
                formID,
                actor->GetName(),
                name,
                intValue,
                applied);
            break;

        case GraphVariableType::kFloat:
            applied = actor->SetGraphVariableFloat(variableName, floatValue);
            SKSE::log::info(
                "GraphVarRx sender={} proxy={:08X} actor='{}' name='{}' type=float value={:.6f} applied={}",
                senderConnectionID,
                formID,
                actor->GetName(),
                name,
                floatValue,
                applied);
            break;
        }

        if (applied) {
            OARClient::GetSingleton()->ClearConditionStateData(actor);
        }
    }

    void STRPM_CALL STRPMClient::OnProxyMappingChanged(
        const STRPM::ProxyMappingEvent* event,
        void* userData)
    {
        if (!event || !userData) {
            return;
        }

        static_cast<STRPMClient*>(userData)->QueueMappingEvent(*event);
    }

    void STRPMClient::QueueMappingEvent(const STRPM::ProxyMappingEvent& event)
    {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            SKSE::log::error("STRPMClient: SKSE task interface unavailable; dropping proxy mapping event");
            return;
        }

        tasks->AddTask([this, event]() {
            ApplyMappingEvent(event);
        });
    }

    void STRPMClient::ApplyMappingEvent(const STRPM::ProxyMappingEvent& event)
    {
        auto* probe = AnimationProbe::GetSingleton();

        switch (event.type) {
        case STRPM::ProxyMappingEventType::kAdded:
        case STRPM::ProxyMappingEventType::kUpdated:
            if (event.oldFormID != STRPM::kInvalidProxyFormID && event.oldFormID != event.newFormID) {
                probe->DetachActorByFormID(event.oldFormID, "STRPM mapping replaced");
                formIDToConnection_.erase(event.oldFormID);
            }

            if (event.newFormID == STRPM::kInvalidProxyFormID) {
                return;
            }

            connectionToFormID_[event.connectionID] = event.newFormID;
            formIDToConnection_[event.newFormID] = event.connectionID;

            SKSE::log::info(
                "STRPMClient: proxy mapping {} connection={} formID={:08X}",
                event.type == STRPM::ProxyMappingEventType::kAdded ? "added" : "updated",
                event.connectionID,
                event.newFormID);

            probe->AttachActorByFormID(event.newFormID, "STRPM proxy mapping");
            break;

        case STRPM::ProxyMappingEventType::kRemoved:
            if (event.oldFormID != STRPM::kInvalidProxyFormID) {
                probe->DetachActorByFormID(event.oldFormID, "STRPM mapping removed");
                formIDToConnection_.erase(event.oldFormID);
            }
            connectionToFormID_.erase(event.connectionID);
            SKSE::log::info(
                "STRPMClient: proxy mapping removed connection={} formID={:08X}",
                event.connectionID,
                event.oldFormID);
            break;

        case STRPM::ProxyMappingEventType::kCleared:
            for (const auto& [formID, connectionID] : formIDToConnection_) {
                (void)connectionID;
                probe->DetachActorByFormID(formID, "STRPM mappings cleared");
            }
            connectionToFormID_.clear();
            formIDToConnection_.clear();
            SKSE::log::info("STRPMClient: proxy mappings cleared");
            break;
        }
    }
}
