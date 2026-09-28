#pragma once

namespace UI {
	struct Position {
		float xPos;
		float yPos;
		float xScale;
		float yScale;
	};

	struct AlignMenuPositions {
		Position ControlPosition;
		Position InfoPosition;
	};

	struct SceneMenuPositions {
		Position ControlPosition;
		Position BarsPosition;
	};
	struct SearchMenuPositions
	{
		Position MenuPosition;
	};

	struct MenuPositions {
		AlignMenuPositions AlignMenuPosition;
		SceneMenuPositions ScenePositions;
		SearchMenuPositions SearchPositions;
	};
	

	class Settings {
	public:
		static void LoadSettings();
		inline static MenuPositions positionSettings;
		inline static float fadeTime;
	};
}