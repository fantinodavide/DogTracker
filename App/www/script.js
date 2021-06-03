var map;
var mapDiv;
var mapPointers = new Array;
var perimeterPointers = new Array;
var perimeterClosed = false;
var source;
var curMode = "MAP";
var curPositioning = "POSREM";
var mapLines = new Array;
var cercatore;
var coordinateManeggio = [7.8007, 44.378534];
var socket;
var title;
var internetStatus = "OFFLINE";
var wsMsgs = false;

window.onload = function () {
    /*setInterval(function(){
        let prevInternetStatus = internetStatus;
        if(httpGet("https://mt0.google.com/vt/lyrs=s&hl=en&x=68373&y=47466&z=17")!=null){
            internetStatus = "ONLINE";
        }else{
            internetStatus = "OFFLINE";
        }
        
        if(prevInternetStatus != internetStatus){
            alert("Internet Status: " + internetStatus + "\nRefreshing map")
            mapInit();
        }

        //alert("Internet tested "+internetStatus+'\n'+httpGet("http://mt0.google.com/vt/lyrs=s&hl=en&x=68373&y=47466&z=17"));
    },5000);*/
    if (httpGet("http://mt0.google.com/vt/lyrs=s&hl=en&x=68373&y=47466&z=17") != null) {
        internetStatus = "ONLINE";
    }
    //alert(internetStatus)

    mapInit();

    addEventListeners();

    init();

    /*setTimeout(function(){
        addCollare([7.801342,44.378534])
    },500)*/

    modeCheck();
}

window.onerror = function (msg, url, lineNo, columnNo, error) {
    console.log(msg + " - " + url + " - " + error);

    return false;
}
function init() {
    socketInit();

    setPosMode();
    title = document.getElementsByTagName("h1")[0];

    var btns = document.getElementsByClassName("menuBtn");
    if (!mobileCheck()) {
        for (var elm of btns) {
            elm.innerHTML = elm.innerHTML.replace("telefono", "computer");
        }
    }

    /*if(socket.readyState == socket.OPEN){
        title.style.color = "#0d0";
        var tmpInt = setInterval(function(){
            try {
            } catch (e) {

            } finally {
                clearInterval(tmpInt)
            }
        },50)
    }*/

}

function socketInit() {
    console.log("Starting socket connection");
    //socket = new WebSocket("ws://" + location.hostname + ":81");

    socket = new WebSocket("ws://192.168.4.1:81");

    socket.onopen = function (event) {
        if (title)
            title.style.color = "#0d0";

        addCercatore(coordinateManeggio);

        requestPerimeterCoords();
    }
    socket.onmessage = function (event) {
        var data = JSON.parse(event.data);
        //if(data.type!="gps")
        if (wsMsgs)
            console.log(data);

        if (data.type == "gps") {
            var coordsSplit = data.val.split('-');
            var tmpCoords = [parseFloat(coordsSplit[1]), parseFloat(coordsSplit[0])];
            var setCoords = [7.801342, 44.378534];

            if (tmpCoords[0] != 0 && tmpCoords[1] != 0) {
                setCoords = tmpCoords;
                if (data.sender != "cercatore" && !document.getElementById(data.sender)) {
                    addCollare(setCoords, data.sender)
                    //setMapCenter(setCoords)
                } else if (data.sender != "cercatore")
                    setPosPointer(document.getElementById(data.sender), setCoords)
            } else {
                if (data.sender != "cercatore" && !document.getElementById(data.sender)) {
                    addCollare(setCoords, data.sender)
                }
            }
        } else if (data.type == "ack") {
            if (data.val == "coords") {
                blinkPerimeterColor("#0b0", "#0f0");
            }
        }
    }
    socket.onclose = function (event) {
        title.style.color = "";
        socketInit();
    }
}

function blinkPerimeterColor(colorP, colorL, timeout = 2000) {
    var tmpLines = document.getElementsByTagName("line");
    var pSaveColor = perimeterPointers[0].style.background;
    var lSaveColor = tmpLines[0].style.stroke;
    for (var p of perimeterPointers) p.style.background = colorP;
    for (var l of tmpLines) l.style.stroke = colorL;
    setTimeout(function () {
        for (var p of perimeterPointers) p.style.background = pSaveColor;
        for (var l of tmpLines) l.style.stroke = lSaveColor;
    }, timeout)
}

