console.log("hello from pageDevinfo");
var slaveid = 0;
var errCount = 0;

function loadDevicesList() {
	$("#devCombobox")
         		.append($("<option selected disabled hidden></option>")
                    .attr("value", 0)
                    .text("select device")); 
	$(this).load(root + '/ui/devices', function (a) {
    	var json = JSON.parse(a);
    	$.each(json, function(i, item) {		    
    		$("#devCombobox")
         		.append($("<option></option>")
                    .attr("value", item.slaveid)
                    .text(item.name)); 
		});
    });
    //slaveid = $("#devCombobox option:selected").attr();
}

$("#devCombobox").change(function(){
	slaveid = $(this).children("option:selected").val();
	//console.log("new slaveid " + slaveid);
	loadDevice();
});

function loadDevice(){ 
	if (slaveid == 0)   
		return;	
    $(this).load(root+'/ui/device?slaveid='+slaveid,function (data, status) {
    	if (status == "error") {
    		if (errCount++ > 5) {
	           clearInterval(intervalId);    
	           console.log("Too many errors, autorefresh disabled.");
	        }
    		return;
    	}

    	var json = JSON.parse(data);
    	//console.log(json);
    	var jOutputs = json.outputs;
		var outputs = "<div class=\"flex-container\" padding >";
		// todo : sort json. need to reverse inputs & outputs
		//$.each(jOutputs, function(i, item) {		    
		    // outputs += "<div><div>"+item.id+"</div>";
		    // if (item.curVal == 1)
		    //  	outputs += "<div id=\""+item.id+"\" class=\"led\" style=\"color: lime\">&#149;</div>";
		    // else 
		    // 	outputs += "<div id=\""+item.id+"\" class=\"led\" style=\"color: gray\">&#149;</div>";
		    // outputs += "<div>"+item.name+"</div>"
		    // outputs += "</div>"; 		    
		//});
		for (var i=0; i<jOutputs.length; i++) {
			var item = jOutputs[jOutputs.length-i-1];
			outputs += "<div><div>"+item.id+"</div>";
		    if (item.curVal == 1)
		     	outputs += "<div id=\""+item.id+"\" class=\"led\" style=\"color: lime\">&#149;</div>";
		    else 
		    	outputs += "<div id=\""+item.id+"\" class=\"led\" style=\"color: gray\">&#149;</div>";
		    outputs += "<div>"+item.name+"</div>"
		    outputs += "</div>"; 		    
		}
		outputs += "</div>";
		$("#Outputs").html(outputs);
		$("#slaveId").html(json.slaveid);

		$(".led").on('click', function(a) {
			//console.log("click " + a.target.id);
            $.ajax({
                type: "POST",
                url: "/ui/output?slaveid="+slaveid+"&output="+a.target.id+"&action=3",                
                contentType: "text/json; charset=utf-8",
                dataType: "text",
                success: function (msg, status, jqXHR) {
                    //var jsonUpdatedData = msg;                    
                    //console.log(msg);
        			if (status == "error") {
					    var msg = "Error: ";
					    $( "#error" ).html( msg + xhr.status + " " + xhr.statusText );
					    $("#content").html("");
					    //console.log(xhr);				    
				    } else {				  	
				  		$( "#error" ).html("");
				    }			
                }
            });
		})

		var jInputs = json.inputs;
		var inputs = "<div class=\"flex-container\">";
		//$.each(jInputs, function(i, item) {		    
		for (var i=0; i<jInputs.length; i++) {
			var item = jInputs[jInputs.length-i-1];
		    inputs += "<div><div>"+item.id+"</div>";
		    if (item.isButton == 1)
		    	inputs += "<div class=\"led\" style=\"color: blue\">&#149;</div>";
		    else {
			    if (item.curVal == 1)
			     	inputs += "<div class=\"led\" style=\"color: lime\">&#149;</div>";
			    else 
			    	inputs += "<div class=\"led\" style=\"color: gray\">&#149;</div>";
			}
		    inputs += "<div>"+item.name+"</div>"
		    inputs += "</div>"; 
		    //console.log(item.id);
		//});
		}
		inputs += "</div>";
		$("#Inputs").html(inputs);
		
        //$(this).unwrap();
    });
}

loadDevicesList();
loadDevice(); // This will run on page load

intervalId = setInterval(function(){     
	if (globalId == "pageDevinfo")
    loadDevice();
  else
    clearInterval(intervalId);    
}, 500);
