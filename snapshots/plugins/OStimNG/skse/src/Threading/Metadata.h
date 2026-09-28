#pragma once

#include "Util/MapUtil.h"
#include "Util/VectorUtil.h"

namespace Threading {
    struct Metadata {
    private:
        std::vector<std::string> metadata;
        std::unordered_map<std::string, float> metaFloats;
        std::unordered_map<std::string, std::string> metaStrings;

    public:
        inline bool hasMetadata(std::string metadata) { return VectorUtil::contains(this->metadata, metadata); }
        inline void addMetadata(std::string metadata) { if (!hasMetadata(metadata)) this->metadata.push_back(metadata); }
        inline std::vector<std::string> getMetadata() { return metadata; }

        inline bool hasMetaFloat(std::string key) { return metaFloats.contains(key); }
        inline float getMetaFloat(std::string key) { return MapUtil::getOrFallback(metaFloats, key, 0.0f); }
        inline void setMetaFloat(std::string key, float value) { metaFloats[key] = value; }

        inline bool hasMetaString(std::string key) { return metaStrings.contains(key); }
        inline std::string getMetaString(std::string key) { return MapUtil::getOrFallback<std::string, std::string>(metaStrings, key, ""); }
        inline void setMetaString(std::string key, std::string value) { metaStrings[key] = value; }
    };
}