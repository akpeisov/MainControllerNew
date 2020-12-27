var newDeviceRow = {
        "slaveid": 0,
        "name": "",
        "description": "",
        "status": "offline",
        "polling": false,
        "autotimeout": 4000
      };
var newInputRow = {
        "id": 0,
        "isButton": 0,
        "name": "",
        "curVal": 0,
        "isNew": true
      };
var newOutputRow = {
        "id": 0,        
        "name": "",
        "room": "",
        "curVal": 0                       
      };      
var newEventRow = {
        "event": "off",        
        "slaveid": 2,   
        "output": 2,
        "action": "off"   
      };

var tableTree = new Tabulator("#tableDevicesTree", {
    //height:"311px",    
    layout:"fitData", // fitDataFill, fitData    
    dataTree:true,
    //data:devicesTree,    
    ajaxURL:"/ui/devicesTree",
    dataTreeStartExpanded:false,
    responsiveLayout:"collapse",
    selectable: false,
    columns:[
        {title:"Devices", field:"name", width:400},        
        ],
    rowClick:function(e, row){
        //console.log(row._row.parent.getData());
        var slaveid = -1;
        var id = -1;
        // смотрим родилетя
        if (parent = row.getTreeParent()) {
            // если он есть, то возможно у него тоже есть парент
            if (gparent = parent.getTreeParent()) {
                // выходим на верхний уровень (девайс)
                slaveid = gparent.getData().slaveid;
                id = row.getData().id;
            } else 
                slaveid = parent.getData().slaveid;
        } else {
            // если его нет, то значит мы на самом верху (девайс)
            slaveid = row.getData().slaveid;
        }
        //console.log(row.getTreeParent().getData().slaveid);
        treeRowClick(slaveid, row.getData().type, id);
        console.log(slaveid);
    }
});

var buttons = "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bDevAdd\">Add device</button>"+
              "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bDevAddFile\">Add device from file</button>"+
              "<input id=\"file-input-add\" type=\"file\" name=\"addDevice\" style=\"display: none;\"/>";
$("#buttonsLeftDiv").html(buttons);
$("#bDevAdd").click(function(){        
            deviceTable(0);            
        });
$("#bDevAddFile").click(function(){        
    $('#file-input-add').trigger('click');    
});

$('#file-input-add').on('change', function () {
    var fileReader = new FileReader();
    fileReader.onload = function () {
        var data = fileReader.result;  // data <-- in this var you have the file data in Base64 format
        //console.log(data);
        $.ajax({
              type: "POST",
              url: "/ui/device?slaveid=0",
              data: data,
              contentType: "text/json; charset=utf-8",
              dataType: "text",
              success: function (msg, status, xhr) {
                    //var jsonUpdatedData = msg;                    
                    $( "#error" ).html( msg + xhr.status + " " + xhr.statusText );
                    console.log(msg);
                },
              error: function (xhr, exception) {
                    $( "#error" ).html( xhr.status + " " + xhr.responseText );
                }
        });
    };
    fileReader.readAsText($('#file-input-add').prop('files')[0]);
});


