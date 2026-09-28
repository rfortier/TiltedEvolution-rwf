class MenuSelector_MC extends MovieClip
{
	var main:Option_Box_MC;
	var bg:MovieClip;
	var options:Array = new Array(0);
	var currentlyHighlightedIdx:Number = 0;

	var elementCount = 3;
	public function MenuSelector_MC()
	{
		main = _parent.optionBoxes;
		bg._width = main.menuGutter * 2 + main.optionWidth;
		bg._height = main.menuGutter * 2 + (main.optionHeight * elementCount) + (main.optionGutter * (elementCount - 1));
		bg._x = 0;
		bg._y = 0;
		// constructor code

		GenerateOptions();
	}

	function GenerateOption(idx:Number):MovieClip
	{
		var mc = this.attachMovie("Option_MC", "s" + idx, idx + 1, {_x: bg._width / 2, _y: 0 - (main.menuGutter + ((main.optionGutter + main.optionHeight) * idx) + (main.optionHeight / 2)), _width: main.optionWidth, _height: main.optionHeight});
		mc.DrawBorder('FFFFFF');
		mc.myIdx = idx;
		mc.TextureLoader = main.TextureLoader;
		return mc;
	}

	function GenerateOptions():Void
	{
		for (var i = 0; i < elementCount; i++)
		{
			options[i] = GenerateOption(i);
			options[i].ShowOption();
		}
		options[0].SetData( {Description: "Options", ImagePath: "Ostim/icons/OStim/symbols/settings.dds", NodeID: ""});
		options[0].doSelect = function()
		{
			_parent.doShowOptions();
			return 1;
		};
		options[1].SetData( {Description: "Alignment", ImagePath: "Ostim/icons/OStim/symbols/alignment.dds", NodeID: ""});
		options[1].doSelect = function()
		{
			_parent.doShowAlignment();
			return 0;
		};
		options[2].SetData( {Description: "Search", ImagePath: "Ostim/icons/OStim/symbols/search.dds", NodeID: ""});
		options[2].doSelect = function()
		{
			_parent.doShowSearch();
			return 0;
		};
	}

	function EnterMenu()
	{
		Highlight(currentlyHighlightedIdx);
	}
	function LeaveMenu()
	{
		for (var i = 0; i < elementCount; i++)
		{
			options[i].OnUnHighlight();
		}
	}

	function NavUp():Void
	{
		if (currentlyHighlightedIdx < elementCount - 1)
		{
			options[currentlyHighlightedIdx].OnUnHighlight();
			currentlyHighlightedIdx++;
			Highlight(currentlyHighlightedIdx);
		}
	}
	function NavDown():Void
	{
		if (currentlyHighlightedIdx > 0)
		{
			options[currentlyHighlightedIdx].OnUnHighlight();
			currentlyHighlightedIdx--;
			Highlight(currentlyHighlightedIdx);
		}
	}

	function SelectOption()
	{
		return options[currentlyHighlightedIdx].OnSelect();
	}

	function Highlight(idx:Number):Void
	{
		options[idx].OnHighlight();
		main.optionDesc.text = options[idx].Description;
	}
	
	function doShowOptions()
	{
	}

	function doShowSearch()
	{
	}

	function doShowAlignment()
	{
	}
}