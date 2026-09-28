#pragma once

#include "ThreadAlignment.h"

#include "Graph/Node.h"

namespace Alignment {
    class Alignments {
    public:
        static void LoadAlignments();
        static void SerializeAlignments();

        static ActorAlignment getActorAlignment(std::string threadKey, Graph::Node* node, int index);
        static void setActorAlignment(std::string threadKey, Graph::Node* node, int index, ActorAlignment alignment);
        
    private:
        inline static std::unordered_map<std::string, ThreadAlignment> threadAlignments;

        static ActorAlignment getDefaultAlignment(Graph::Node* node, int index);
    };
}  // namespace OAlign