/* Runs the actual page script against a simulated browser, network and gamepad. */
let gamepadTime=0;
const performance={now:()=>gamepadTime};
const preferences={};
const localStorage={getItem:key=>preferences[key]??null,setItem:(key,value)=>{preferences[key]=value}};
const intervals = [];
const events = {};
const elements = {};
const moves = [];
const presetRequests = [];
const pairingRequests = [];
const zoomRequests = [];
let serverZoom={present:false,calibrated:false,available:false,position:null,moving:false,target:0,error:""};
let serverZoomPresets=[null,null,null,null,null];
let serverPresets=[[0,0,0],null,null,null,null];
let deferNextMove=false,releaseMove=null;
const commandOrder=[];
let serverSession = 42;
let serverReady = true;
let serverVerified = true;
let serverControls = true;
let serverPose = null;
let serverBattery = null;
let serverSleeping = null;
const sleepRequests = [];
let online = true;
let statusRequests = 0;
let gamepads = [];
function element(name) {
  return elements[name] || (elements[name] = {
    checked: false, value: '', textContent: '', style: {},
    attributes: {}, setAttribute(name,value) {this.attributes[name]=value;},
    children: [], replaceChildren() {this.children=[];}, append(...v) {this.children.push(...v);}, setPointerCapture() {},
    addEventListener(name, cb) {this['on'+name]=cb;},
    showModal() {this.open=true;}, close() {this.open=false;if(this.onclose)this.onclose();},
    getBoundingClientRect() { return {left: 0, top: 0, width: 240, height: 240}; }
  });
}
element('#zoomSpeed').value='20'; element('#presetZoomSpeed').value='20'; element('#zoomPosition').value='0';
element('#gamepadSpeed').value='100'; element('#joySpeed').value='100'; element('#presetDuration').value='20';
const document = {hidden: false, querySelector: element, createElement: () => ({children:[],setAttribute() {},append(...v) {this.children.push(...v);}}), addEventListener: (name, cb) => {events['document.'+name] = cb;}};
const window = {addEventListener: (name, cb) => {events[name] = cb;}};
const navigator = {getGamepads: () => gamepads};
const setInterval = (cb, ms) => { intervals.push({cb, ms}); return intervals.length; };
const setTimeout = () => 1;
const clearTimeout = () => {};
class AbortController { constructor() { this.signal = {}; } abort() {} }
function fetch(url, options) {
  if(!online) return Promise.reject(new Error('offline'));
  if(['/discover','/resume','/disconnect','/select'].includes(url)) {pairingRequests.push({url,body:options.body});return Promise.resolve({ok:true});}
  if(url === '/devices') return Promise.resolve({ok:true, json:async()=>({devices:[],targetName:'',targetId:'',connected:serverReady,ready:serverReady,protocolVerified:serverVerified,controlEnabled:serverControls})});
  if(url === '/zoom/presets') return Promise.resolve({ok:true, json:async()=>serverZoomPresets});
  if(url === '/zoom'||url.startsWith('/zoom/')) {zoomRequests.push({url,body:options.body,session:Number(options.headers['X-Ronin-Session'])});return Promise.resolve({ok:serverReady&&serverControls&&serverZoom.available});}
  if(url === '/presets') return Promise.resolve({ok:true, json:async()=>serverPresets});
  if(url.startsWith('/preset/')) {commandOrder.push('preset:'+options.body); presetRequests.push({url,body:options.body}); return Promise.resolve({ok:true}); }
  if(url === '/status') statusRequests++;
  if(url === '/status') return Promise.resolve({ok:true, json:async()=>({ble:serverReady,protocolVerified:serverVerified,controlEnabled:serverControls,session:serverSession,battery:serverBattery,pose:serverPose,zoom:serverZoom,sleeping:serverSleeping})});
  if(url === '/led') return Promise.resolve({ok:true, json:async()=>({brightness:8})});
  if(url === '/sleep') {sleepRequests.push({body:options.body,session:Number(options.headers['X-Ronin-Session'])});return Promise.resolve({ok:true});}
  if(url === '/move') {
    let session = Number(options.headers['X-Ronin-Session']);
    let accepted = serverReady && serverControls && session === serverSession;
    moves.push({body:options.body,session,accepted});commandOrder.push('move:'+options.body);
    if(deferNextMove){deferNextMove=false;return new Promise(resolve=>{releaseMove=()=>resolve({ok:accepted})})}
    return Promise.resolve({ok:accepted});
  }
  return Promise.reject(new Error('unexpected request'));
}
function flush() { for(let i=0;i<10;i++) drainMicrotasks(); }
function tick(ms) { gamepadTime+=ms; for(const item of intervals) if(item.ms === ms) item.cb(); flush(); }
function check(value, message) { if(!value) throw new Error(message); }
function simulatedPad(x,y,held,buttons=[]) {return {index:0,id:'PS5',mapping:'standard',connected:true,axes:[x,y],buttons:Array.from({length:16},(_,i)=>({pressed:(i===6&&held)||buttons.includes(i),value:(i===6&&held)||buttons.includes(i)?1:0}))};}
load('main/gamepad.js');
const pageScript = readFile('main/index.html').split('<script>')[1].split('</script>')[0];
eval(pageScript);
flush();
check(element('#status').textContent.includes('BLE connecté'), 'initial link');
check(element('#sleepButton').disabled && element('#wakeButton').disabled, 'unknown sleep state disables both controls');
serverSleeping=false;tick(1000);
check(!element('#sleepButton').disabled && element('#wakeButton').disabled, 'awake state allows sleep');
element('#sleepButton').onclick();flush();
check(sleepRequests.some(r=>r.body==='1'&&r.session===serverSession), 'sleep request carries current session');
serverSleeping=true;tick(1000);
check(element('#sleepButton').disabled && !element('#wakeButton').disabled, 'sleep state allows wake');
element('#wakeButton').onclick();flush();
check(sleepRequests.some(r=>r.body==='0'&&r.session===serverSession), 'wake request carries current session');
serverSleeping=null;tick(1000);
element('#openPairing').onclick(); flush();
check(pairingRequests.some(r=>r.url==='/discover'), 'opening dialog starts discovery');
element('#closePairing').onclick(); flush();
check(pairingRequests.some(r=>r.url==='/resume'), 'closing dialog resumes saved target');
element('#devices').value='00:17:0F:23:B9:1C:48'; element('#selectDevice').onclick(); flush();
check(pairingRequests.some(r=>r.url==='/select'&&r.body==='00:17:0F:23:B9:1C:48'), 'selection uses address identity');
check(!pageScript.includes('#gamepadMode'), 'no gamepad activation toggle');
tick(100);check(element('#gamepadStatus').className==='', 'undetected gamepad icon dimmed');
gamepads=[simulatedPad(0,0,false)]; tick(100);
check(element('#gamepadStatus').className==='connected', 'connected gamepad detected automatically');
check(element('#gamepadStatus').attributes['aria-label']==='Manette détectée', 'detection icon is accessible');
gamepads=[simulatedPad(1,0,true)]; tick(100);
check(moves.some(m => m.body==='300,0' && m.accepted), 'controller motion delivered');
element('#gamepadSpeed').value='200'; tick(100);
check(moves[moves.length-1].body==='600,0', '200 percent gamepad motion');
element('#joySpeed').value='10'; tick(100);
check(moves[moves.length-1].body==='600,0', 'virtual speed does not change gamepad speed');
element('#gamepadSpeed').value='100'; element('#joySpeed').value='100';
gamepads=[simulatedPad(1,0,false)]; tick(100);
check(moves[moves.length-1].body==='0,0', 'trigger release sends stop');
gamepads=[simulatedPad(0,0,false)]; tick(100);
gamepads=[simulatedPad(1,0,true)]; tick(100);
serverReady=false; serverSession=43; tick(1000);
let count=moves.length; tick(100); check(moves.length===count, 'no commands while BLE down');
serverReady=true; serverSession=44; tick(1000); tick(100);
check(moves.length===count, 'held trigger cannot restart after reconnect');
gamepads=[simulatedPad(0,0,false)]; tick(100);
gamepads=[simulatedPad(1,0,true)]; tick(100);
check(moves[moves.length-1].session===44 && moves[moves.length-1].body==='300,0', 'rearm uses current session');
events.blur(); flush(); check(moves[moves.length-1].body==='0,0', 'focus loss stops');
events.focus(); flush();
element('#pad').onpointerdown({pointerId:1,clientX:208,clientY:120}); flush();
check(moves[moves.length-1].body==='300,0', 'virtual joystick still works');
element('#pad').onpointerup(); flush(); check(moves[moves.length-1].body==='0,0', 'virtual release stops');
element('#joySpeed').value='200'; element('#pad').onpointerdown({pointerId:1,clientX:208,clientY:120}); flush();
check(moves[moves.length-1].body==='600,0', '200 percent virtual motion');
element('#pad').onpointerup(); flush();
element('#presetDuration').value='5';
element('#presets').children[0].children[0].onclick(); flush();
check(presetRequests.some(p=>p.url==='/preset/goto' && p.body==='0,05,020'), 'half second encoded with leading zero');

