import com.greensock.*;

class SpeedButton extends MovieClip
{

	var shape:MovieClip;
	var usable:Boolean;

	public function SpeedButton()
	{
		_alpha = 65;
	}

	public function Show(showButton:Boolean)
	{
		if (!showButton)
		{
			this._alpha = 30;
			usable = false;
		}
		else
		{
			this._alpha = 65;
			usable = true;
		}
	}

	public function OnHighlight()
	{
		this._alpha = 100;
	}

	public function OnUnHighlight()
	{
		if (usable)
		{
			this._alpha = 65;
		}
		else
		{
			this._alpha = 30;
		}
	}
}