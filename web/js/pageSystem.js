console.log("hello from pageSystem");


function factoryReset() {
    if (confirm('Are you sure you want to reset factory defaults?')) {
        $.ajax({
            type: "POST",
            url: "/service/config/factoryReset?reset=reset",
            contentType: "text/json; charset=utf-8",            
            dataType: "text",
            success: function (msg, status, jqXHR) {
                if (status == "error") {
				    var msg = "Error: ";
				    $( "#error" ).html( msg + xhr.status + " " + xhr.statusText );
				    $("#content").html("");				    
			    } else {
			     	// load default data				  				  		
			  		$( "#error" ).html("");
			    }	
            }
        });
    }
}


function reboot() {
    if (confirm('Are you sure you want to reboot device?')) {
        $.ajax({
            type: "POST",
            url: "/service/reboot?reboot=reboot",
            contentType: "text/json; charset=utf-8",            
            dataType: "text",
            success: function (msg, status, jqXHR) {
                if (status == "error") {
                    var msg = "Error: ";
                    $( "#error" ).html( msg + xhr.status + " " + xhr.statusText );
                    $("#content").html("");                 
                } else {
                    // load default data                                        
                    $( "#error" ).html("");
                }     
            }
        });
    }
}
