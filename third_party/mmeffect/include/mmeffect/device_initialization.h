#pragma once

namespace mme {

// Per-device lazy initialization. A failed attempt stays pending; the failure
// callback reports every attempt. This state is independent of the user's
// effects-disabled flag, which must neither prevent retries nor be cleared by
// a later successful initialization.
class DeviceInitialization {
public:
    template<class Initialize, class ReportFailure>
    bool EnsureReady(Initialize initialize, ReportFailure reportFailure) {
        if (ready_)
            return true;
        if (initialize() != 0) {
            reportFailure();
            return false;
        }
        ready_ = true;
        return true;
    }

    bool IsReady() const { return ready_; }
    void Reset() { ready_ = false; }

private:
    bool ready_ = false;
};

} // namespace mme