serverVerified=false; serverControls=false; serverPose=[1771,0,251]; serverBattery=32; tick(1000);
check(element('#pose').textContent.includes('177.1')&&element('#pose').textContent.includes('25.1'), 'read-only angles shown with motion blocked');
check(element('#battery').textContent.includes('32'), 'read-only battery shown with motion blocked');
check(element('#status').textContent.includes('indisponibles'), 'unsupported model remains blocked');
let blockedCount=moves.length; element('#pad').onpointerdown({pointerId:1,clientX:208,clientY:120}); tick(100);
check(moves.length===blockedCount, 'unverified model cannot move');
serverControls=true; tick(1000);
check(element('#status').textContent==='BLE connecté', 'RS4 Pro has a neutral connected label');
element('#pad').onpointerdown({pointerId:1,clientX:208,clientY:120}); flush();
check(moves[moves.length-1].accepted&&moves[moves.length-1].body==='600,0', 'RS4 Pro joystick allowed with unverified protocol');
element('#pad').onpointerup(); flush();
let oldPresetRequests=presetRequests.length;
element('#presets').children[0].children[0].onclick(); flush();
check(presetRequests.length===oldPresetRequests+1, 'RS4 Pro preset available for user trial');
online=false; tick(1000); check(element('#status').textContent.includes('inaccessible'), 'network failure displayed');
online=true; tick(1000);
gamepads=[simulatedPad(0,0,false)]; tick(100);
gamepads=[simulatedPad(1,0,true)]; tick(100);
check(moves[moves.length-1].body==='300,0', 'automatic gamepad resumes after neutral rearm');
gamepads=[]; events.gamepaddisconnected(); flush(); tick(100);
check(moves[moves.length-1].body==='0,0', 'gamepad disconnect stops motion');
check(element('#gamepadStatus').className==='', 'disconnected gamepad icon dimmed');
let afterDisconnect=moves.length;
gamepads=[simulatedPad(1,0,true)]; tick(100);
check(moves.length===afterDisconnect, 'held trigger cannot move on automatic detection');
gamepads=[simulatedPad(0,0,false)]; tick(100);
gamepads=[simulatedPad(1,0,true)]; tick(100);
element('#pad').onpointerdown({pointerId:2,clientX:120,clientY:32}); flush(); tick(100);
check(moves[moves.length-1].body==='0,600', 'virtual input takes priority over automatic gamepad');
element('#pad').onpointerup(); flush();
let afterVirtual=moves.length; tick(100);
check(moves.length===afterVirtual, 'held trigger cannot take over after virtual release');

