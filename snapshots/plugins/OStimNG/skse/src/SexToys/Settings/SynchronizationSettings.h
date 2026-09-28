#pragma once

#include "SynchronizationType.h"

#include "Serialization/DeserializationInfo.h"
#include "Serialization/ResetInfo.h"
#include "Serialization/SerializationInfo.h"

namespace Toys {
    namespace Settings {
        struct SynchronizationSettings {
            static constexpr SynchronizationType synchronizationTypeDefault = SynchronizationType::INDIVIDUAL_SYNCHRONIZATION;

            SynchronizationType synchronizationType = synchronizationTypeDefault;

            void serialize(Serialization::SerializationInfo& info);
            void deserialize(Serialization::DeserializationInfo& info);
            void reset(Serialization::ResetInfo& info);

            void writeJson(json& json);
            void readJson(json& json);
        };
    }
}