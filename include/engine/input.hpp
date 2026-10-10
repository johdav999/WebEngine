#pragma once
#include "engine/math.hpp"
#include <cstdint>
#include <functional>
#include <span>
#include <unordered_map>
#include <vector>

namespace engine {
using InputActionId = std::uint32_t;
namespace InputActions {
inline constexpr InputActionId Orbit=1, Pan=2, Zoom=3, Select=4, ResetCamera=5,
    Move=6, Look=7, Interact=8;
}
enum class InputEventType { ActionValue, ActionDelta, PointerDown, PointerMove, PointerUp, PointerCancel, CancelAll };
enum class InputDevice { Mouse, Touch, Pen };

// Browser ABI: eleven float64 fields (88 bytes), independent of C++ enum layout.
// Action records use id=action, source=independent binding, x/y=value.
// Pointer records use id=pointerId, CSS-local x/y, backing-pixel pixelX/Y.
struct InputPacket {
    double type=0, id=0, source=0, x=0, y=0, pixelX=0, pixelY=0,
        buttons=0, device=0, pressure=0, timestamp=0;
};
static_assert(sizeof(InputPacket)==11*sizeof(double));
struct InputActionState {
    Vec2 value{}, delta{};
    bool pressed=false, held=false, released=false;
};
struct InputPointer {
    std::int32_t id=0;
    Vec2 position{}, pixelPosition{};
    std::uint32_t buttons=0;
    InputDevice device=InputDevice::Mouse;
    float pressure=0;
};

// Device adapters submit records; controllers consume a stable frame snapshot.
// Multiple bindings contribute independently. Opposing held axes still count as held.
class InputSystem {
public:
    static constexpr std::size_t maxQueuedEvents=4096;
    bool submit(std::span<const InputPacket> packets);
    void beginFrame();
    void reset(); // Immediately discard queued/held input and frame edges at a lifecycle boundary.
    const InputActionState& action(InputActionId id) const;
    const std::unordered_map<std::int32_t,InputPointer>& pointers() const { return pointers_; }
    const std::vector<InputPacket>& events() const { return events_; }
private:
    struct Action {
        InputActionState state;
        std::unordered_map<std::uint32_t,Vec2> sources;
    };
    std::unordered_map<InputActionId,Action> actions_;
    std::unordered_map<std::int32_t,InputPointer> pointers_;
    std::vector<InputPacket> pending_,events_;
    void cancel();
    void apply(const InputPacket& packet);
};

using InputControlId = std::uint32_t;
using InputContextId = std::uint32_t;
struct InputBinding {
    InputContextId context=0;
    InputControlId control=0;
    InputActionId action=0;
    Vec2 value{1,0};
    bool displacement=false;
};
struct InputContext {
    InputContextId id=0;
    bool blockLower=true;
};
// Portable device-to-action mapping. Stack order is bottom to top; the first
// matching context owns a control. Configuration changes release this mapper's
// sources only. The borrowed InputSystem (or callback captures) must outlive this object. Reserve the
// [firstSource, firstSource+1023] range exclusively for this mapper.
class InputBindings {
public:
    using Submit = std::function<bool(std::span<const InputPacket>)>;
    explicit InputBindings(InputSystem& input,std::uint32_t firstSource=0x80000000u);
    // Owns the non-throwing callback, which consumes packets synchronously and must implement
    // InputSystem's atomic rejection/cancellation contract. Application::submitInput
    // is a suitable sink; do not bypass its host/lifecycle gate.
    explicit InputBindings(Submit submit,std::uint32_t firstSource=0x80000000u);
    ~InputBindings();
    InputBindings(const InputBindings&)=delete;
    InputBindings& operator=(const InputBindings&)=delete;
    void setBindings(std::span<const InputBinding> bindings);
    void setContexts(std::span<const InputContext> contexts);
    bool control(InputControlId control,float value); // Scalar key/axis or displacement.
    void cancel();
private:
    Submit submit_;
    std::uint32_t firstSource_;
    std::vector<InputBinding> bindings_;
    std::vector<InputContext> contexts_;
    std::vector<bool> held_;
};
} // namespace engine
