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
  function createGamepadShortcuts() {
    let identity = null;
    let previousFaces = [false, false, false, false];
    let zoomArmed = false;
    let pendingFace = null, pressedAt = 0, longFired = false;
    const pressed = button => !!button && (button.pressed === true ||
      (Number.isFinite(button.value) && button.value > 0.5));
    function reset() { identity = null; previousFaces.fill(false); zoomArmed = false; pendingFace = null; longFired = false; }
    return {
      reset,
      cancelZoom() { zoomArmed = false; },
      update(visible, linkReady, pad, nowMs = typeof performance !== 'undefined' ? performance.now() : Date.now()) {
        const none = {preset: null, save: null, zoom: 0};
        if (!visible || !linkReady || !pad || !pad.connected ||
            pad.mapping !== 'standard' || !pad.buttons || pad.buttons.length < 4 || !Number.isFinite(nowMs)) {
          reset();
          return none;
        }
        // W3C standard: bottom/right/left/top face buttons, D-pad up/down.
        const faces = [0, 1, 2, 3].map(index => pressed(pad.buttons[index]));
        const up = pressed(pad.buttons[12]), down = pressed(pad.buttons[13]);
        const current = String(pad.index) + ':' + String(pad.id);
        if (identity !== current) {
          identity = current;
          pendingFace = null; longFired = false;
          previousFaces = faces;
          zoomArmed = !up && !down;
          return none;
        }
        const count = faces.filter(Boolean).length;
        const face = count === 1 ? faces.findIndex(Boolean) : -1;
        let preset = null, save = null;
        if (count > 1) {
          pendingFace = null; longFired = false;
        } else if (pendingFace !== null) {
          const elapsed = nowMs - pressedAt;
          if (elapsed < 0) {
            pendingFace = null; longFired = false;
          } else if (faces[pendingFace]) {
            if (!longFired && elapsed >= 1000) { save = pendingFace; longFired = true; }
          } else {
            // Decide on release too, in case no poll occurred at the threshold.
            if (count === 0 && !longFired) {
              if (elapsed >= 1000) save = pendingFace;
              else preset = pendingFace;
            }
            pendingFace = null; longFired = false;
          }
        } else if (face >= 0 && !previousFaces[face]) {
          pendingFace = face; pressedAt = nowMs; longFired = false;
        }
        previousFaces = faces;
        if (!up && !down) { zoomArmed = true; }
        if (up && down) { zoomArmed = false; }
        return {preset, save, zoom: zoomArmed && up !== down ? (up ? 1 : -1) : 0};
      }
    };
  }
  root.createGamepadControl = createGamepadControl;
  root.createGamepadShortcuts = createGamepadShortcuts;
})(typeof globalThis !== 'undefined' ? globalThis : this);
