#pragma once

namespace lf::ui {

// Tells Windows that a video is on screen, so the display does not go to standby and the PC does not sleep while
// nobody touches the keyboard or mouse. libmpv renders through the render API, with no window of its own, so its
// own screensaver inhibition never runs: the application has to ask.
//
// The request belongs to the thread that makes it (SetThreadExecutionState), so use one KeepAwake from the GUI
// thread only. It is withdrawn when switched off and when destroyed.
class KeepAwake {
public:
    KeepAwake() = default;
    ~KeepAwake();
    KeepAwake(const KeepAwake&) = delete;
    KeepAwake& operator=(const KeepAwake&) = delete;

    void setActive(bool active);
    bool isActive() const { return m_active; }

private:
    bool m_active = false;
};

}  // namespace lf::ui