check(element('#zoomControls').disabled, 'missing motor disables only zoom controls');
let noMotorCount=zoomRequests.length;
element('#zoomIn').onclick(); flush();
check(zoomRequests.length===noMotorCount, 'missing motor cannot receive zoom target');
serverZoom={present:true,calibrated:true,available:true,position:2048,moving:false,target:2048,error:''};
tick(1000);
check(!element('#zoomControls').disabled&&element('#zoomStatus').textContent.includes('50.0'), 'actual calibrated position displayed');
element('#zoomSpeed').value='7'; element('#zoomPosition').oninput({target:{value:'2200'}}); flush();
check(zoomRequests[zoomRequests.length-1].body==='2200,7', 'manual zoom speed sent independently');
tick(500);check(zoomRequests[zoomRequests.length-1].url==='/zoom/keepalive', 'active zoom has a browser lease');
serverZoomPresets=[2048,null,null,null,null];
element('#selectDevice').onclick(); flush();
// Refresh the page data after storing optional zoom metadata.
let presetMessageCount=presetRequests.length;
element('#presets').children[0].children[1].onclick(); flush();
check(presetRequests.length===presetMessageCount+1, 'save remains available with zoom');
check(element('#presets').children[0].children[0].textContent.includes('50.0'), 'preset shows its stored zoom');
element('#presetZoomSpeed').value='13';element('#presets').children[0].children[0].onclick();flush();
check(presetRequests[presetRequests.length-1].body==='0,05,013', 'preset zoom speed independent of manual zoom');
element('#stop').onclick();flush();
check(moves[moves.length-1].body==='0,0'&&zoomRequests[zoomRequests.length-1].url==='/zoom/stop', 'global stop cancels both controls');
element('#zoomPosition').oninput({target:{value:'2000'}});flush();events.blur();flush();
check(zoomRequests[zoomRequests.length-1].url==='/zoom/stop', 'focus loss stops zoom');
events.focus();serverZoom={present:true,calibrated:false,available:false,position:null,moving:false,error:''};tick(1000);
check(element('#zoomControls').disabled&&element('#zoomStatus').textContent.includes('calibrer'), 'uncalibrated motor disabled');
let axesOnlyCount=presetRequests.length;
element('#presets').children[0].children[0].onclick();flush();
check(presetRequests.length===axesOnlyCount+1, 'missing optional zoom does not block a preset');