function addEventListeners() {
    setMultipleListeners(document.getElementsByClassName("menuBtn"), "click", modeCheck);
    document.getElementById("viewMap").addEventListener("click", viewMap);
    document.getElementById("delPerimeter").addEventListener("click", clearPerimeter);
    document.getElementById("setPerimeter").addEventListener("click", regPerimeter);
    document.getElementById("sideMenuToggle").addEventListener("click", toggleSideMenu);
    document.getElementById("useTelPos").addEventListener("click", setPosMode);
    document.getElementById("useRemPos").addEventListener("click", setPosMode);
}

function setPosMode() {
    var sender = event.target;
    if (sender && sender.getAttribute && sender.getAttribute("positioning"))
        curPositioning = sender.getAttribute("positioning");
    switch (curPositioning) {
        case "POSREM":
            setMapCenter(coordinateManeggio);
            break;
        case "POSTEL":
            setTelCenter();
            break;
        default:
    }
}

function setMultipleListeners(elmArr, evt, func) {
    //console.log(elmArr)
    for (var elm of elmArr) {
        elm.addEventListener(evt, func);
        //console.log(elm);
    }
}
/*function pointInPolygon(x,y) {
    var polyY = perPointsToPolArr("y");
    var oddNodes=false;
    var polyCorners = polyY.length;
    var current=polyY[polyCorners-1]>y;
    var previous;
    for (var i=0; i<polyCorners; i++) {
        previous=current;
        current=polyY[i]>y;
        if (current!=previous)
            oddNodes^=y*multiple[i]+constant[i]<x;
    }
    return oddNodes;
}*/
function httpGet(theUrl) {
    var ret = null;
    var xmlHttp = new XMLHttpRequest();
    xmlHttp.open("GET", theUrl, false); // false for synchronous request
    try {
        xmlHttp.send(null);
        setTimeout(function () {
            if (!xmlHttp.responseText) {
                xhr.abort();
            }
        }, 1000);
    } catch (e) {
        //alert(e);
    } finally {

        if (xmlHttp.responseText)
            ret = xmlHttp.responseText;
    }
    return ret;
}

function pointInPolygon(x, y) {
    var polyY = perPointsToPolArr("y");
    var polyX = perPointsToPolArr("x");
    var polyCorners = polyY.length;
    var i, j = polyCorners - 1;
    var oddNodes = false;
    for (i = 0; i < polyCorners; i++) {
        if ((polyY[i] < y && polyY[j] >= y || polyY[j] < y && polyY[i] >= y) && (polyX[i] <= x || polyX[j] <= x)) {
            oddNodes ^= (polyX[i] + (y - polyY[i]) / (polyY[j] - polyY[i]) * (polyX[j] - polyX[i]) < x);
        }
        j = i;
    }
    return oddNodes == 1;
}
function perPointsToPolArr(ax) {
    var arrIndx = ax == "x" ? 0 : 1;
    var polY = [];
    for (var p of perimeterPointers) {
        polY.push(p.coords[arrIndx]);
    }
    return polY;
}
function viewMap() {
    curMode = "MAP";
}

function requestPerimeterCoords() {
    readTextFile("http://192.168.4.1/pCoords.json", function (txt) {
        var data = JSON.parse(txt);
        for (var c of data)
            addPerimeterPointer([c.lng, c.lat], true, c == data[data.length - 1]);
        console.log(data);

    })
}
function readTextFile(file, callback) {
    var rawFile = new XMLHttpRequest();
    rawFile.overrideMimeType("application/json");
    rawFile.open("GET", file, true);
    rawFile.onreadystatechange = function () {
        if (rawFile.readyState === 4 && rawFile.status == "200") {
            callback(rawFile.responseText);
        }
    }
    rawFile.send(null);
}

