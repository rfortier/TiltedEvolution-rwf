#include "ToyHandler.h"

#include "Settings/Settings.h"

#include "Core/Thread.h"
#include "Util/Constants.h"

namespace Toys {
    Settings::SlotSettings* getSlotSettings(std::string slot, ToyWrapper* toy) {
        Settings::ToySettings* settings = Settings::Settings::getSingleton()->getToySettings(Settings::ToySettings::GLOBAL_KEY);
        Settings::SlotSettings* slotSettings = settings->getSlotSettings(Settings::SlotSettings::GLOBAL_KEY);
        if (slotSettings->scalingSettings.scalingType != Settings::ScalingType::INDIVIDUAL_SCALING) {
            return slotSettings;
        }

        slotSettings = settings->getSlotSettings(slot);
        if (slotSettings->scalingSettings.scalingType != Settings::ScalingType::INDIVIDUAL_SCALING) {
            return slotSettings;
        }

        std::string id = toy->getID();
        settings = Settings::Settings::getSingleton()->getToySettings(id);
        slotSettings = settings->getSlotSettings(Settings::SlotSettings::GLOBAL_KEY);
        if (slotSettings->scalingSettings.scalingType != Settings::ScalingType::INDIVIDUAL_SCALING) {
            return slotSettings;
        }

        return settings->getSlotSettings(slot);
    }


    ToyHandler::ToyHandler(Threading::ThreadActor* actor, std::string slot, ToyWrapper* toy)
        : actor{actor},
          slot {slot},
          toy{toy},
          settings{getSlotSettings(slot, toy)},
          updateInterval{toy->getUpdateInterval()} {}

    void ToyHandler::loop() {
        updateCooldown -= Constants::LOOP_TIME_MILLISECONDS;
    }

    void ToyHandler::update(uint16_t peakInterval) {
        if (updateCooldown > 0) {
            return;
        }

        updateCooldown = updateInterval;

        float baseline = 0.0f;
        float peak = 0.0f;

        float currentValue = 0.0f;
        switch (settings->scalingSettings.scalingType) {
        case Settings::ScalingType::STIMULATION_RATE:
            currentValue = actor->getBaseExcitementInc() * (1 + actor->getThread()->getRelativeSpeed());
            break;
        case Settings::ScalingType::STIMULATION_VALUE:
            currentValue = actor->getExcitement();
            break;
        case Settings::ScalingType::SPEED:
            currentValue = actor->getThread()->getRelativeSpeed();
            break;
        default:
            currentValue = settings->scalingSettings.maxScale;
            break;
        }

        if (currentValue <= settings->scalingSettings.minScale) {
            baseline = settings->scalingSettings.minMagnitude;
            peak = settings->scalingSettings.doPeaks ? settings->scalingSettings.minPeak : -1.0f;
        } else if (currentValue >= settings->scalingSettings.maxScale) {
            baseline = settings->scalingSettings.maxMagnitude;
            peak = settings->scalingSettings.doPeaks ? settings->scalingSettings.maxPeak : -1.0f;
        } else {
            float scale = (currentValue - settings->scalingSettings.minScale) / (settings->scalingSettings.maxScale - settings->scalingSettings.minScale);
            baseline = settings->scalingSettings.minMagnitude + (settings->scalingSettings.maxMagnitude - settings->scalingSettings.minMagnitude) * scale;
            peak = settings->scalingSettings.doPeaks ? settings->scalingSettings.minPeak + (settings->scalingSettings.maxPeak - settings->scalingSettings.minPeak) * scale : -1.0f;
        }

        toy->update(baseline, peak, settings->scalingSettings.doPeaks ? peakInterval : -1);
    }

    void ToyHandler::climax() {
        if (!settings->climaxSettings.doClimax) {
            return;
        }

        updateCooldown = 1000;

        
        toy->peak(settings->climaxSettings.climaxMagnitude, 1);
    }

    void ToyHandler::stop() {
        toy->stop();
    }
}