class SceneInfo extends MovieClip
{
	/* STAGE ELEMENTS */
   public var packData:TextField;
   public var animNameData:TextField;
   public var genderMapData: TextField;

   function SceneInfo()
   {
      super();
   }

   function updateInfo(packVal, animNameVal, genderMapVal)
   {
      this.packData.text = packVal;
      this.animNameData.text = animNameVal;
      this.genderMapData.text = genderMapVal;
   }   
   
  function setPosition(xPos, yPos, xScale, yScale)
   {
	   this._x = -140 + xPos;
	   this._y = 170 + yPos;
	   this._xscale = xScale;
	   this._yscale = yScale;
   }
}
