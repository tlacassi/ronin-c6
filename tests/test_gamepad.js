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
print('Gamepad simulation passed');
