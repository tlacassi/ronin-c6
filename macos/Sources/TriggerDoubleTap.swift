// Recognize two short trigger clicks at neutral, never a held steering gesture.
struct TriggerDoubleTap {
    private var downAt: Double?
    private var releasedAt: Double?
    private var previousPressed = false

    mutating func update(pressed: Bool, neutral: Bool, ready: Bool, now: Double) -> Bool {
        guard ready && neutral && now.isFinite else {
            self = TriggerDoubleTap()
            previousPressed = pressed
            return false
        }
        guard pressed != previousPressed else { return false }
        previousPressed = pressed
        if pressed {
            downAt = now
            if let releasedAt, now - releasedAt > 0.35 { self.releasedAt = nil }
            return false
        }
        guard let downAt, now >= downAt, now - downAt <= 0.25 else {
            self.downAt = nil
            releasedAt = nil
            return false
        }
        self.downAt = nil
        if let releasedAt, downAt >= releasedAt, downAt - releasedAt <= 0.35 {
            self.releasedAt = nil
            return true
        }
        releasedAt = now
        return false
    }
}
