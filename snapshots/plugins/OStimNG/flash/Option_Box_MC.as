// original script by osmosis-wrench 2022
import skse;
import gfx.managers.FocusHandler;
import Mouse;
import com.greensock.*;
class Option_Box_MC extends MovieClip
{
	var bg:MovieClip;
	var optionDesc:TextField;

	var Options:Array = new Array(0);
	var CurrentlyHighlightedIdx:Number;
	var maxOptionIdx:Number = -1;

	var optionGutter = 14;
	var optionGutterX = optionGutter;
	var optionGutterY = optionGutter;

	var menuGutter = 28;
	var menuGutterX;

	var menuGutterYText = 86;
	var menuGutterYBase;
	var menuGutterYTop;
	var menuGutterY;

	var optionWidth = 86;
	var optionHeight = 86;

	var minWidth;
	var maxWidth;

	var TextureLoader;

	var speedUp:MovieClip;
	var speedDown:MovieClip;
	var speedDesc:TextField;
	var speedControls = new Array(2);
	var currentSpeedIdx:Number;

	// 0 animations;
	// 1 speed controls;
	// 2 settings menus;
	var selectedSubMenu:Number = 0;
	var menuselector_mc:MovieClip;

	var fadeOutTime = 2.0;

	public function Option_Box_MC()
	{
		super();
		menuselector_mc = _parent.menuselector_mc;

		speedControls = [speedDown, speedUp];
		CurrentlyHighlightedIdx = -1;

		TextureLoader = new MovieClipLoader();
		TextureLoader.addListener(this);

		menuGutterX = menuGutter * 2;

		menuGutterYBase = menuGutterYText + optionGutter;
		menuGutterYTop = menuGutter;
		menuGutterY = menuGutterYBase + menuGutterYTop;

		minWidth = menuGutterX + 150;
		maxWidth = menuGutterX + (3 * optionWidth) + (2 * optionGutterX);

		bg._width = maxWidth;
		bg._x = bg._width / 2;
		this.speedUp.stop();
		this.speedDown.stop();

		// AssignData(generateTestData());// For testing in flash
	}

	public function SetSettings(fadeOutTime:Number)
	{
		this.fadeOutTime = fadeOutTime;
	}

	public function HandleKeyboardInput(e:Number)
	{

		// 0 = up, 1 = down, 2 = left, 3 = right, 4 = select;

		if (maxOptionIdx == -1)
		{
			return;
		}
		var col = getCol(CurrentlyHighlightedIdx);
		var row = Math.floor(CurrentlyHighlightedIdx / 3);

		var colMin = col;
		var colMax = maxOptionIdx - ((3 - (((col - (maxOptionIdx % 3)) + 3) % 3)) % 3);

		var rowMin = 3 * row;
		var rowMax = Math.min(maxOptionIdx, (3 * row) + 2);

		switch (e)
		{
			case 0:
				{
					switch (selectedSubMenu)
					{
						case 0:
							if (CurrentlyHighlightedIdx + 3 <= colMax)
							{
								HighlightOption(CurrentlyHighlightedIdx + 3);
							}
							break;
						case 1:
							if (currentSpeedIdx == 1)
							{
								SetSubMenu(0);
							}
							else
							{
								if (speedUp.usable)
								{
									HighlightSpeed(1);
								}
								else
								{
									SetSubMenu(0);
								}
							}
							break;
						case 2:
							menuselector_mc.NavUp();
							break;
					}
				};
				break;
			case 1:
				{
					switch (selectedSubMenu)
					{
						case 0:
							if (CurrentlyHighlightedIdx - 3 < colMin)
							{
								if (speedUp._visible == true)
								{
									SetSubMenu(1);
								}
							}
							else
							{
								HighlightOption(CurrentlyHighlightedIdx - 3);
							}
							break;
						case 1:
							if (currentSpeedIdx == 1)
							{
								if (speedDown.usable)
								{
									HighlightSpeed(0);
								}
							}
							break;
						case 2:
							menuselector_mc.NavDown();
							break;
					}
				};
				break;
			case 2:
				{
					switch (selectedSubMenu)
					{
						case 0:
							if (CurrentlyHighlightedIdx - 1 < rowMin)
							{
								SetSubMenu(2);
							}
							else
							{
								HighlightOption(CurrentlyHighlightedIdx - 1);
							}
							break;
						case 1:
							SetSubMenu(2);
							break;
					}
				};
				break;
			case 3:
				{
					switch (selectedSubMenu)
					{
						case 0:
							if (CurrentlyHighlightedIdx + 1 <= rowMax)
							{
								HighlightOption(CurrentlyHighlightedIdx + 1);
							}
							break;
						case 2:
							SetSubMenu(0);
							break;
					}
				};
				break;
			case 4:
				switch (selectedSubMenu)
				{
					case 0:
						Options[CurrentlyHighlightedIdx].OnSelect();
						break;
					case 1:
						SelectSpeed(currentSpeedIdx);
						break;
					case 2:
						if(menuselector_mc.SelectOption() == 1){
							SetSubMenu(0)
						}
						break;
				}

				break;
		}
		FadeTrigger();
	}

	function FadeTrigger()
	{

		_parent.KillFadeOut();
		_parent.FadeIn();
		_parent.StartFadeOut(fadeOutTime);
	}

