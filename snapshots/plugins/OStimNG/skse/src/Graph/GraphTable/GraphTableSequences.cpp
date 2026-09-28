#include "Graph/GraphTable.h"

#include "Util/JsonFileLoader.h"
#include "Util/RNGUtil.h"
#include "Util/StringUtil.h"

namespace Graph {
    const char* SEQUENCE_FILE_PATH{"Data/SKSE/Plugins/OStim/sequences"};

    void GraphTable::setupSequences() {
        Util::JsonFileLoader::LoadFilesInFolder(SEQUENCE_FILE_PATH, [&](std::string path, std::string filename, json json) {
            std::string id = filename;
            StringUtil::toLower(&id);

            if (!json.is_object()) {
                logger::warn("sequence {} is malformed", filename);
                return;
            }

            if (!json.contains("scenes")) {
                logger::warn("sequence {} does not have field scenes defined", filename);
                return;
            }

            if (!json["scenes"].is_array()) {
                logger::warn("field scenes of sequence {} is not a list", filename);
                return;
            }

            Sequence sequence = {.id = id};

            int index = 0;
            for (auto& entry : json["scenes"]) {
                if (!entry.is_object()) {
                    logger::warn("scene {} in sequence {} is malformed", index, filename);
                    return;
                }

                if (!entry.contains("id")) {
                    logger::warn("scene {} in sequence {} does not have field id defined", index, filename);
                    return;
                }

                if (!entry["id"].is_string()) {
                    logger::warn("field id of scene {} in sequence {} is not a string", index, filename);
                    return;
                }

                Node* node = getNodeById(entry["id"]);
                if (!node) {
                    logger::warn("id {} of scene {} in sequence {} doesn't exist", (std::string)entry["id"], index, filename);
                    return;
                }

                if (!sequence.nodes.empty()) {
                    if (node->furnitureType->isChildOf(sequence.furnitureType)) {
                        sequence.furnitureType = node->furnitureType;
                    } else if (!sequence.furnitureType->isChildOf(node->furnitureType)) {
                        logger::warn("scene {} in sequence {} has the wrong furniture type", index, filename);
                        return;
                    }

                    Node* front = sequence.nodes.front().node;
                    if (node->actors.size() != front->actors.size()) {
                        logger::warn("scene {} in sequence {} has the wrong actor count", index, filename);
                        return;
                    }

                    if (!node->hasSameActorTpyes(front)) {
                        logger::warn("scene {} in sequence {} has the wrong actor types", index, filename);
                        return;
                    }
                } else {
                    sequence.furnitureType = node->furnitureType;
                }

                SequenceEntry sEntry = {.node = node};

                if (entry.contains("duration")) {
                    if (!entry["duration"].is_number()) {
                        logger::warn("field duration of scene {} in sequence {} is not a number", index, filename);
                        return;
                    }

                    sEntry.duration = static_cast<int>(static_cast<float>(entry["duration"]) * 1000.0f);
                } else {
                    sEntry.duration = sEntry.node->animationLengthMs;
                }

                sequence.nodes.push_back(sEntry);
                index++;
            }

            if (sequence.nodes.empty()) {
                logger::warn("sequence {} is empty", filename);
                return;
            }

            if (json.contains("tags")) {
                if (json["tags"].is_array()) {
                    index = 0;
                    for (auto& tag : json["tags"]) {
                        if (tag.is_string()) {
                            sequence.tags.push_back(tag);
                        } else {
                            logger::warn("tag {} of sequence {} is not a string", index, filename);
                        }
                        index++;
                    }
                } else {
                    logger::warn("field tags of sequence {} is not a list", filename);
                }
            }

            sequences[id] = sequence;
        });
    }


    Sequence* GraphTable::getSequenceByID(std::string id) {
        StringUtil::toLower(&id);
        auto iter = sequences.find(id);
        if (iter != sequences.end()) {
            return &iter->second;
        }
        return nullptr;
    }

    Sequence* GraphTable::getRandomSequence(Furniture::FurnitureType* furnitureType, std::vector<Trait::ActorCondition> actorConditions, std::function<bool(Sequence*)> sequenceCondition) {
        std::vector<Sequence*> copy;
        for (auto& [id, sequence] : sequences) {
            Node* front = sequence.nodes.front().node;
            if (front->actors.size() != actorConditions.size()) {
                continue;
            }

            if (!furnitureType->isChildOf(sequence.furnitureType)) {
                continue;
            }

            copy.push_back(&sequence);
        }

        if (copy.empty()) {
            return nullptr;
        }

        std::shuffle(std::begin(copy), std::end(copy), RNGUtil::RNG);

        for (Sequence* sequence : copy) {
            if (sequence->fulfilledBy(actorConditions) && sequenceCondition(sequence)) {
                return sequence;
            }
        }

        return nullptr;
    }
}