serverZoom={present:true,calibrated:true,available:true,position:2048,moving:false,target:2048,error:''};
serverPresets=[[0,0,0],[10,20,30],[20,30,40],[30,40,50],null];
element('#presets').children[0].children[1].onclick();flush();tick(1000);
const symbols=['✕','○','□','△'];
for(let i=0;i<4;i++)check(element('#presets').children[i].children[0].textContent.startsWith(symbols[i]),'visible face symbol '+i);
gamepads=[simulatedPad(0,0,false)];tick(100);
for(let i=0;i<4;i++){
  const before=presetRequests.length;
  gamepads=[simulatedPad(0,0,false,[i])];tick(100);
  check(presetRequests.length===before,'face press waits before recall '+i);
  tick(100);check(presetRequests.length===before,'held face does not recall '+i);
  gamepads=[simulatedPad(0,0,false)];tick(100);
  check(presetRequests.length===before+1&&presetRequests[presetRequests.length-1].body.startsWith(i+','),'short release recalls corresponding preset without L2 '+i);
}
// An in-flight joystick write must be followed by neutral before the preset.
deferNextMove=true;gamepads=[simulatedPad(1,0,true)];tick(100);
const beforeDelayed=presetRequests.length;const orderStart=commandOrder.length;
gamepads=[simulatedPad(1,0,true,[0])];tick(100);gamepads=[simulatedPad(1,0,true)];tick(100);
check(presetRequests.length===beforeDelayed,'preset waits for pending motion write');
releaseMove();flush();
check(presetRequests.length===beforeDelayed+1,'preset follows acknowledged neutral');
const delayedOrder=commandOrder.slice(orderStart);
check(delayedOrder.indexOf('move:0,0')>=0&&delayedOrder.findIndex(v=>v.startsWith('preset:'))>delayedOrder.indexOf('move:0,0'),'neutral ordered before preset');
gamepads=[simulatedPad(0,0,false)];tick(100);
element('#zoomSpeed').value='7';
gamepads=[simulatedPad(0,0,false,[12])];tick(100);
check(zoomRequests[zoomRequests.length-1].url==='/zoom'&&zoomRequests[zoomRequests.length-1].body==='4095,7','D-pad up zooms in at manual speed without L2');
const heldZoomCount=zoomRequests.length;tick(100);check(zoomRequests.length===heldZoomCount,'held arrow sends one trajectory');
gamepads=[simulatedPad(0,0,false)];tick(100);
check(zoomRequests[zoomRequests.length-1].url==='/zoom/stop','D-pad release stops');
gamepads=[simulatedPad(0,0,false,[13])];tick(100);
check(zoomRequests[zoomRequests.length-1].body==='0,7','D-pad down zooms out');
events.gamepaddisconnected();flush();
check(zoomRequests[zoomRequests.length-1].url==='/zoom/stop','controller disconnect stops D-pad zoom');
gamepads=[simulatedPad(0,0,false,[13])];tick(100);
const heldAfterReconnect=zoomRequests.length;tick(100);
check(zoomRequests.length===heldAfterReconnect,'held arrow cannot restart after reconnect');
gamepads=[simulatedPad(0,0,false)];tick(100);gamepads=[simulatedPad(0,0,false,[12])];tick(100);
element('#pad').onpointerdown({pointerId:3,clientX:120,clientY:120});flush();tick(100);
check(zoomRequests[zoomRequests.length-1].url==='/zoom/stop','virtual joystick cancels D-pad zoom');
element('#pad').onpointerup();flush();const virtualZoomCount=zoomRequests.length;tick(100);tick(100);
check(zoomRequests.length===virtualZoomCount,'held arrow cannot take over after virtual input');
gamepads=[simulatedPad(0,0,false)];tick(100);
serverZoom={present:false,calibrated:false,available:false,position:null,moving:false,error:''};tick(1000);
const missingMotorZoomCount=zoomRequests.length;gamepads=[simulatedPad(0,0,false,[12])];tick(100);
check(zoomRequests.length===missingMotorZoomCount,'D-pad ignores absent motor');
serverZoom={present:true,calibrated:true,available:true,position:2048,moving:false,target:2048,error:''};tick(1000);tick(100);
check(zoomRequests.length===missingMotorZoomCount,'motor return cannot start held D-pad');
gamepads=[simulatedPad(0,0,false)];tick(100);gamepads=[simulatedPad(0,0,false,[12])];tick(100);
const beforeFaceRecall=presetRequests.length;gamepads=[simulatedPad(0,0,false,[12,0])];tick(100);gamepads=[simulatedPad(0,0,false,[12])];tick(100);
check(presetRequests.length===beforeFaceRecall+1,'face preset takes priority over D-pad');
const zoomAfterFace=zoomRequests.length;tick(100);tick(100);
check(zoomRequests.length===zoomAfterFace,'held D-pad does not override preset');
events.blur();flush();gamepads=[simulatedPad(0,0,false)];tick(100);gamepads=[simulatedPad(0,0,false,[0,12])];
const hiddenPresetCount=presetRequests.length,hiddenZoomCount=zoomRequests.length;tick(100);
check(presetRequests.length===hiddenPresetCount&&zoomRequests.length===hiddenZoomCount,'hidden page suppresses face and D-pad');
events.focus();tick(100);
check(presetRequests.length===hiddenPresetCount&&zoomRequests.length===hiddenZoomCount,'held buttons cannot act on focus return');
gamepads=[simulatedPad(0,0,false)];tick(100);
serverPresets=[null,null,null,null,null];element('#presets').children[0].children[1].onclick();flush();
const emptyCount=presetRequests.length;gamepads=[simulatedPad(0,0,false,[0])];tick(100);gamepads=[simulatedPad(0,0,false)];tick(100);
check(presetRequests.length===emptyCount&&element('#presetMessage').textContent.includes('non enregistré'),'empty preset ignored');