function treeRowClick(slaveid, type, id) {
    console.log("slaveid " + slaveid + " id " + id + " type " + type);
    // сам девайс - slaveid + type
    // выходы - slaveid + type
    // входы - slaveid + type
    // вход - slaveid + type + id
    if (type == "outputs")
        outputsTable(slaveid);
    else if (type == "inputs")
        inputsTable(slaveid);
    else if (type == "input")
        eventsTable(slaveid, id);
    else if (type == "device")
        deviceTable(slaveid);
}
//var curOutputsTable;
function deviceTable(slaveid) {
    // get /getDevice?slaveid=1
    // /deviceData    
    function makeDeviceTable(dev) {
        //var dev = deviceData; // replace ajax
        function isSelected(val1, val2) {
            if (val1 == val2)
                return "selected";
            return "";  
        }
        var id = dev.slaveid;
        console.log(dev);
        var polling = "";
        if (dev.polling)
            polling = "checked=\"checked\"";
        var ros = "";
        if (dev.ros)
            ros = "checked=\"checked\"";  
        var inverse = "";
        if (dev.inverse)
            inverse = "checked=\"checked\"";  
        var ol = dev.status;
        if (ol == "online")
            ol = "style=\"color:lime\"";
        else
            ol = "style=\"color:red\"";
        var area = "<table><tbody>"+
                   "<tr><td><label>SlaveId</label>"+
                   "<td>&nbsp;</td>"+
                   "<td><input class=\"txtStr\" type=\"text\" id=\"dev-slaveid\" value=\""+dev.slaveid+"\" maxlength=\"2\"></td>"+
                   "</tr>"+
                   "<tr><td><label>Devce name</label>"+
                   "<td>&nbsp;</td>"+
                   "<td><input class=\"txtStr\" type=\"text\" id=\"dev-name\" value=\""+dev.name+"\" maxlength=\"20\"></td>"+
                   "</tr>"+
                   "<tr><td><label>Description</label>"+
                   "<td>&nbsp;</td>"+
                   "<td><input class=\"txtStr\" type=\"text\" id=\"dev-description\" value=\""+dev.description+"\" maxlength=\"20\"></td>"+
                   "</tr>"+
                   "<tr><td><label>Timeout for auto mode, ms</label>"+
                   "<td>&nbsp;</td>"+
                   "<td><input class=\"txtStr\" type=\"text\" id=\"dev-autotimeout\" value=\""+dev.autotimeout+"\" maxlength=\"6\"></td>"+
                   "</tr>"+
                   "<tr><td><label>Restore outputs on start</label>"+
                   "<td>&nbsp;</td>"+
                   "<td><input id=\"dev-ros\" type=\"checkbox\" "+ros+"/></td>"+
                   "</tr>"+
                   "<tr><td><label>Inverse outputs (for 16 IO)</label>"+
                   "<td>&nbsp;</td>"+
                   "<td><input id=\"dev-inverse\" type=\"checkbox\" "+inverse+"/></td>"+
                   "</tr>"+
                   "<tr><td><label>Mode</label>"+
                   "<td>&nbsp;</td>"+
                   "<td><select id=\"dev-mode\">"+
                   "<option "+ isSelected(dev.mode, 0) + " value=\"0\">Passive</option>"+
                   "<option "+ isSelected(dev.mode, 1) + " value=\"1\">Active</option>"+
                   "<option "+ isSelected(dev.mode, 2) + " value=\"2\">Auto</option>"+
                   "</select></td>"+
                   "</tr>"+                   
                   "<tr><td><label>Status</label>"+
                   "<td>&nbsp;</td>"+
                   "<td "+ol+">"+dev.status+"</td>"+
                   "</tr>"+
                   "<tr><td><label>Polling</label>"+
                   "<td>&nbsp;</td>"+
                   "<td><input id=\"dev-polling\" type=\"checkbox\" "+polling+"/></td>"+
                   "</tr>"+
                   "<tr><td><label>Polling mode (1 coil, 2 input, 3 events)</label>"+
                   "<td>&nbsp;</td>"+
                   "<td><input class=\"txtStr\" type=\"text\" id=\"dev-pollingmode\" value=\""+dev.pollingmode+"\" maxlength=\"1\"></td>"+
                   "</tr>"+
                   "</table>";
        /*
        var area = "<p class=\"labelDev\">SlaveId</p><input id=\"dev-slaveid\" value=\""+dev.slaveid+"\"/><br>"+ 
                   "<p class=\"labelDev\">Name</p><input id=\"dev-name\" value=\""+dev.name+"\"/><br>"+ 
                   "<p class=\"labelDev\">Description</p><input id=\"dev-description\" value=\""+dev.description+"\"/><br>"+ 
                   "<p class=\"labelDev\">Status</p>"+dev.status+ "<br>";
        */
                   
        var buttons = "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bSave\">Save</button>";
        buttons += "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bDelete\">Delete</button>";
        buttons += "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bSetConfig\">Set device config</button>";
        $("#tableDiv").removeClass("tabulator");
        $("#tableDiv").html(area);
        $("#buttonsDiv").html(buttons);

        $("#bSave").click(function(){        
            dev.slaveid = parseInt($("#dev-slaveid").val());
            dev.name = $("#dev-name").val();        
            dev.description = $("#dev-description").val();        
            dev.autotimeout = parseInt($("#dev-autotimeout").val());
            dev.polling = $("#dev-polling").prop("checked");
            dev.pollingmode = parseInt($("#dev-pollingmode").val());
            dev.ros = $("#dev-ros").prop("checked");
            dev.inverse = $("#dev-inverse").prop("checked");
            dev.mode = parseInt($("#dev-mode").val());
            dev.id = id;
            console.log(dev);
            // слать так: POST /setDevice?slaveid=111
            // в данных слать объект dev, чтобы была возможность изменить slaveid
            var jsonData = JSON.stringify(dev);
            //console.log(jsonData);
            $.ajax({
              type: "POST",
              url: "/ui/device?slaveid="+id,
              data: jsonData,
              contentType: "text/json; charset=utf-8",
              dataType: "text",
              success: function (msg, status, xhr) {                  
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
        });
        $("#bDelete").click(function(){ 
            if (confirm('Are you sure you want to delete device '+dev.name+'?')) {
                $.ajax({
                  type: "POST",
                  url: "/ui/delDevice?slaveid="+id,                  
                  contentType: "text/json; charset=utf-8",
                  dataType: "text",
                  success: function (msg, status, jqXHR) {
                        console.log(msg);
                        tableTree.replaceData();
                        $("#tableDiv").html(null);
                  }
                });
            }
        });
        $("#bSetConfig").click(function(){        
            dev.slaveid = parseInt($("#dev-slaveid").val());                 
            var jsonData = JSON.stringify(dev);
            //console.log(jsonData);
            $.ajax({
              type: "POST",
              url: "/ui/setDeviceConfig?slaveid="+id,
              data: jsonData,
              contentType: "text/json; charset=utf-8",
              dataType: "text",
              success: function (msg, status, xhr) {
                  if (status == "success") {
                      $("#error").css('color', 'green');
                      $("#error").html("Saved success");                
                  } else {            
                      var msg = "Error: ";
                      $("#error").html( msg + xhr.status + " " + xhr.statusText );                
                  }
              },
              error: function (msg, status, xhr) {
                  $("#error").css('color', 'red');
                  $( "#error" ).html(msg.responseText);            
              }
            });
        });
    }

    if (slaveid > 0) {
        $.get("/ui/device?slaveid="+slaveid, function(data, status){
            console.log("Data: " + data + "\nStatus: " + status);
            makeDeviceTable(data); 
        });
    } else {
        // for new device
        makeDeviceTable(newDeviceRow); 
    }

    
}

function getMax(arr, prop) {        
    var max = null;
    for (var i=0 ; i<arr.length ; i++) {
        if (max == null || parseInt(arr[i][prop]) > parseInt(max[prop]))
            max = arr[i];
    }
    return max;
}

function outputsTable(slaveid) {
    $("#subTitleDiv").html("<h3>Outputs</h3>");
    // TODO : endpoint get outputs by slaveid
    var table = new Tabulator("#tableDiv", {
    layout:"fitColumns", // fitDataFill, fitData
    resizableColumns:true,
    //data:inputsData,
    ajaxURL:"/ui/outputs?slaveid="+slaveid,
    responsiveLayout:"collapse",
    selectable: true,
    columns:[
        //{formatter:"responsiveCollapse", width:30, minWidth:30, align:"center", resizable:false, headerSort:false},    

        {title:"ID", field:"id",editor:true,validator:"required"},
        {title:"Name", field:"name",editor:true,validator:"required"},                        
        {title:"Room", field:"room",editor:true,validator:"required"},                        
        {title:"Alice", field:"alice",editor:true,formatter:"tick"},
        {title:"Status", field:"curVal"},        
        //{title:"Delete", formatter:"buttonCross", cellClick:function(e, cell){if(confirm('Are you sure you want to delete this entry?')) cell.getRow().delete();}},
        ],
    rowClick:function(e, row){
        //alert("Row " + row.getIndex() + " Clicked!!!!")    
    }
    });
    
    var buttons = "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bSave\">Save</button>"+ 
                  "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bAdd\">Add output</button>"+
                  "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bDelete\">Delete output</button>";
    $("#buttonsDiv").html(buttons);

    $("#bAdd").click(function(){
        var maxId = getMax(table.getData(), "id");
        if (maxId != null)
          maxId = maxId.id; // max inputid        
        else
          maxId = -1;
        newOutputRow.id = maxId+1;
        table.addRow(Object.assign({}, newOutputRow));
    });

    $("#bSave").click(function(){
        var a = table.getData();
        console.log(a);
        var jsonData = JSON.stringify(a);
        //console.log(jsonData);
        $.ajax({
              type: "POST",
              url: "/ui/outputs?slaveid="+slaveid,
              data: jsonData,
              contentType: "text/json; charset=utf-8",
              dataType: "text",
              success: function (msg, status, xhr) {                  
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
    });

    $("#bDelete").click(function(){        
        if (confirm('Are you sure you want to delete selected items?')) {
            var selectedRows = table.getSelectedRows();
            for(var i=0;i<selectedRows.length;i++)
            {
                selectedRows[i].delete();
            }
        }        
    });
}

function inputsTable(slaveid) {
    $("#subTitleDiv").html("<h3>Inputs</h3>");
    // TODO : endpoint get inputs by slaveid
    var table = new Tabulator("#tableDiv", {
    //height:"311px",
    layout:"fitColumns", // fitDataFill, fitData
    resizableColumns:true,
    //data:inputsData,
    ajaxURL:"/ui/inputs?slaveid="+slaveid,
    //dataTreeStartExpanded:true,
    responsiveLayout:"collapse",
    selectable: true,
    columns:[
        //{formatter:"responsiveCollapse", width:30, minWidth:30, align:"center", resizable:false, headerSort:false},    

        {title:"ID", field:"id"},        
        {title:"Name", field:"name",editor:true,validator:"required"},                
        {title:"isButton", field:"isButton",editor:true,formatter:"tick"},                
        {title:"Status", field:"curVal"},
        {title:"Events", formatter:"buttonTick", cellClick:function(e, cell){eventsTable(slaveid, cell.getData().id)}},
        //{title:"Delete", formatter:"buttonCross", cellClick:function(e, cell){if(confirm('Are you sure you want to delete this entry?')) cell.getRow().delete();}},
        ],
    rowClick:function(e, row){
        //alert("Row " + row.getIndex() + " Clicked!!!!")
    
    }
    });
    
    var buttons = "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bSave\">Save</button>"+ 
                  "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bAdd\">Add input</button>"+
                  "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bDelete\">Delete input</button>";
    $("#buttonsDiv").html(buttons);

    $("#bAdd").click(function(){
        var maxId = getMax(table.getData(), "id");
        if (maxId != null)
          maxId = maxId.id; // max inputid
        else
          maxId = -1;
        newInputRow.id = maxId+1;
        table.addRow(Object.assign({}, newInputRow));    
    });

    $("#bSave").click(function(){
        var a = table.getData();
        console.log(a);
        var jsonData = JSON.stringify(a);
        //console.log(jsonData);
        $.ajax({
              type: "POST",
              url: "/ui/inputs?slaveid="+slaveid,
              data: jsonData,
              contentType: "text/json; charset=utf-8",
              dataType: "text",
              success: function (msg, status, xhr) {                  
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
    });

    $("#bDelete").click(function(){        
        if (confirm('Are you sure you want to delete selected items?')) {
            var selectedRows = table.getSelectedRows();
            for(var i=0;i<selectedRows.length;i++)
            {
                selectedRows[i].delete();
            }
        }        
    });
}

function eventsTable(slaveid, inputid) {
    $("#subTitleDiv").html("<h3>Events</h3>");
    // TODO : endpoint get events by slaveid and inputid
    var table = new Tabulator("#tableDiv", {
    //height:"311px",
    layout:"fitColumns", // fitDataFill, fitData
    resizableColumns:true,
    //data:eventsData,
    ajaxURL:"/ui/events?slaveid="+slaveid+"&inputid="+inputid,
    
    //ajaxURL:"devices2.json",
    //dataTreeStartExpanded:true,
    responsiveLayout:"collapse",
    selectable: true,
    columns:[
        //{formatter:"responsiveCollapse", width:30, minWidth:30, align:"center", resizable:false, headerSort:false},    

        {title:"Event", field:"event", editor:"select", editorParams:{values:{"off":"OFF", "on":"ON", "toggle":"Toggle"}}},
        {title:"Name", field:"name", editor:true},           
        {title:"Slaveid", field:"slaveid", editor:true, formatter:"money", formatterParams:{precision:false}},
        {title:"Output", field:"output", editor:true},
        {title:"Action", field:"action", editor:"select", editorParams:{values:{"off":"OFF", "on":"ON", "toggle":"Toggle"}}},
        //{title:"Delete", formatter:"buttonCross", cellClick:function(e, cell){if(confirm('Are you sure you want to delete this entry?')) cell.getRow().delete();}},
        ],
    rowClick:function(e, row){
        //alert("Row " + row.getIndex() + " Clicked!!!!")
    
    }
    });

    var buttons = "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bSave\">Save</button>"+ 
                  "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bAdd\">Add event</button>"+
                  "<button class=\"ui-button ui-widget ui-corner-all\" id=\"bDelete\">Delete event</button>";    
    
    $("#buttonsDiv").html(buttons);

    $("#bAdd").click(function(){
        table.addRow(Object.assign({}, newEventRow));    
    });

    $("#bSave").click(function(){
        var json = table.getData();
        for(var i = 0; i < json.length; i++) {
            json[i].slaveid = parseInt(json[i].slaveid);            
            json[i].output = parseInt(json[i].output);
        }
        console.log(json);
        var jsonData = JSON.stringify(json);
        //console.log(jsonData);
        $.ajax({
              type: "POST",
              url: "/ui/events?slaveid="+slaveid+"&inputid="+inputid,
              data: jsonData,
              contentType: "text/json; charset=utf-8",
              dataType: "text",
              success: function (msg, status, xhr) {                  
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
    });

    $("#bDelete").click(function(){        
        if (confirm('Are you sure you want to delete selected items?')) {
            var selectedRows = table.getSelectedRows();
            for(var i=0;i<selectedRows.length;i++)
            {
                selectedRows[i].delete();
            }
        }        
    });
}


$('#btnSetDevices').on('click', function () {
    $('#file-input').trigger('click');    
});

$('#file-input').on('change', function () {
    var fileReader = new FileReader();
    fileReader.onload = function () {
        var data = fileReader.result;  // data <-- in this var you have the file data in Base64 format
        //console.log(data);
        $.ajax({
              type: "POST",
              url: "/ui/devices",
              data: data,
              contentType: "text/json; charset=utf-8",
              dataType: "text",
              success: function (msg, status, xhr) {
                    //var jsonUpdatedData = msg;                    
                    $( "#error" ).html( msg + xhr.status + " " + xhr.statusText );
                    console.log(msg);
                },
              error: function (xhr, exception) {
                    $( "#error" ).html( xhr.status + " " + xhr.responseText );
                }
        });
    };
    fileReader.readAsText($('#file-input').prop('files')[0]);
});

$('#btnGetDevices').on('click', function (e) {
    e.preventDefault();
    window.location.href = "/ui/devices";
    console.log("click");    
});