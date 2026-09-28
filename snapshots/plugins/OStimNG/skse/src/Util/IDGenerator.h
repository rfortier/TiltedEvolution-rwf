#pragma once

namespace Util {
    class IDGenerator {
    public:
        inline IDGenerator(int start) : start{start}, next{start} {}

        int get();
        inline void free(int id) { freeIDs.push(id); }
        void reset();

    private:
        int start;
        int next;

        std::queue<int> freeIDs;
    };
}