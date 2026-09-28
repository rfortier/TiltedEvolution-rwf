import com.greensock.*;
import com.greensock.easing.*;
import gfx.io.GameDelegate;
import gfx.managers.FocusHandler;
import gfx.ui.InputDetails;
import gfx.ui.NavigationCode;
import Shared.GlobalFunc;
import skse;

// by osmosis-wrench 2022
class Option extends MovieClip
{
	public function Option() {
		super();
		this.onRollOver = function ()
		{
			trace(this);
		}
	}
}

