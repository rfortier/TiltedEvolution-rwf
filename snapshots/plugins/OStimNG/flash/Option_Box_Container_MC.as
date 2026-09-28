
import com.greensock.*;
class Option_Box_Container_MC extends MovieClip
{


	public function Option_Box_Container_MC()
	{
		// constructor code
	}

	public function setPosition(xPos:Number, yPos:Number, xScale:Number, yScale:Number)
	{
		this._x = xPos + 160;
		this._y = yPos + 980;
		this._xscale = xScale;
		this._yscale = yScale;
	}

	var faded = false;
	public function StartFadeOut(time:Number)
	{
		TweenLite.killDelayedCallsTo(FadeOut);
		TweenLite.delayedCall(time,FadeOut,[],this);
	}
	public function KillFadeOut()
	{
		TweenLite.killDelayedCallsTo(FadeOut);
	}

	public function FadeOut()
	{
		if (!faded)
		{
			TweenLite.killTweensOf(this,true,{_alpha:true});
			TweenLite.to(this,1.5,{_alpha:0});
			faded = true;
		}
	}

	public function FadeIn()
	{
		if (faded)
		{
			TweenLite.killTweensOf(this,true,{_alpha:true});
			TweenLite.to(this,0.2,{_alpha:100});
			faded = false;
		}
	}
}