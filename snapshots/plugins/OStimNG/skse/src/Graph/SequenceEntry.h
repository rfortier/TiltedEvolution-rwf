#pragma once

namespace Graph {
    struct Node;

    struct SequenceEntry {
        int duration = 0;
        Node* node = nullptr;
    };
}