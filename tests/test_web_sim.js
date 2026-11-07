/* Runs the actual page script against a simulated browser, network and gamepad. */
const intervals = [];
const events = {};
const elements = {};
const moves = [];
const presetRequests = [];
let serverSession = 42;
let serverReady = true;
let online = true;
let gamepads = [];
function element(name) {
  return elements[name] || (elements[name] = {
    checked: false, value: '', textContent: '', style: {},
    children: [], replaceChildren() {this.children=[];}, append(...v) {this.children.push(...v);}, setPointerCapture() {},
    getBoundingClientRect() { return {left: 0, top: 0, width: 240, height: 240}; }
  });
}
element('#joySpeed').value='100'; element('#presetDuration').value='20';
const document = {hidden: false, querySelector: element, createElement: () => ({children:[],setAttribute() {},append(...v) {this.children.push(...v);}}), addEventListener: (name, cb) => {events['document.'+name] = cb;}};
const window = {addEventListener: (name, cb) => {events[name] = cb;}};
const navigator = {getGamepads: () => gamepads};
const setInterval = (cb, ms) => { intervals.push({cb, ms}); return intervals.length; };
const setTimeout = () => 1;
const clearTimeout = () => {};
class AbortController { constructor() { this.signal = {}; } abort() {} }
function fetch(url, options) {
  if(!online) return Promise.reject(new Error('offline'));
  if(url === '/devices') return Promise.resolve({ok:true, json:async()=>({devices:[],selected:''})});
  if(url === '/presets') return Promise.resolve({ok:true, json:async()=>[[0,0,0],null,null,null,null]});
  if(url.startsWith('/preset/')) { presetRequests.push({url,body:options.body}); return Promise.resolve({ok:true}); }
  if(url === '/status') return Promise.resolve({ok:true, json:async()=>({ble:serverReady,session:serverSession,battery:null})});
  if(url === '/move') {
    let session = Number(options.headers['X-Ronin-Session']);
    let accepted = serverReady && session === serverSession;
    moves.push({body:options.body,session,accepted});
    return Promise.resolve({ok:accepted});
  }
  return Promise.reject(new Error('unexpected request'));
}
function flush() { for(let i=0;i<10;i++) drainMicrotasks(); }
function tick(ms) { for(const item of intervals) if(item.ms === ms) item.cb(); flush(); }
function check(value, message) { if(!value) throw new Error(message); }
function simulatedPad(x,y,held) {return {index:0,id:'PS5',mapping:'standard',connected:true,axes:[x,y],buttons:Array.from({length:7},(_,i)=>({pressed:i===6&&held,value:i===6&&held?1:0}))};}
load('main/gamepad.js');
const pageScript = readFile('main/index.html').split('<script>')[1].split('</script>')[0];
eval(pageScript);
flush();
check(element('#status').textContent.includes('BLE connecté'), 'initial link');
let modeElement = element('#gamepadMode'); modeElement.checked = true; modeElement.onchange(); flush();
gamepads=[simulatedPad(0,0,false)]; tick(100);
gamepads=[simulatedPad(1,0,true)]; tick(100);
check(moves.some(m => m.body==='300,0' && m.accepted), 'controller motion delivered');
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
events.focus(); modeElement.checked=false; modeElement.onchange(); flush();
element('#pad').onpointerdown({pointerId:1,clientX:208,clientY:120}); flush();
check(moves[moves.length-1].body==='300,0', 'virtual joystick still works');
element('#pad').onpointerup(); flush(); check(moves[moves.length-1].body==='0,0', 'virtual release stops');
element('#joySpeed').value='200'; element('#pad').onpointerdown({pointerId:1,clientX:208,clientY:120}); flush();
check(moves[moves.length-1].body==='600,0', '200 percent virtual motion');
element('#pad').onpointerup(); flush();
element('#presetDuration').value='5';
element('#presets').children[0].children[0].onclick(); flush();
check(presetRequests.some(p=>p.url==='/preset/goto' && p.body==='0,05'), 'half second encoded with leading zero');

online=false; tick(1000); check(element('#status').textContent.includes('inaccessible'), 'network failure displayed');
print('Web and gamepad simulation passed');
