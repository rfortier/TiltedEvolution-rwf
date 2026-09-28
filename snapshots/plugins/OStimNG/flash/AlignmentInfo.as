import gfx.ui.InputDetails;
import Shared.GlobalFunc;
import gfx.ui.NavigationCode;

class AlignmentInfo extends MovieClip
{
	var selectedField = 0;
	/* STAGE ELEMENTS */
	public var actorNameData:TextField;
	public var actorSlotData:TextField;
	public var actorGenderData:TextField;
	public var sourceData:TextField;
	public var incrementField:TextField;
	/*Fields*/
	public var xField:InfoField;
	public var yField:InfoField;
	public var zField:InfoField;
	public var sField:InfoField;
	public var rField:InfoField;
	public var pField:InfoField;

	var prevFocus;
	function AlignmentInfo()
	{
		super();
		this.selectField(this.selectedField);
	}
	public function handleInput(details:InputDetails, pathToFocus:Array):Boolean
	{
		if (GlobalFunc.IsKeyPressed(details))
		{
			if (details.navEquivalent == NavigationCode.TAB || details.navEquivalent == NavigationCode.ESCAPE)
			{
				doHideMenuRequest();
				return true;
			}
		}
	}

	function Show()
	{
		prevFocus = Selection.getFocus();
		Selection.setFocus(this);
		gfx.managers.FocusHandler.instance.setFocus(this,0);
	}
	function Hide()
	{
		if (prevFocus)
		{
			gfx.managers.FocusHandler.instance.setFocus(prevFocus,0);
			Selection.setFocus(prevFocus);
		}
	}

	function doHideMenuRequest()
	{
	}

	function updateInfo(actorNameValue, actorSlotValue, actorGenderValue, incrementValue, xOffsetValue, yOffsetValue, zOffsetValue, sValue, rValue, pBendValue)
	{
		this.actorNameData.text = actorNameValue;
		this.actorSlotData.text = actorSlotValue.toString();
		this.actorGenderData.text = actorGenderValue;
		this.incrementField.text = incrementValue;
		this.xField.updateText(this.formatNumber(xOffsetValue));
		this.yField.updateText(this.formatNumber(yOffsetValue));
		this.zField.updateText(this.formatNumber(zOffsetValue));
		this.sField.updateText(this.formatNumber(sValue));
		this.rField.updateText(this.formatNumber(rValue));
		this.pField.updateText(pBendValue);
	}
	function updateIncrement(newVal)
	{
		this.incrementField.text = newVal;
	}
	function selectField(field)
	{
		this.getSelectedField().deselect();
		this.selectedField = field;
		this.getSelectedField().select();
	}
	function updateDoubleField(newVal, up)
	{
		this.updateField(this.selectedField == 5 ? newVal : this.formatNumber(newVal),up);
	}
	function updateField(newVal, up)
	{
		this.getSelectedField().animateUpdateText(newVal,up);
	}
	function getSelectedField()
	{
		switch (this.selectedField)
		{
			case 0 :
				return this.xField;
			case 1 :
				return this.yField;
			case 2 :
				return this.zField;
			case 3 :
				return this.sField;
			case 4 :
				return this.rField;
			case 5 :
				return this.pField;
			default :
		}
	}
	function formatNumber(val)
	{
		var _loc2_ = val.toString();
		if (_loc2_.indexOf("E") >= 0)
		{
			return "0.000";
		}
		var _loc3_ = _loc2_.split(".");
		if (_loc3_.length == 1)
		{
			return _loc2_ + ".000";
		}
		if (_loc3_[1].length > 3)
		{
			_loc2_ = _loc2_.substr(0, _loc3_[0].length + 1 + 3);
		}
		var _loc4_ = _loc3_[1].length;
		while (_loc4_ < 3)
		{
			_loc2_ += "0";
			_loc4_ += 1;
		}
		return _loc2_;
	}

	function setPosition(xPos, yPos, xScale, yScale)
	{
		this._x = -140 + xPos;
		this._y = 170 + yPos;
		this._xscale = xScale;
		this._yscale = yScale;
	}
}