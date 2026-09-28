#pragma once

namespace Threading {
	using ThreadActorFlags = uint32_t;

	enum ThreadActorFlag {
		NO_OVERRIDE_EXPRESSION   = 1 << 0,
		NO_UNDERLYING_EXPRESSION = 1 << 1
	};
}