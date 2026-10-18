#pragma once
#include <cstdint>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace engine {
// Admission limits for indivisible new assets, not simulation/dynamic uniforms.
// Zero disables a limit. One oversized first resource is allowed for progress.
struct UploadBudget {
    std::uint64_t maxBytes=0,maxResources=0;
    double maxMilliseconds=0;
    void validate() const {
        if(!std::isfinite(maxMilliseconds)||maxMilliseconds<0)
            throw std::invalid_argument("Invalid upload time budget");
    }
};
class UploadAdmission {
public:
    void begin(UploadBudget budget) { budget.validate();budget_=budget;bytes_=resources_=0; }
    bool admit(std::uint64_t bytes,double elapsedMilliseconds) {
        if(bytes>std::numeric_limits<std::uint64_t>::max()-bytes_||resources_==std::numeric_limits<std::uint64_t>::max())return false;
        if(resources_ && ((budget_.maxResources&&resources_>=budget_.maxResources)||
            (budget_.maxBytes&&(bytes_>=budget_.maxBytes||bytes>budget_.maxBytes-bytes_))||
            (budget_.maxMilliseconds&&elapsedMilliseconds>=budget_.maxMilliseconds))) return false;
        bytes_+=bytes;++resources_;return true;
    }
    std::uint64_t bytes() const { return bytes_; }
    std::uint64_t resources() const { return resources_; }
private:
    UploadBudget budget_;
    std::uint64_t bytes_=0,resources_=0;
};
}
