load('main/gamepad.js');
function check(value, msg) { if (!value) throw new Error(msg); }
function pad(x, y, trigger) {
  return {index: 0, id: 'DualSense test', mapping: 'standard', connected: true,
    axes: [x, y], buttons: Array.from({length: 7}, (_, i) => ({pressed: i === 6 && trigger, value: i === 6 && trigger ? 1 : 0}))};
}
let ctl = createGamepadControl();
let state = ctl.update(true, true, true, pad(1, 0, true));
check(!state.armed && !state.moving, 'held stick cannot arm');
state = ctl.update(true, true, true, pad(0, 0, false));
check(state.armed && !state.moving, 'release at center arms');
state = ctl.update(true, true, true, pad(1, -1, true));
check(state.yaw === 300 && state.pitch === 300 && state.moving, 'axes and limits');
state = ctl.update(true, true, true, pad(0.1, -0.1, true));
check(!state.moving, 'deadzone');
state = ctl.update(true, true, true, pad(1, 0, false));
check(!state.moving, 'trigger release');
state = ctl.update(true, true, false, pad(1, 0, true));
check(!state.armed && !state.moving, 'BLE loss');
state = ctl.update(true, true, true, pad(1, 0, true));
check(!state.moving, 'reconnection requires neutral and release');
ctl.update(true, true, true, pad(0, 0, false));
check(ctl.update(true, true, true, pad(-1, 1, true)).yaw === -300, 'rearm');
check(!ctl.update(true, false, true, pad(1, 1, true)).moving, 'hidden page');
check(!ctl.update(true, true, true, null).moving, 'disconnected pad');
let unknown = pad(1, 1, true); unknown.mapping = '';
check(!ctl.update(true, true, true, unknown).armed, 'nonstandard mapping rejected');
unknown = pad(NaN, 0, true);
check(!ctl.update(true, true, true, unknown).moving, 'invalid input rejected');
function shortcutPad(indices=[]) {
  return {index:0,id:'DualSense',mapping:'standard',connected:true,buttons:Array.from({length:16},(_,i)=>({pressed:indices.includes(i),value:indices.includes(i)?1:0}))};
}
let shortcuts=createGamepadShortcuts();
let now=0;
function sample(indices=[],visible=true,ready=true,advance=100){now+=advance;return shortcuts.update(visible,ready,shortcutPad(indices),now)}
check(sample([0,12]).preset===null,'initial held face ignored');
check(sample([0,12],true,true,1500).save===null,'initial held face cannot save');
check(sample([0,12]).zoom===0,'initial held D-pad ignored');sample();
for(let i=0;i<4;i++){
  let s=sample([i]);check(s.preset===null&&s.save===null,'press does not move or save '+i);
  check(sample([i]).preset===null,'held face waits '+i);
  s=sample();check(s.preset===i&&s.save===null,'short release recalls '+i);
  sample();
  s=sample([i]);check(s.preset===null&&s.save===null,'long hold starts quietly '+i);
  s=sample([i],true,true,999);check(s.save===null&&s.preset===null,'below threshold waits '+i);
  s=sample([i],true,true,1);check(s.save===i&&s.preset===null,'threshold saves without recall '+i);
  s=sample([i],true,true,2000);check(s.save===null&&s.preset===null,'continued hold never repeats '+i);
  s=sample();check(s.preset===null&&s.save===null,'long release does not recall '+i);
}
sample([2]);let delayed=sample([],true,true,1100);
check(delayed.save===2&&delayed.preset===null,'long release recognized after delayed poll');
sample([0]);check(sample([0,1]).save===null,'ambiguous face chord ignored');
check(sample([1],true,true,1500).save===null,'chord release cannot start a hold');
check(sample().preset===null,'chord release cannot recall');
check(sample([12]).zoom===1,'up holds zoom in without L2');
check(sample([12]).zoom===1,'held zoom direction persists');
check(sample().zoom===0,'release stops zoom');
check(sample([13]).zoom===-1,'down holds zoom out');
check(sample([12,13]).zoom===0,'opposite arrows cancel');
check(sample([12]).zoom===0,'opposite chord requires full release');sample();
check(sample([12]).zoom===1,'released arrows rearm');
check(sample([12],false).zoom===0,'hidden page cancels shortcuts');
check(sample([12]).zoom===0,'held arrow cannot restart after focus loss');sample();
sample([0]);sample([0],true,false);
check(sample([0],true,true,1500).save===null,'held preset suppressed after reconnect');
check(sample().preset===null,'reconnect release cannot recall');
sample([0]);sample([0],false,true);
check(sample([0],true,true,1500).save===null,'held preset suppressed after focus return');
check(sample().preset===null,'focus return release cannot recall');
sample([0]);check(sample().preset===0,'fresh short press works');
sample([1]);
const other=shortcutPad();other.id='Another controller';now+=100;
check(shortcuts.update(true,true,other,now).preset===null,'identity change cancels pending hold');now+=1500;
let switched=shortcuts.update(true,true,other,now);
check(switched.preset===null&&switched.save===null,'old press cannot act on replacement controller');
print('Gamepad simulation passed');
