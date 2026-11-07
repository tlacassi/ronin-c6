/* Pure input state machine, also exercised by the host simulation. */
(function (root) {
  'use strict';
  function createGamepadControl() {
    let armed = false;
    let identity = null;
    return {
      reset() { armed = false; identity = null; },
      update(enabled, visible, linkReady, pad) {
        if (!enabled || !visible || !linkReady || !pad || !pad.connected ||
            pad.mapping !== 'standard' || !pad.axes || pad.axes.length < 2 ||
            !pad.buttons || pad.buttons.length <= 6 || !pad.buttons[6] ||
            !Number.isFinite(pad.axes[0]) || !Number.isFinite(pad.axes[1])) {
          armed = false; identity = null;
          return { yaw: 0, pitch: 0, armed: false, moving: false };
        }
        const current = String(pad.index) + ':' + String(pad.id);
        if (identity !== current) { identity = current; armed = false; }
        const clamp = v => Math.max(-1, Math.min(1, v));
        const x = clamp(pad.axes[0]);
        const y = clamp(pad.axes[1]);
        const held = pad.buttons[6].pressed || pad.buttons[6].value > 0.5;
        const neutral = Math.abs(x) <= 0.12 && Math.abs(y) <= 0.12;
        if (!held && neutral) { armed = true; }
        if (!armed || !held) { return { yaw: 0, pitch: 0, armed, moving: false }; }
        const scale = v => Math.abs(v) <= 0.12 ? 0 : Math.sign(v) * (Math.abs(v) - 0.12) / 0.88;
        const yaw = Math.round(scale(x) * 300);
        const pitch = Math.round(-scale(y) * 300);
        return { yaw, pitch, armed: true, moving: yaw !== 0 || pitch !== 0 };
      }
    };
  }
  root.createGamepadControl = createGamepadControl;
})(typeof globalThis !== 'undefined' ? globalThis : this);
