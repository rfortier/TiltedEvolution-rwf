
class ExcitementBars extends MovieClip
{
	var bars : Array;
	var testText : MovieClip;	
	public function ExcitementBars(){
		bars = new Array(0);
	}
	
	public function Update(values : Array){
	}
	
	public function AssignData(barsData){
		//Come back to this later. This does work in spawning the correct number of imported bar graphics
		return;
		trace(barsData.actors.length);
		for(var i = 0; i < barsData.actors.length; i++){
			var id = "excBar" + Number(i);
			if(bars[i] == undefined) {
				trace("creating new bar");
				
				var mc = this.attachMovie("Bar", id, i+1,{ _y: (i * -50), _width:400, _height:30});
				trace(mc);
				bars.push(mc);
				
			}
			trace(bars[i])
			trace("bar at " + bars[i]._y);
		}
	}
}