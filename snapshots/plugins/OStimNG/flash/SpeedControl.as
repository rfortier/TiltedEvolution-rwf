import com.greensock.*;
import com.greensock.easing.*;
import gfx.io.GameDelegate;
import gfx.managers.FocusHandler;
import gfx.ui.InputDetails;
import gfx.ui.NavigationCode;
import Shared.GlobalFunc;
import skse;

// by osmosis-wrench 2022
class SpeedControl extends MovieClip
{
	var SpeedDown: MovieClip;
	var SpeedUp: MovieClip;
	
	var controlBase: Array = new Array(2);
	
	var speed1: MovieClip;
	var speed2: MovieClip;
	var speed3: MovieClip;
	var speed4: MovieClip;
	var speed5: MovieClip;
	
	var speedBase: Array = new Array;
	
	var speeds: Array = new Array(5);
	var speedIdx: Number;
	
	public function SpeedControl() {
		super();
		
		controlBase["_h"] = SpeedUp._height;
		controlBase["_w"] = SpeedUp._width;
		speedBase["_h"] = speed1._height;
		speedBase["_w"] = speed1._width;
		
		speeds = [speed1, speed2, speed3, speed4, speed5]
		speedIdx = 0;
		
		speeds[0]._width = speeds[0]._width+2;
		speeds[0]._height = speeds[0]._height+2;
		
		SpeedUp.onRollOver = function () {
			_parent.Highlight(this, true);
		}
		SpeedDown.onRollOver = function () {
			_parent.Highlight(this, true);
		}
		SpeedUp.onRollOut = function () {
			_parent.Highlight(this, false);
		}
		SpeedDown.onRollOut = function () {
			_parent.Highlight(this, false);
		}
		SpeedUp.onMouseDown = function () {
			if (Mouse.getTopMostEntity() == this) { 
				//_parent.ChangeSpeed(true);
			}
		}
		SpeedDown.onMouseDown = function () {
			//if (Mouse.getTopMostEntity() == this) { 
				_parent.ChangeSpeed(false);
			//}
		}
	}
	
	function onLoad() {
		
	}
	
	function Highlight(SpeedButton:MovieClip, TypeOfChange:Boolean): Void {
		if (TypeOfChange){
			TweenLite.to(SpeedButton, 0.5, {_width:controlBase._w+3, _height:controlBase._h+3});
		} else {
			TweenLite.to(SpeedButton, 0.5, {_width:controlBase._w, _height:controlBase._h});
		}
	}
	
	// note update this to instead just take the new speed, rather than what it is now.
	function ChangeSpeed(TypeOfChange:Boolean): Void {
		TypeOfChange ? speedIdx = speedIdx + 1 : speedIdx = speedIdx - 1;
		if ( speedIdx <= 0 ){
			speedIdx = 0;
			SpeedDown.gotoAndStop("hide");
		} else if ( speedIdx >= 4 ){
			speedIdx = 4;
			SpeedDown.gotoAndStop("show");
		} else {
			SpeedDown.gotoAndStop("show");
			SpeedDown.gotoAndStop("show");
		}
		trace("aaa"+speedIdx);
		for (var x = 0; x < speeds.length; x++){
			if (x <= speedIdx) {
				TweenLite.to(speeds[x], 0.5, {_width:speedBase._w+2, _height:speedBase._h+2});
			} else {
				TweenLite.to(speeds[x], 0.5, {_width:speedBase._w, _height:speedBase._h});
			}
		}
	}
}

