console.log("hello from statusPage");
loadStatus();
var errCount = 0;
//var intervalId;

function loadStatus() {
	$(this).load(root + '/ui/status', loadCallback);
}

function loadCallback(data, status) {
    if (status == "error") {
        if (errCount++ > 5) {
           clearInterval(intervalId);    
           console.log("Too many errors, autorefresh disabled.");
        }
        return;
    }
    var json = JSON.parse(data);
    //console.log(json);
    $("#devName").text(json.devicename);
    $("#uptime").text(json.uptime);
    $("#freememory").text(json.freememory);
    $("#curdate").text(json.curdate);
}

intervalId = setInterval(function(){     
	if (globalId == "pageStatus")
        loadStatus();
    else
        clearInterval(intervalId);
}, 1000);