function modeCheck() {
    setTimeout(function () {
        var btns = document.getElementsByClassName("menuBtn");
        for (var elm of btns) {
            if (elm.getAttribute("mode") == curMode) {
                elm.classList.add("curMode");
            } else if (elm.getAttribute("mode")) {
                elm.classList.remove("curMode");
            }

            if (elm.getAttribute("positioning") == curPositioning) {
                elm.classList.add("curMode");
            } else if (elm.getAttribute("positioning")) {
                elm.classList.remove("curMode");
            }
        }
    }, 10)

    setTimeout(function () {
        document.getElementById("sideMenu").classList.remove("active");
        document.getElementsByClassName("hamburger")[0].classList.remove("active");
    }, 250)
}

function setMapCenter(centerPos) {
    //console.log(centerPos)
    map.getView().setCenter(ol.proj.fromLonLat(centerPos));
    /*map.setView(new ol.View({
        zoom: 18,
        center: ol.proj.fromLonLat(centerPos),
        extent: [867641.4152642555, 5523582.8377024885, 868988.6179606778, 5524907.746095569],
    }))*/
    setTimeout(function () {
        refreshPointersPositions();
        refreshLinesPosition();
    }, 25)
}
function setTelCenter() {
    if (navigator.geolocation && curPositioning == "POSTEL") {
        navigator.geolocation.getCurrentPosition(function (pos) {
            var curPos;
            curPos = [parseFloat(pos.coords.longitude.toFixed(8)), parseFloat(pos.coords.latitude.toFixed(8))];
            setMapCenter(curPos);
        });
    }
}

function mapInit() {
    map = null;
    mapDiv = document.getElementById("map");
    mapDiv.style.height = document.getElementById("innerContainer").clientHeight + "px";

    var urlPattern, maxZoom, minZoom, extent;
    if (internetStatus == "ONLINE") {
        urlPattern = "https://mt0.google.com/vt/lyrs=s&hl=en&x={x}&y={y}&z={z}";
        //urlPattern = "http://mt0.google.com/vt/lyrs=s&hl=en&x=68378&y=47469&z=17}";
        //urlPattern = "http://192.168.4.1/vt/x{x}y{y}z{z}.jpg";
        maxZoom = 20;
        //maxZoom = 17;
        //minZoom = 13;
        //extent = [867641.4152642555, 5523582.8377024885, 868988.6179606778, 5524907.746095569];
    } else {
        //urlPattern = "http://192.168.4.1/vt/x{x}y{y}z{z}.jpg";
        urlPattern = "vt/x{x}y{y}z{z}.jpg";
        maxZoom = 17;
        minZoom = 17;
        extent = [867641.4152642555, 5523582.8377024885, 868988.6179606778, 5524907.746095569];
    }

    source = new ol.source.XYZ({
        //url: 'https://api.maptiler.com/tiles/satellite/{z}/{x}/{y}.jpg?key=' + "8Ge6b8WanLd0VDnzkaGE",
        url: urlPattern,
        maxZoom: maxZoom,
        minZoom: minZoom
    })

    map = new ol.Map({
        target: 'map',
        layers: [
            new ol.layer.Tile({
                //source: new ol.source.OSM()
                source: source,
            })
        ],
        view: new ol.View({
            zoom: 17,
            center: ol.proj.fromLonLat(coordinateManeggio),
            extent: extent,
            //extent: [-572513.341856, 5211017.966314, 916327.095083, 6636950.728974],
        })
    });
    try {

    } catch (e) {
        console.log("Error: " + e)
    } finally {

    }

    map.on("movestart", refreshPointersPositions);
    map.on("movestart", refreshLinesPosition);

    map.on('click', function (evt) {
        var coords = ol.proj.transform(evt.coordinate, 'EPSG:3857', 'EPSG:4326');
        var pxCoords = ol.proj.fromLonLat(coords)[0];
        console.log(pointInPolygon(coords[0], coords[1]));
        console.log(ol.proj.fromLonLat(coords))
        addPerimeterPointer(coords);
    });
}

