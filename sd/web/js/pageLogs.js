console.log("hello from logsPage");

$(this).load(root + '/ui/log', loadCallback);

function loadCallback(data, status, xhr) {
    if (status == "error") {
        $( "#error" ).html( xhr.status + " " + xhr.statusText );           
    } else {
        $( "#error" ).html(" ");            
        var table = $("<table />");
        var row = $("<tr><th>Datetime</th><th>Millis</th><th>Message</th></tr>");            
            table.append(row);
        var rows = data.split("\n");
        for (var i = 0; i < rows.length; i++) {
            var row = $("<tr />");
            var cells = rows[i].split(";");
            for (var j = 0; j < cells.length; j++) {
                if (j != 2) { // type
                    var cell = $("<td />");
                    cell.html(cells[j]);
                    row.append(cell);
                } else {
                    if (cells[j] == "W")
                        row.css("color","yellow");
                    else if (cells[j] == "I")
                        row.css("color","green");
                    else if (cells[j] == "E")
                        row.css("color","red");
                }
            }
            table.append(row);
        }
        $("#tableLog").html('');
        $("#tableLog").append(table);            
    }    
}

