import com.greensock.*;

class SearchOption extends MovieClip
{
	var searchOptionBg:MovieClip;
	var optionVal:TextField;
	var idx = 0;
	var sceneid = "";
	public function SearchOption()
	{
		optionVal.autoSize = "left";
		optionVal.wordWrap = false;
	}

	function handleInput(details, pathToFocus)
	{

	}
	function init(i:Number, val:Object)
	{		
		optionVal.text = val.label;
		this.sceneid = val.sceneid;
		idx = i;
		this.onRollOver = function()
		{
			_parent.SetIdx(this);
			_parent.UpdateHighlight();
		};
		this.onRollOut = function()
		{
			this.OnUnHighlight();
		};
		this.onMouseDown = function()
		{
			if (Mouse.getTopMostEntity()._parent == this)
			{
				_parent.SelectOption();
			}
		};
		searchOptionBg._width = this.optionVal.textWidth;
	}
	function OnHighlight()
	{
		TweenLite.to(searchOptionBg,0.5,{alpha:70});
		searchOptionBg._visible = true;
	}

	function OnUnHighlight()
	{
		TweenLite.to(searchOptionBg,0.2,{alpha:00});
		searchOptionBg._visible = false;
	}
}