function setPosPointer(p, coords) {
    p.coords = coords;
    var dispCoords = map.getPixelFromCoordinate(ol.proj.fromLonLat(coords));
    p.style.top = dispCoords[1] + "px";
    p.style.left = dispCoords[0] + "px";
    //if(p.id!="cercatore") setMapCenter(coords)
}

function toggleSideMenu() {
    var sideMenu = document.getElementById("sideMenu");
    var btnSender = document.getElementsByClassName("hamburger")[0];

    if (!sideMenu.classList.contains("active")) {
        sideMenu.classList.add("active");
        btnSender.classList.add("active");
    }
    else {
        sideMenu.classList.remove("active");
        btnSender.classList.remove("active");

    }

}

function regPerimeter() {
    if (perimeterClosed)
        clearPerimeter();
    curMode = "REGPER";
}

function setPointerPosition(p, coords, msSpeed = 500) {
    p.coords = coords;
    var dispCoords = map.getPixelFromCoordinate(ol.proj.fromLonLat(coords));
    p.style.transition = "top " + msSpeed + "ms ease-in-out, left " + msSpeed + "ms ease-in-out";
    p.style.top = dispCoords[1] + "px";
    p.style.left = dispCoords[0] + "px";
    if (msSpeed > 0) {
        setTimeout(function () {
            p.style.transition = "";
        }, msSpeed);
    }
}

function refreshPointersPositions() {
    map.on("moveend", stopInterval)

    var repositionInt = setInterval(function () {
        for (p of mapPointers) {
            p.classList.add("noTransition");
            var dispCoords = map.getPixelFromCoordinate(ol.proj.fromLonLat(p.coords));
            p.style.top = dispCoords[1] + "px";
            p.style.left = dispCoords[0] + "px";
            p.classList.remove("noTransition");
        }
    }, 10);

    function stopInterval() {
        clearInterval(repositionInt);
    }
}

function addCollare(coords, id) {
    var collare = document.createElement("div");
    var dispCoords = map.getPixelFromCoordinate(ol.proj.fromLonLat(coords));
    collare.classList.add("collarePoint");
    collare.style.top = dispCoords[1] + "px";
    collare.style.left = dispCoords[0] + "px";
    collare.style.background = "#" + ((1 << 24) * Math.random() | 0).toString(16);
    collare.coords = coords;
    collare.id = id;
    mapPointers.push(collare);
    mapDiv.appendChild(collare);
}
function addCercatore(coords) {
    var p = document.createElement("div");
    var dispCoords = map.getPixelFromCoordinate(ol.proj.fromLonLat(coords));
    p.classList.add("cercatorePoint");
    p.style.top = dispCoords[1] + "px";
    p.style.left = dispCoords[0] + "px";
    p.style.background = "#" + ((1 << 24) * Math.random() | 0).toString(16);
    p.coords = coords;
    p.id = "cercatore";
    mapPointers.push(p);
    mapDiv.appendChild(p);
    cercatore = p;
}

function clearPerimeter() {
    for (var elm of perimeterPointers) {
        elm.parentNode.removeChild(elm);
    }
    perimeterPointers = new Array;
    document.getElementsByTagName("svg")[0].innerHTML = "";
    mapLines = new Array;
    perimeterClosed = false;
}