// Long holds save even empty slots and never dispatch a goto.
for(let i=0;i<4;i++){
  gamepads=[simulatedPad(0,0,false)];tick(100);
  const before=presetRequests.length;
  gamepads=[simulatedPad(0,0,false,[i])];tick(100);
  for(let j=0;j<9;j++)tick(100);
  check(presetRequests.length===before,'long hold does not recall before threshold '+i);
  tick(100);
  check(presetRequests.length===before+1&&presetRequests[presetRequests.length-1].url==='/preset/save'&&presetRequests[presetRequests.length-1].body===String(i),'long hold saves associated slot '+i);
  for(let j=0;j<15;j++)tick(100);
  gamepads=[simulatedPad(0,0,false)];tick(100);
  check(presetRequests.length===before+1,'long hold and release write once without recall '+i);
}
const beforeCancelledHold=presetRequests.length;
gamepads=[simulatedPad(0,0,false,[0])];tick(100);events.blur();flush();
for(let j=0;j<12;j++)tick(100);
events.focus();tick(100);gamepads=[simulatedPad(0,0,false)];tick(100);
check(presetRequests.length===beforeCancelledHold,'focus loss cancels pending save and release');

// Inversion changes display and manual/gamepad targets, never stored coordinates.
serverPresets=[[0,0,0],null,null,null,null];serverZoomPresets=[1024,null,null,null,null];
serverZoom={present:true,calibrated:true,available:true,position:1024,moving:false,target:1024,error:''};
element('#presets').children[0].children[1].onclick();flush();tick(1000);
check(element('#zoomStatus').textContent.includes('25.0'),'normal zoom percentage');
check(element('#zoomInvert').attributes['aria-pressed']==='false','normal direction by default');
element('#zoomInvert').onclick();flush();
check(element('#zoomInvert').attributes['aria-pressed']==='true'&&preferences['ronin.zoomInverted']==='1','inversion toggles and persists');
check(element('#zoomStatus').textContent.includes('75.0')&&element('#zoomStatus').textContent.includes('1024/4095'),'inverted percentage retains raw measurement');
check(element('#zoomPosition').value==='3071','slider mirrors current motor position');
check(element('#presets').children[0].children[0].textContent.includes('75.0'),'saved preset percentage mirrors');
element('#zoomSpeed').value='7';element('#zoomPosition').oninput({target:{value:'2200'}});flush();
check(zoomRequests[zoomRequests.length-1].body==='1895,7','inverted slider maps to raw motor position');
check(element('#zoomMessage').textContent.includes('53.7'),'target caption follows inverted display');
element('#zoomIn').onclick();flush();check(zoomRequests[zoomRequests.length-1].body==='1854,7','plus moves toward inverted zoom in');
element('#zoomOut').onclick();flush();check(zoomRequests[zoomRequests.length-1].body==='1895,7','minus moves toward inverted zoom out');
element('#zoomPosition').oninput({target:{value:'0'}});flush();check(zoomRequests[zoomRequests.length-1].body==='4095,7','inverted zero endpoint');
element('#zoomPosition').oninput({target:{value:'4095'}});flush();check(zoomRequests[zoomRequests.length-1].body==='0,7','inverted full endpoint');
function xbox(buttons=[]){let p=simulatedPad(0,0,false,buttons);p.id='Xbox controller';return p}
gamepads=[xbox()];tick(100);gamepads=[xbox([12])];tick(100);
check(zoomRequests[zoomRequests.length-1].body==='0,7','Xbox up respects inversion');
gamepads=[xbox()];tick(100);gamepads=[xbox([13])];tick(100);
check(zoomRequests[zoomRequests.length-1].body==='4095,7','Xbox down respects inversion');
const presetCountBeforeToggle=presetRequests.length;
element('#zoomInvert').onclick();flush();
check(zoomRequests[zoomRequests.length-1].url==='/zoom/stop','changing direction stops active zoom');
const afterDirectionStop=zoomRequests.length;tick(100);tick(100);
check(zoomRequests.length===afterDirectionStop,'held arrow cannot reverse itself on toggle');
check(presetRequests.length===presetCountBeforeToggle,'toggle does not modify any preset');
check(element('#zoomInvert').attributes['aria-pressed']==='false'&&preferences['ronin.zoomInverted']==='0','direction can be restored dynamically');
check(element('#zoomStatus').textContent.includes('25.0')&&element('#presets').children[0].children[0].textContent.includes('25.0'),'normal percentages restored');
preferences['ronin.zoomInverted']='1';
eval(pageScript);flush();
check(element('#zoomInvert').attributes['aria-pressed']==='true'&&element('#zoomStatus').textContent.includes('75.0'),'saved direction restored on reload');
localStorage.getItem=()=>{throw new Error('storage unavailable')};
eval(pageScript);flush();
check(element('#zoomInvert').attributes['aria-pressed']==='false','storage failure keeps normal default');
localStorage.setItem=()=>{throw new Error('storage unavailable')};
element('#zoomInvert').onclick();flush();
check(element('#zoomInvert').attributes['aria-pressed']==='true'&&element('#zoomStatus').textContent.includes('75.0'),'toggle works without persistent storage');