	function SetSubMenu(subMenu:Number)
	{
		switch (selectedSubMenu)
		{
			case 0:
				Options[CurrentlyHighlightedIdx].OnUnHighlight();
				break;
			case 1:
				speedUp.OnUnHighlight();
				speedDown.OnUnHighlight();
				break;
			case 2:
				menuselector_mc.LeaveMenu();
				break;
		}
		switch (subMenu)
		{
			case 0:
				HighlightOption(CurrentlyHighlightedIdx);
				break;
			case 1:
				if (speedUp.usable)
				{
					HighlightSpeed(1);
				}
				else
				{
					HighlightSpeed(0);
				}
				break;
			case 2:
				menuselector_mc.EnterMenu();
				break;
		}
		selectedSubMenu = subMenu;
	}

	function HighlightOption(idx:Number)
	{
		if (CurrentlyHighlightedIdx != -1)
		{
			Options[CurrentlyHighlightedIdx].OnUnHighlight();
		}
		CurrentlyHighlightedIdx = idx;
		Options[CurrentlyHighlightedIdx].OnHighlight();
		optionDesc.text = Options[CurrentlyHighlightedIdx].Description;
	}

	function HighlightSpeed(idx:Number)
	{
		if (currentSpeedIdx != -1)
		{
			speedControls[currentSpeedIdx].OnUnHighlight();
		}
		currentSpeedIdx = idx;
		speedControls[currentSpeedIdx].OnHighlight();
		if (currentSpeedIdx == 0)
		{
			optionDesc.text = "Slow down";
		}
		else
		{
			optionDesc.text = "Speed up";
		}
	}

	public function SelectOption(NodeID:String)
	{
		log("UI_TransitionRequest {}, {" + NodeID + "}");
		doSelectOption(NodeID);
	}

	public function doSelectOption(sceneId:String)
	{
	}

	public function AssignData(Edges:Array)
	{
		for (var j = 0; j < Options.length; j++)
		{
			Options[j].HideOption();
		}

		maxOptionIdx = Edges.length - 1;
		var maxOptionRow = Math.floor(maxOptionIdx / 3);
		var noOfCols = maxOptionIdx >= 3 ? 3 : maxOptionIdx + 1;
		var newHeight = menuGutterY + ((maxOptionRow + 1) * optionHeight) + (maxOptionRow * optionGutterY);
		TweenLite.to(bg, 0.5, {_height: newHeight, _y: 0 - (newHeight / 2)});

		if (Edges.length == 0)
		{
			TweenLite.to(this, 0.2, {_alpha: 0});
			TweenLite.to(menuselector_mc, 0.2, {_alpha: 0});
			return;
		}
		else
		{
			TweenLite.to(this, 0.2, {_alpha: 100});
			TweenLite.to(menuselector_mc, 0.2, {_alpha: 100});
			FadeTrigger();
		}
		for (var i = 0; i < Edges.length; i++)
		{
			if (Options.length <= i)
			{
				Options.push(GenerateOption(i, getCol(i), getRow(i)));
			}
			Options[i].SetData(Edges[i]);
			Options[i].ShowOption();
		}
		CurrentlyHighlightedIdx = -1;
		HighlightOption(0);
	}

	function getCol(idx):Number
	{
		return idx % 3;
	}

	function getRow(idx):Number
	{
		return Math.floor(idx / 3);
	}

	public function GenerateOption(idx, colIdx, rowIdx):MovieClip
	{
		var mc = this.attachMovie("Option_MC", "o" + idx, idx + 1, {_x: (menuGutterX / 2) + (colIdx * (optionWidth + optionGutterX)) + (optionWidth / 2), _y: 0 - ((menuGutterYBase) + (rowIdx * (optionHeight + optionGutterY)) + (optionHeight / 2)), _width: optionWidth, _height: optionHeight});
		mc.doSelect = function()
		{
			_parent.SelectOption(this.NodeID);
		};
		mc.myIdx = idx;
		mc.TextureLoader = this.TextureLoader;
		return mc;
	}

	function log(str:String)
	{
		trace(str);
	}

	function onLoadInit(aTargetClip)
	{
		if (undefined != this.TextureLoader)
		{
			if (undefined != aTargetClip)
			{
				aTargetClip._width = 82;
				aTargetClip._height = 82;
			}
		}
	}

	function generateTestData()
	{
		var arr = new Array(15);
		for (var i = 0; i < arr.length; i++)
		{
			var obj = {};
			obj.Border = "FFFFFF";
			obj.ImagePath = "test" + i + ".dds";
			obj.Description = "Standing, holding or some other description thats long";
			arr[i] = obj;
		}
		return arr;
	}

	function ShowSpeed(speedVal:String, showUp:Boolean, showDown:Boolean)
	{
		speedUp._visible = true;
		speedUp.Show(showUp);

		speedDown._visible = true;
		speedDown.Show(showDown);

		speedDesc._visible = true;
		speedDesc.text = speedVal;

		if (selectedSubMenu == 1)
		{
			if (currentSpeedIdx == 0)
			{
				if (showDown == false)
				{
					HighlightSpeed(1);
				}
				else
				{
					HighlightSpeed(0);
				}
			}
			else if (currentSpeedIdx == 1)
			{
				if (showUp == false)
				{
					HighlightSpeed(0);
				}
				else
				{
					HighlightSpeed(1);
				}
			}
		}
	}

	function SelectSpeed()
	{
		doChangeSpeed(currentSpeedIdx == 1);
	}

	function doChangeSpeed(val:Boolean)
	{
	}

	function SpeedUp()
	{
		speedUp.play();
	}
	function SpeedDown()
	{
		speedDown.play();
	}

	function HideSpeed()
	{
		speedUp._visible = false;
		speedDown._visible = false;
		speedDesc._visible = false;

		if (selectedSubMenu == 1)
		{
			SetSubMenu(0);
		}
	}
}