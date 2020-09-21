console.log("hello from pageService");

function loadData() {
	$(this).load(root + '/service/config/service', function (a) {
    	var json = JSON.parse(a);
    	$("#pollingTime").val(json.pollingTime); 
    	$("#pollingTimeout").val(json.pollingTimeout); 
    	$("#pollingRetries").val(json.pollingRetries); 
    	$("#waitingRetries").val(json.waitingRetries); 
    	$("#savePeriod").val(json.savePeriod); 
    	// $("#httpEnable").prop("checked", json.httpEnable);
    	$("#httpsEnable").prop("checked", json.httpsEnable);
    	$("#authEnable").prop("checked", json.authEnable);
    	$("#authUser").val(json.authUser); 
    	$("#authPass").val(json.authPass); 
    	$("#wdteth").prop("checked", json.wdteth);
    	$("#wdtmem").prop("checked", json.wdtmem);
    	$("#wdtmemsize").val(json.wdtmemsize); 
        $("#readtimeout").val(json.readtimeout);         
        $("#actionslaveproc").prop("checked", json.actionslaveproc);        
    }); 
}

function saveData() {
	// TODO : make validation
	var json = new Object();
	json.pollingTime = parseInt($("#pollingTime").val());
	json.pollingTimeout = parseInt($("#pollingTimeout").val());
	json.pollingRetries = parseInt($("#pollingRetries").val());
	json.waitingRetries = parseInt($("#waitingRetries").val());
	json.savePeriod = parseInt($("#savePeriod").val());
	// json.httpEnable = $("#httpEnable").prop("checked");
	json.httpsEnable = $("#httpsEnable").prop("checked");
	json.authEnable = $("#authEnable").prop("checked");
	json.authUser = $("#authUser").val();
	json.authPass = $("#authPass").val();
	json.wdteth = $("#wdteth").prop("checked");	
	json.wdtmem = $("#wdtmem").prop("checked");	
	json.wdtmemsize = parseInt($("#wdtmemsize").val());
    json.readtimeout = parseInt($("#readtimeout").val());
    json.actionslaveproc = $("#actionslaveproc").prop("checked");
	//console.log(JSON.stringify(json));
    $.ajax({
        type: "POST",
        url: "/service/config/service",            
        contentType: "text/json; charset=utf-8",
        dataType: "text",
        data: JSON.stringify(json),
        success: function (msg, status, xhr) {
            console.log("stat " + status);
            if (status == "success") {
                $("#error").css('color', 'green');
                $("#error").html("Saved success");                
            } else {                    
                var msg = "Error: ";
                $("#error").html( msg + xhr.status + " " + xhr.statusText );                
            }
        },
        error: function (msg, status, xhr) {
            //console.log(msg);
            $("#error").css('color', 'red');
            $( "#error" ).html(msg.responseText);            
        }
    });

}

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
			  		loadData();
			    }	
            }
        });
    }

}

loadData();