function addPerimeterPointer(coords, override = false, lastCoord = false) {
    if (curMode == "REGPER" || override) {
        var p = document.createElement("div");
        //var line = document.createElement('line');
        var svgContainer = document.getElementsByTagName("svg")[0];
        var dispCoords = map.getPixelFromCoordinate(ol.proj.fromLonLat(coords));
        //console.log(dispCoords);
        p.classList.add("perimeterPointer");
        p.style.top = dispCoords[1] + "px";
        p.style.left = dispCoords[0] + "px";
        p.coords = coords;
        if (perimeterPointers.length > 0) {
            /*line.setAttribute("x1",parseInt(perimeterPointers[perimeterPointers.length-1].style.left.replace("px","")));
            line.setAttribute("y1",parseInt(perimeterPointers[perimeterPointers.length-1].style.top.replace("px","")));
            line.setAttribute("x2",parseInt(dispCoords[0]));
            line.setAttribute("y2",parseInt(dispCoords[1]));

            svgContainer.appendChild(line);
            console.log(line)*/
            var x1 = parseInt(perimeterPointers[perimeterPointers.length - 1].style.left.replace("px", ""));
            var y1 = parseInt(perimeterPointers[perimeterPointers.length - 1].style.top.replace("px", ""));
            var x2 = parseInt(dispCoords[0]);
            var y2 = parseInt(dispCoords[1]);
            var lines = document.getElementsByTagName("line");

            var htmlLine = "<line id=\"line-" + lines.length + "\" x1=\"" + x1 + "\" y1=\"" + y1 + "\" x2=\"" + x2 + "\" y2=\"" + y2 + "\" ></line>";

            svgContainer.innerHTML += htmlLine;
            mapLines.push([map.getCoordinateFromPixel([x1, y1]), map.getCoordinateFromPixel([x2, y2])]);

            if (perimeterPointers.length == 2) {
                perimeterPointers[0].addEventListener("click", connectLastPoint);
                perimeterPointers[0].style.cursor = "pointer";
                perimeterPointers[0].style.transform = "translate(-50%,-50%) scale(2)";
            }
        } else {
        }

        mapPointers.push(p);
        perimeterPointers.push(p);
        mapDiv.appendChild(p);

        if (lastCoord)
            connectLastPoint();

        function connectLastPoint() {
            var lines = document.getElementsByTagName("line");
            var x1 = parseInt(perimeterPointers[perimeterPointers.length - 1].style.left.replace("px", ""));
            var y1 = parseInt(perimeterPointers[perimeterPointers.length - 1].style.top.replace("px", ""));
            var x2 = parseInt(perimeterPointers[0].style.left.replace("px", ""));
            var y2 = parseInt(perimeterPointers[0].style.top.replace("px", ""));
            var htmlLine = "<line id=\"line-" + lines.length + "\" x1=\"" + x1 + "\" y1=\"" + y1 + "\" x2=\"" + x2 + "\" y2=\"" + y2 + "\" ></line>";
            svgContainer.innerHTML += htmlLine;
            mapLines.push([map.getCoordinateFromPixel([x1, y1]), map.getCoordinateFromPixel([x2, y2])]);
            perimeterPointers[0].style.cursor = "";
            perimeterPointers[0].style.transform = "translate(-50%,-50%)";
            perimeterPointers[0].removeEventListener("click", connectLastPoint);
            perimeterClosed = true;

            if (!override) {
                var perimeterCoords = new Array;
                for (var point of perimeterPointers) {
                    var pCoords = new Object;
                    pCoords.lng = parseFloat(point.coords[0].toFixed(5));
                    pCoords.lat = parseFloat(point.coords[1].toFixed(5));
                    perimeterCoords.push(pCoords);
                }
                if (socket.readyState == socket.OPEN) {
                    socket.send("pCoords:" + JSON.stringify(perimeterCoords));
                    console.log("Sent perimeter coords: " + JSON.stringify(perimeterCoords));
                }
            }


            viewMap();
            modeCheck();
        }
    }
}

function refreshLinesPosition() {
    map.on("moveend", stopInterval)
    var lines = document.getElementsByTagName("line");
    //console.log(lines);

    var refreshInterval = setInterval(function () {
        for (var line of lines) {
            //console.log(line.precCoords)
            var newXY1 = map.getPixelFromCoordinate(mapLines[parseInt(line.id.split('-')[1])][0]);
            var newXY2 = map.getPixelFromCoordinate(mapLines[parseInt(line.id.split('-')[1])][1]);

            line.setAttribute("x1", newXY1[0]);
            line.setAttribute("y1", newXY1[1]);
            line.setAttribute("x2", newXY2[0]);
            line.setAttribute("y2", newXY2[1]);
        }
    }, 10)

    function stopInterval() {
        clearInterval(refreshInterval);
    }
}

function getRandomColor() {
    var letters = '0123456789ABCDEF';
    var color = '#';
    for (var i = 0; i < 6; i++) {
        color += letters[Math.floor(Math.random() * 16)];
    }
    return color;
}

