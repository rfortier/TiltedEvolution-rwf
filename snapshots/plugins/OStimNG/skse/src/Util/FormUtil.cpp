#include "FormUtil.h"

#include "Graph/GraphTable.h"
#include "MCM/MCMTable.h"

namespace FormUtil {
    bool isWig(RE::Actor* actor, RE::TESObjectARMO* armor) {
        if (!armor || !armor->HasPartOf(RE::BGSBipedObjectForm::BipedObjectSlot::kHair)) {
            return false;
        }

        auto& weightModel = actor->GetBiped(0);
        if (weightModel) {
            std::set<RE::NiAVObject*> touched;

            for (int i = 0; i < 42; ++i) {
                auto& data = weightModel->objects[i];
                if (data.partClone) {
                    RE::TESForm* bipedArmor = data.item;
                    RE::TESForm* addon = data.addon;
                    RE::NiAVObject* object = data.partClone.get();

                    bool isSame = bipedArmor->formType == RE::TESObjectARMO::FORMTYPE && bipedArmor->formID == armor->formID;
                    if (isSame && !touched.count(object)) {
                        if (data.partClone.get()->HasShaderType(RE::BSShaderMaterial::Feature::kHairTint)) {
                            return true;
                        } else {
                            touched.emplace(object);
                        }
                    }
                }
            }
        }
        
        return false;
    }

    bool canUndress(RE::TESForm* form) {
        if (form->formType == RE::TESObjectARMO::FORMTYPE) {
            RE::TESObjectARMO* armor = form->As<RE::TESObjectARMO>();
            if ((MCM::MCMTable::getUndressingMask() & armor->GetSlotMask().underlying()) == 0) {
                return false;
            }
        }

        RE::BGSKeywordForm* keywordForm = form->As<RE::BGSKeywordForm>();
        if (keywordForm->ContainsKeywordString("NoStrip")) {
            return false;
        }

        return true;
    }

    void apply(RE::TESImageSpaceModifier* modifier, float strength) {
        Apply(nullptr, 0, modifier, strength);
    }
}