// ---- WebSocket transport: push state, session-bound commands, safe switch to HTTP ----
localStorage.getItem=key=>preferences[key]??null;localStorage.setItem=(key,value)=>{preferences[key]=value};preferences['ronin.zoomInverted']='0';
intervals.length=0;moves.length=0;zoomRequests.length=0;commandOrder.length=0;
online=true;serverReady=true;serverVerified=true;serverControls=true;serverSession=42;serverSleeping=null;gamepads=[];deferNextMove=false;
const realNow=Date.now;let skew=0;Date.now=()=>realNow()+skew;
globalThis.location={protocol:'http:',host:'ronin.test'};
const sockets=[];
class FakeWebSocket{
  constructor(url){this.url=url;this.readyState=0;this.sent=[];this.bufferedAmount=0;sockets.push(this)}
  send(text){if(this.readyState!==1)throw new Error('not open');this.sent.push(text)}
  close(){if(this.readyState===3)return;this.readyState=3;if(this.onclose)this.onclose({})}
  open(){this.readyState=1;if(this.onopen)this.onopen({})}
  recv(text){if(this.onmessage)this.onmessage({data:text})}
  drop(){this.readyState=3;if(this.onclose)this.onclose({})}
}
globalThis.WebSocket=FakeWebSocket;
const pushState=(over={})=>'s'+JSON.stringify(Object.assign({ble:true,protocolVerified:true,controlEnabled:true,battery:80,session:serverSession,
  zoom:{present:true,calibrated:true,position:2000,available:true,moving:false,target:2000,error:''}},over));