window.mobileCheck = function () {
    let check = false;
    (function (a) { if (/(android|bb\d+|meego).+mobile|avantgo|bada\/|blackberry|blazer|compal|elaine|fennec|hiptop|iemobile|ip(hone|od)|iris|kindle|lge |maemo|midp|mmp|mobile.+firefox|netfront|opera m(ob|in)i|palm( os)?|phone|p(ixi|re)\/|plucker|pocket|psp|series(4|6)0|symbian|treo|up\.(browser|link)|vodafone|wap|windows ce|xda|xiino/i.test(a) || /1207|6310|6590|3gso|4thp|50[1-6]i|770s|802s|a wa|abac|ac(er|oo|s\-)|ai(ko|rn)|al(av|ca|co)|amoi|an(ex|ny|yw)|aptu|ar(ch|go)|as(te|us)|attw|au(di|\-m|r |s )|avan|be(ck|ll|nq)|bi(lb|rd)|bl(ac|az)|br(e|v)w|bumb|bw\-(n|u)|c55\/|capi|ccwa|cdm\-|cell|chtm|cldc|cmd\-|co(mp|nd)|craw|da(it|ll|ng)|dbte|dc\-s|devi|dica|dmob|do(c|p)o|ds(12|\-d)|el(49|ai)|em(l2|ul)|er(ic|k0)|esl8|ez([4-7]0|os|wa|ze)|fetc|fly(\-|_)|g1 u|g560|gene|gf\-5|g\-mo|go(\.w|od)|gr(ad|un)|haie|hcit|hd\-(m|p|t)|hei\-|hi(pt|ta)|hp( i|ip)|hs\-c|ht(c(\-| |_|a|g|p|s|t)|tp)|hu(aw|tc)|i\-(20|go|ma)|i230|iac( |\-|\/)|ibro|idea|ig01|ikom|im1k|inno|ipaq|iris|ja(t|v)a|jbro|jemu|jigs|kddi|keji|kgt( |\/)|klon|kpt |kwc\-|kyo(c|k)|le(no|xi)|lg( g|\/(k|l|u)|50|54|\-[a-w])|libw|lynx|m1\-w|m3ga|m50\/|ma(te|ui|xo)|mc(01|21|ca)|m\-cr|me(rc|ri)|mi(o8|oa|ts)|mmef|mo(01|02|bi|de|do|t(\-| |o|v)|zz)|mt(50|p1|v )|mwbp|mywa|n10[0-2]|n20[2-3]|n30(0|2)|n50(0|2|5)|n7(0(0|1)|10)|ne((c|m)\-|on|tf|wf|wg|wt)|nok(6|i)|nzph|o2im|op(ti|wv)|oran|owg1|p800|pan(a|d|t)|pdxg|pg(13|\-([1-8]|c))|phil|pire|pl(ay|uc)|pn\-2|po(ck|rt|se)|prox|psio|pt\-g|qa\-a|qc(07|12|21|32|60|\-[2-7]|i\-)|qtek|r380|r600|raks|rim9|ro(ve|zo)|s55\/|sa(ge|ma|mm|ms|ny|va)|sc(01|h\-|oo|p\-)|sdk\/|se(c(\-|0|1)|47|mc|nd|ri)|sgh\-|shar|sie(\-|m)|sk\-0|sl(45|id)|sm(al|ar|b3|it|t5)|so(ft|ny)|sp(01|h\-|v\-|v )|sy(01|mb)|t2(18|50)|t6(00|10|18)|ta(gt|lk)|tcl\-|tdg\-|tel(i|m)|tim\-|t\-mo|to(pl|sh)|ts(70|m\-|m3|m5)|tx\-9|up(\.b|g1|si)|utst|v400|v750|veri|vi(rg|te)|vk(40|5[0-3]|\-v)|vm40|voda|vulc|vx(52|53|60|61|70|80|81|83|85|98)|w3c(\-| )|webc|whit|wi(g |nc|nw)|wmlb|wonu|x700|yas\-|your|zeto|zte\-/i.test(a.substr(0, 4))) check = true; })(navigator.userAgent || navigator.vendor || window.opera);
    return check;
};
