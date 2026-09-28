#include "SynchronizationSettings.h"

#include "Util/JsonUtil.h"

namespace Toys {
    namespace Settings {
        void SynchronizationSettings::serialize(Serialization::SerializationInfo& info) {
            info.serial.write<SynchronizationType>(synchronizationType);
        }

        void SynchronizationSettings::deserialize(Serialization::DeserializationInfo& info) {
            synchronizationType = info.serial.read<SynchronizationType>();
        }

        void SynchronizationSettings::reset(Serialization::ResetInfo& info) {
            synchronizationType = synchronizationTypeDefault;
        }


        void SynchronizationSettings::writeJson(json& json) {
            json["synchronizationType"] = static_cast<int>(synchronizationType);
        }

        void SynchronizationSettings::readJson(json& json) {
            std::string objectName = "export";

            JsonUtil::consumeInt(json, [this](int value) { synchronizationType = static_cast<SynchronizationType>(value); }, "synchronizationType", objectName, "setting", false);
        }        
    }
}