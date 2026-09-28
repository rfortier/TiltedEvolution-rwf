class InfoField extends MovieClip
{
   static var GREEN = 39168;
   static var WHITE = 16777215;
   
   public var offsetData:TextField;
   public var upButton: MovieClip;
   public var downButton: MovieClip;
   
   function InfoField()
   {
      super();
      this.upButton.stop();
      this.downButton.stop();
   }
   function updateText(textVal)
   {
      this.offsetData.text = textVal;
   }
   function select()
   {
      this.offsetData.textColor = InfoField.GREEN;
   }
   function deselect()
   {
      this.offsetData.textColor = InfoField.WHITE;
   }
   function animateUpdateText(textVal, up)
   {
      if(up)
      {
         this.upButton.play();
      }
      else
      {
         this.downButton.play();
      }
      this.updateText(textVal);
   }
}