eval(pageScript);flush();
tick(500);
check(sockets.length===1&&sockets[0].url==='ws://ronin.test/ws','page opens the /ws socket');
let ws1=sockets[0];
check(!element('#wsStatus').textContent.includes('WebSocket'),'no WebSocket label until the socket is open');
ws1.open();flush();
ws1.recv(pushState());flush();
check(ws1.sent.includes('h42'),'hello carries the session from the pushed state');
check(element('#status').textContent.includes('BLE connecté'),'pushed state drives the UI');
ws1.recv('r1');flush();
check(element('#wsStatus').textContent.includes('WebSocket'),'controller label');
let polled=statusRequests;tick(1000);check(statusRequests===polled,'fresh push replaces /status polling');
ws1.recv(pushState());
// Commands travel over the socket and not over HTTP while controller.
gamepads=[simulatedPad(0,0,false)];tick(100);gamepads=[simulatedPad(1,0,true)];tick(100);
check(ws1.sent.includes('m300,0')&&moves.length===0,'motion goes over the socket');
element('#zoomPosition').oninput({target:{value:'1000'}});flush();
check(ws1.sent.some(m=>/^z1000,[0-9]+$/.test(m))&&zoomRequests.length===0,'zoom goes over the socket');
tick(500);check(ws1.sent.includes('k'),'zoom keepalive goes over the socket');
element('#zoomStop').onclick();flush();check(ws1.sent.includes('x'),'zoom stop goes over the socket');
// Heartbeat and RTT
tick(500);const ping=ws1.sent.filter(m=>m[0]==='p').pop();check(/^p[0-9]+$/.test(ping),'client sends timestamped pings');
ws1.recv('o'+ping.slice(1));flush();check(/RTT [0-9]+ ms/.test(element('#wsStatus').textContent),'RTT is displayed');
ws1.recv('o'+'4294967295');ws1.recv('oabc');flush();check(element('#wsStatus').textContent.includes('WebSocket'),'garbage pong ignored');
// Socket lost while the trigger is held: stop over HTTP, no parasitic motion, re-arm required.
let before=moves.length;
ws1.drop();flush();
check(moves.length===before+1&&moves[moves.length-1].body==='0,0','socket loss sends an HTTP stop');
check(element('#wsStatus').textContent==='Liaison : HTTP','label falls back to HTTP');
tick(100);tick(100);check(moves.length===before+1,'no motion after the switch while the trigger is still held');
gamepads=[simulatedPad(0,0,false)];tick(100);gamepads=[simulatedPad(1,0,true)];tick(100);
check(moves[moves.length-1].body==='300,0','HTTP motion resumes only after release and recenter');
gamepads=[simulatedPad(0,0,false)];tick(100);
// Reconnection with backoff
const opened=sockets.length;tick(500);check(sockets.length===opened,'no immediate reconnect');
skew+=600;tick(500);check(sockets.length===opened+1,'reconnects after the first backoff');
sockets[sockets.length-1].drop();skew+=600;tick(500);check(sockets.length===opened+1,'backoff grows');
skew+=600;tick(500);check(sockets.length===opened+2,'reconnects after the longer backoff');
const ws3=sockets[sockets.length-1];skew+=5000;tick(500);check(ws3.readyState===3,'handshake that never completes is abandoned');
// Observer: read-only, commands stay on HTTP
skew+=20000;tick(500);const ws4=sockets[sockets.length-1];ws4.open();ws4.recv(pushState());ws4.recv('r0');flush();
check(element('#wsStatus').textContent.includes('lecture seule'),'observer label');
before=moves.length;gamepads=[simulatedPad(1,0,true)];tick(100);
check(moves.length>before&&!ws4.sent.some(m=>m[0]==='m'),'observer keeps using HTTP for commands');
gamepads=[simulatedPad(0,0,false)];tick(100);
// Session change: hello again, and no socket commands until granted for the new session
skew+=3000;ws4.recv('r1');flush();
check(ws4.sent.filter(m=>m[0]==='h').length>=1,'hello sent');
serverSession=50;skew+=1500;ws4.recv(pushState({session:50}));flush();
check(ws4.sent.includes('h50'),'new session triggers a new hello');
gamepads=[simulatedPad(0,0,false)];tick(100);before=moves.length;const socketMoves=ws4.sent.filter(m=>m[0]==='m').length;gamepads=[simulatedPad(1,0,true)];tick(100);
check(moves.length>before&&ws4.sent.filter(m=>m[0]==='m').length===socketMoves,'HTTP carries motion until the hello is granted');
gamepads=[simulatedPad(0,0,false)];tick(100);
ws4.recv('r1');flush();
// Server-side refusals
ws4.recv('esess');flush();check(element('#wsStatus').textContent.includes('WebSocket'),'session error keeps the socket');
ws4.recv('ebad');flush();check(element('#wsStatus').textContent==='Liaison : HTTP','protocol error drops the socket');
// Heartbeat loss degrades, stops, and falls back
skew+=20000;tick(500);const ws5=sockets[sockets.length-1];ws5.open();ws5.recv(pushState({session:50}));ws5.recv('r1');flush();
ws5.sent.length=0;ws5.recv('r1');
gamepads=[simulatedPad(0,0,false)];tick(100);gamepads=[simulatedPad(1,0,true)];tick(100);
before=moves.length;skew+=3000;tick(500);
check(element('#wsStatus').textContent.includes('dégradée'),'missing heartbeat is shown');
check(moves.length>before&&moves[moves.length-1].body==='0,0','degraded link stops motion over HTTP');
tick(100);tick(100);check(moves[moves.length-1].body==='0,0','held trigger does not move a degraded link');
skew+=4000;tick(500);check(ws5.readyState===3,'dead heartbeat closes the socket');
// Stale push falls back to HTTP polling
skew+=20000;tick(500);const ws6=sockets[sockets.length-1];ws6.open();ws6.recv(pushState({session:50}));flush();
polled=statusRequests;skew+=2000;tick(1000);check(statusRequests>polled,'stale push resumes /status polling');
// Hidden page releases the socket and does not reconnect until visible again
const hiddenCount=sockets.length;document.hidden=true;events['document.visibilitychange']();flush();
check(ws6.readyState===3,'hidden page closes the socket');skew+=20000;tick(500);check(sockets.length===hiddenCount,'no reconnect while hidden');
document.hidden=false;events['document.visibilitychange']();tick(500);check(sockets.length===hiddenCount+1,'reconnect when visible again');
Date.now=realNow;

check(!/expérimental|en essai|aux essais|pour essais/.test(readFile('main/index.html')), 'no experimental UI labels');
print('Web and gamepad simulation passed');
