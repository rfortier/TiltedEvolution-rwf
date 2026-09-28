# pragma once

#include "GameActor.h"
#include "GameAPIUtil.h"
#include "GameRecord.h"

namespace GameAPI {
    struct GameSound : GameRecord<RE::BGSSoundDescriptorForm> {
    public:
        void play(GameActor actor, float volume);

        inline bool isPlaying() {
            return IsPlaying(&handle);
        }

        inline void stop() {
            handle.Stop();
        }

    private:
        RE::BSSoundHandle handle;

        static inline bool IsPlaying(RE::BSSoundHandle* handle) {
            using func_t = decltype(IsPlaying);
            REL::Relocation<func_t> func{RELOCATION_ID(66359, 67620)};
            return func(handle);
        }
    };
}