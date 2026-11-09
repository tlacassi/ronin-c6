@main
struct TriggerDoubleTapTests {
    static func main() {
        var tap = TriggerDoubleTap()
        func event(_ pressed: Bool, _ time: Double, neutral: Bool = true, ready: Bool = true) -> Bool {
            tap.update(pressed: pressed, neutral: neutral, ready: ready, now: time)
        }
        assert(!event(true, 0)); assert(!event(false, 0.1))
        assert(!event(true, 0.2)); assert(event(false, 0.3))
        assert(!event(true, 1)); assert(!event(false, 2))
        assert(!event(true, 2.1)); assert(!event(false, 2.2))
        assert(!event(true, 3)); assert(!event(false, 3.1))
        assert(!event(true, 3.2, neutral: false)); assert(!event(false, 3.3))
        assert(!event(true, 4, ready: false)); assert(!event(false, 4.1))
        assert(!event(true, 4.2)); assert(!event(false, 4.3))
        print("Trigger double tap: passed")
    }
}
