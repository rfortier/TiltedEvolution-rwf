#pragma once

#include "GameAPI/GameCondition.h"

namespace ActorProperties {
	template<class T>
	struct ActorProperty {
        GameAPI::GameCondition condition;
        T value;
	};
}