document.addEventListener("DOMContentLoaded", function(e) {

	var starttime = document.getElementById('starttime');
	starttime.addEventListener('focusout',
		function(e) {
			ajaxPost('/lightRelay', {
				"startTime": this.valueAsNumber
			},
			function(data) {
				console.log(data);
			});
		}
	);

	var duration = document.getElementById('duration');
	duration.addEventListener('focusout',
		function(e) {
			ajaxPost('/lightRelay', {
				"duration": this.valueAsNumber
			},
			function(data) {
				console.log(data);
			});
		}
	);

	function pumpInterval(event) {
		console.log("pump interval value: "+this.value);
		if(event.type) {
			ajaxPost('/pumpRelay', {
				"interval": this.value
			}, function(data) {
				console.log(data);
			});
		}
	}
	function pumpDuration(event) {
		console.log("pump duration value: "+this.value);
		if(event.type) {
			ajaxPost('/pumpRelay', {
				"duration": this.value
			}, function(data) {
				console.log(data);
			});
		}
	}
	document.querySelectorAll("input[name='interval']").forEach((input) => {
		if(input.value == externalPumpInterval) {
			input.checked = true;
		}
		input.addEventListener('change', pumpInterval);
	});
	document.querySelectorAll("input[name='duration']").forEach((input) => {
		if(input.value == externalPumpDuration) {
			input.checked = true;
		}
		input.addEventListener('change', pumpDuration);
	});

	function ajaxCall(url, callback) {
		var ajax;
		ajax = new XMLHttpRequest();
		ajax.onreadystatechange = function() {
			if(ajax.readyState == 4 && ajax.status == 200) {
				callback(ajax.responseText);
      			}
  			};
		ajax.open("GET", url, true);
		ajax.send();
	}

	function ajaxPost(url, data, success) {
		var params = typeof data == 'string'
		? data
		: Object.keys(data).map(
			function(k) {
				return(encodeURIComponent(k) + '=' + encodeURIComponent(data[k]))
			}
		).join('&');

		var xhr = window.XMLHttpRequest
			? new XMLHttpRequest()
			: new ActiveXObject("Microsoft.XMLHTTP");

		xhr.open('POST', url);
		xhr.onreadystatechange = function() {
			if (xhr.readyState>3 && xhr.status==200) {
				success(xhr.responseText);
			}
		};
		xhr.setRequestHeader('X-Requested-With', 'XMLHttpRequest');
		xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
		xhr.send(params);
		return(xhr);
	}
		function onMessage(event) {
			if(event.type == "message") {
				json = JSON.parse(event.data);
				document.getElementById('timedate').innerHTML = json.timedate;
				document.getElementById('signal').innerHTML = json.signal;
				document.getElementById('pumpRelay').innerHTML = json.pumpstate ? "<u>ON</u>" : "OFF";
				document.getElementById('lightRelay').innerHTML = json.lightstate ? "<u>ON</u>" : "OFF";
				line1.append(new Date().getTime(), json.temperature);
				document.getElementById('temperature').innerHTML = json.temperature;
				line2.append(new Date().getTime(), json.humidity);
				document.getElementById('humidity').innerHTML = json.humidity;
						// console.log(event.data);
			}
		}

		var smoothie = new SmoothieChart({
		    millisPerPixel: 2000,
			grid: {fillStyle: '#222', strokeStyle: '#444',millisPerLine:80000,verticalSections:5},
		});
		var smoothie2 = new SmoothieChart({
			millisPerPixel: 2000,
			grid: {fillStyle: '#222', strokeStyle: '#444',millisPerLine:80000,verticalSections:5},
		});
		smoothie.streamTo(document.getElementById("mycanvas"));
		smoothie2.streamTo(document.getElementById("mycanvas2"));
		var line1 = new TimeSeries();
		var line2 = new TimeSeries();
		smoothie.addTimeSeries(line1, { strokeStyle:'rgb(0, 255, 0)' , lineWidth:3});
		smoothie2.addTimeSeries(line2, { strokeStyle:'rgb(255, 0, 255)' , lineWidth:3});

		/*
		var line1 = new TimeSeries();
		var line2 = new TimeSeries();
		setInterval(function() {
			ajaxCall("/combined", (vals) => {
				json = JSON.parse(vals);
				document.getElementById('timedate').innerHTML = json.timedate;
				document.getElementById('signal').innerHTML = json.signal;
				document.getElementById('pumpRelay').innerHTML = json.pumpstate ? "<u>ON</u>" : "OFF";
				document.getElementById('lightRelay').innerHTML = json.lightstate ? "<u>ON</u>" : "OFF";
				line1.append(new Date().getTime(), json.temperature);
				document.getElementById('temperature').innerHTML = json.temperature;
				line2.append(new Date().getTime(), json.humidity);
				document.getElementById('humidity').innerHTML = json.humidity;
			})
		}, 2000);

		smoothie.addTimeSeries(line1, { strokeStyle:'rgb(0, 255, 0)' , lineWidth:3});
		smoothie2.addTimeSeries(line2, { strokeStyle:'rgb(255, 0, 255)' , lineWidth:3});
		*/
	
	var gateway = "ws://" + window.location.hostname + "/ws";
	var websocket;
	function initWebSocket() {
		console.log('Trying to open a WebSocket connection...');
		websocket = new WebSocket(gateway);
		websocket.onopen = onOpen;
		websocket.onclose = onClose;
		websocket.onmessage = onMessage; // <-- add this line
	}

	function onOpen(event) {
		console.log('Connection opened');
	}

	function onClose(event) {
		console.log('Connection closed');
		setTimeout(initWebSocket, 2000);
	}
	
	window.addEventListener('load', onLoad);
	function onLoad(event) {
		initWebSocket();
		initButton();
	}

	function initButton() {
		document.getElementById('trigger').addEventListener('click', toggle);
	}

	function toggle(){
		websocket.send('toggle');
	}

});