document.addEventListener("DOMContentLoaded", function(e) {

	var liter = 1;
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
	smoothie.addTimeSeries(line1, { strokeStyle:'rgb(0, 255, 0)' , lineWidth:1});
	smoothie2.addTimeSeries(line2, { strokeStyle:'rgb(255, 0, 255)' , lineWidth:1});

	
	var gateway = "ws://" + window.location.hostname + "/ws";
	var websocket;

	function nutritionScheme() {
		return(`
		<br>
		<br>
		Nutrition scheme for <input type="text" value="${liter}" size="3" id="liter"> liter:
		<br>
		<table id="table">
		<tr>
			<th>Description</th>
			<th>Weeks</span>
			<th>Light<br>hours</th>
			<th>Aqua<br>Vega<br>A & B</th>
			<th>Aqua<br>Flores<br>A & B</th>
			<th>Rhizo<br>tonic</th>
			<th>Cana<br>zym</th>
			<th>PK13/14</th>
			<th>Canna<br>boost</th>
			<th>EC+</span>
			<th>EC Total</th>
		</tr>

		<tr class="schemerow" id="1">
			<td id='desc'>Start / rooting (3-5 days)<br>Wetten the substrate</td>
			<td>~1</td>
			<td>18</td>
			<td>1.8</td>
			<td>-</td>
			<td>4</td>
			<td>-</td>
			<td>-</td>
			<td>-</td>
			<td>0.9</td>
			<td>1.3</td>
		</tr>

		<tr class="schemerow" id="2">
			<td id='desc'>Vegetative Phase I<br>Plant developse in Volume</td>
			<td>1-3</td>
			<td>18</td>
			<td>2.2</td>
			<td>-</td>
			<td>2</td>
			<td>2.5</td>
			<td>-</td>
			<td>-</td>
			<td>1.1</td>
			<td>1.5</td>
		</tr>

		<tr class="schemerow" id="3">
			<td id='desc'>Vegetative phase II<br>Up to growth stagnation<br>start of fructification<br>or blooming</td>
			<td>2-4</td>
			<td>12</td>
			<td>2.8</td>
			<td>-</td>
			<td>2</td>
			<td>2.5</td>
			<td>-</td>
			<td>3</td>
			<td>1.4</td>
			<td>1.8</td>
		</tr>

		<tr><td><br></td></tr>

		<tr class="schemerow" id="4">
			<td id='desc'>Generative period I<br>Flowers or fuits develope in<br>size. Growth in height achieved</td>
			<td>2-3</td>
			<td>12</td>
			<td>-</td>
			<td>3.4</td>
			<td>0.5</td>
			<td>2.5</td>
			<td>-</td>
			<td>3</td>
			<td>1.6</td>
			<td>2</td>
		</tr>

		<tr class="schemerow" id="5">
			<td id='desc'>Generative period II<br>Develope mass (weight) of<br>flowers or fruits</td>
			<td>1</td>
			<td>12</td>
			<td>-</td>
			<td>3.4</td>
			<td>0.5</td>
			<td>2.5</td>
			<td>1.5</td>
			<td>3</td>
			<td>1.8</td>
			<td>2.2</td>
		</tr>

		<tr class="schemerow" id="6">
			<td id='desc'>Generative period III<br>Develope mass (weight) of<br>flowers or fruits</td>
			<td>2-3</td>
			<td>12</td>
			<td>-</td>
			<td>2.5</td>
			<td>0.5</td>
			<td>2.5</td>
			<td>-</td>
			<td>3</td>
			<td>1.2</td>
			<td>1.6</td>
		</tr>

		<tr class="schemerow" id="7">
			<td id='desc'>Generative period IV<br>Flower or fruit ripening process</td>
			<td>1-2</td>
			<td>10</td>
			<td>-</td>
			<td>-</td>
			<td>-</td>
			<td>3.5</td>
			<td>-</td>
			<td>3</td>
			<td>-</td>
			<td>0.4</td>
		</tr>


		</table>
		`);
	}

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

	function literChanged(event) {
		console.log("changed liter value: "+this.value);
		liter = this.value;
		websocket.send(
			JSON.stringify({
				type: "literChanged",
				liter: parseFloat(liter).toFixed(1)
			}));
	}

	function resetCycle(event) {
		console.log("reset cycle");
		websocket.send(
			JSON.stringify({
				type: "resetCycle",
			}));
	}
	
	function cycleAsText(totalDays) {
	    weeks = parseInt(totalDays / 7);
	    days = weeks - (parseInt(weeks / 7) * 7);
		var reset = '&#10226';
		if(!weeks) {
			return(`${totalDays} days <span id="resetCycle">${reset}</span>${nutritionScheme()}`);
		} else {
			if(!days) {
				return(`${totalDays} days or ${weeks} weeks <span id="resetCycle">${reset}</span>${nutritionScheme}()`);
			} else {
				return(`${totalDays} days or ${weeks} weeks and ${days} days <span id="resetCycle">${reset}</span>${nutritionScheme()}`);
			}
		}
	}

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

			if(json.type == "update") {

				document.getElementById('timedate').innerHTML = json.timedate;
				document.getElementById('wifiSignal').innerHTML = json.wifiSignal;
				document.getElementById('pumpRelay').innerHTML = json.pumpstate ? "<u>ON</u>" : "OFF";
				document.getElementById('lightRelay').innerHTML = json.lightstate ? "<u>ON</u>" : "OFF";
				line1.append(new Date().getTime(), json.temperature);
				document.getElementById('temperature').innerHTML = json.temperature.toFixed(2);
				line2.append(new Date().getTime(), json.humidity);
				document.getElementById('humidity').innerHTML = json.humidity.toFixed(2);

			} else if(json.type == "updateScheme") {

				liter = json.liter;
				document.getElementById('cyclelen').innerHTML = cycleAsText(json.cycleLength);
				document.querySelectorAll("tr[class='schemerow']").forEach((tr) => {
					for(var i=3; i <= 8; i++) {
						if(tr.cells[i].innerHTML != '-') {
							var val = tr.cells[i].innerHTML * liter;
							tr.cells[i].innerHTML = val.toFixed(1);
						}

						var createClickHandler = function(clickedRow) {
							return function() {
								websocket.send(
									JSON.stringify({
										type: "schemeStep",
										schemeStep: parseInt(clickedRow.id)
									}));
							};
						};
						tr.onclick = createClickHandler(tr);
					}
					if(tr.id == json.schemeStep) {
						tr.style.background = '#333';
					} else {
						tr.style.background = 'none';
					}
				});
				document.querySelectorAll("input[id='liter']").forEach((input) => {
					input.addEventListener('change', literChanged);
				});
				document.getElementById('resetCycle').onclick = resetCycle;
				/*
				document.querySelectorAll("span[id='resetCycle']").forEach((span) => {
					span.addEventListener('click', resetCycle);
				});
				*/
				
			} else if(json.type == "updatePump") {

				document.querySelectorAll("input[name='interval']").forEach((input) => {
					if(input.value == json.pumpinterval) {
						input.checked = true;
					}
				});
				document.querySelectorAll("input[name='duration']").forEach((input) => {
					if(input.value == json.pumpduration) {
						input.checked = true;
					}
				});

			} else if(json.type == "updateLight") {

				document.getElementById('starttime').value = json.lightstart;
				document.getElementById('duration').value = json.lightduration;

			} else {
				console.log(event.data);
			}
		}
	}

	function initWebSocket() {
		console.log('Trying to open a WebSocket connection...');
		websocket = new WebSocket(gateway);
		websocket.onopen = onOpen;
		websocket.onclose = onClose;
		websocket.onmessage = onMessage;
	}

	function onOpen(event) {
		console.log('...connected.');
	}

	function onClose(event) {
		console.log('Connection closed');
		setTimeout(initWebSocket, 2000);
	}
	
	window.addEventListener('load', onLoad);
	function onLoad(event) {
		initWebSocket();
		toggleButton();
		resetButton();
	}

	function toggleButton() {
		document.getElementById('toggle').addEventListener('click', toggle);
	}
	function toggle(){
		websocket.send(JSON.stringify({type: "toggle"}));
	}

	function resetButton() {
		document.getElementById('reset').addEventListener('click', reset);
	}
	function reset(){
		websocket.send('reset');
	}

});

function listAllEventListeners() {
  const allElements = Array.prototype.slice.call(document.querySelectorAll('*'));
  allElements.push(document);
  allElements.push(window);

  const types = [];

  for (let ev in window) {
    if (/^on/.test(ev)) types[types.length] = ev;
  }

  let elements = [];
  for (let i = 0; i < allElements.length; i++) {
    const currentElement = allElements[i];
    for (let j = 0; j < types.length; j++) {
      if (typeof currentElement[types[j]] === 'function') {
        elements.push({
          "node": currentElement,
          "type": types[j],
          "func": currentElement[types[j]].toString(),
        });
      }
    }
  }

  return elements.sort(function(a,b) {
    return a.type.localeCompare(b.type);
  });
}
