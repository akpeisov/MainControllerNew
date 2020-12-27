console.log("hello from pageNetwork");

function loadData() {
	$(this).load(root + '/service/config/network', function (a) {
    	var json = JSON.parse(a);
        $("#networkmode").val(json.networkmode);
        $("#ethdhcp").prop("checked", json.ethdhcp);              
    	$("#ethip").val(json.ethip); 
    	$("#ethnetmask").val(json.ethnetmask); 
    	$("#ethgateway").val(json.ethgateway);
    	
    	$("#wifi_ssid").val(json.wifi_ssid); 
    	$("#wifi_pass").val(json.wifi_pass); 
        $("#wifidhcp").prop("checked", json.wifidhcp);              
        $("#wifiip").val(json.wifiip); 
        $("#wifinetmask").val(json.wifinetmask); 
        $("#wifigateway").val(json.wifigateway);
    	
        $("#hostname").val(json.hostname);         
        $("#dns").val(json.dns);         
        $("#ntpserver").val(json.ntpserver);         
        $("#ntpTZ").val(json.ntpTZ);         
        $("#otaURL").val(json.otaURL);         
    });    
}

function ethDisabled(val) {    
    $("#ethdhcp").prop("disabled", val);
    $("#ethip").prop("disabled", val);
    $("#ethnetmask").prop("disabled", val);
    $("#ethgateway").prop("disabled", val);
}

function wifiDisabled(val) {    
    $("#wifidhcp").prop("disabled", val);
    $("#wifiip").prop("disabled", val);
    $("#wifinetmask").prop("disabled", val);
    $("#wifigateway").prop("disabled", val);
}

$("#networkmode").change(function () {
    var nMode = parseInt($(this).children("option:selected").val());
    console.log(nMode);        
    switch (nMode) {
        case 0:
            ethDisabled(false);
            wifiDisabled(true);
            break;
        case 1:
            ethDisabled(true);
            wifiDisabled(false);
            break;
        case 2:
            ethDisabled(false);
            wifiDisabled(false);
            break;
        default:
    }
  }).change();

function saveData() {
	// TODO : make validation
	var json = new Object();
    json.networkmode = parseInt($("#networkmode").children("option:selected").val());
    json.ethdhcp = $("#ethdhcp").prop("checked"); 
    json.ethip = $("#ethip").val();
    json.ethnetmask = $("#ethnetmask").val();
    json.ethgateway = $("#ethgateway").val(); 
    
    json.wifi_ssid = $("#wifi_ssid").val();    
    json.wifi_pass = $("#wifi_pass").val();
    json.wifidhcp = $("#wifidhcp").prop("checked"); 
    json.wifiip = $("#wifiip").val();
    json.wifinetmask = $("#wifinetmask").val();
    json.wifigateway = $("#wifigateway").val(); 
    
    json.dns = $("#dns").val();
    json.hostname = $("#hostname").val();
    json.ntpserver = $("#ntpserver").val();
    json.ntpTZ = $("#ntpTZ").val();   
    json.otaURL = $("#otaURL").val();   
    console.log(json);
    $.ajax({
        type: "POST",
        url: "/service/config/network",            
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

loadData();