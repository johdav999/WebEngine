# Reusable object definitions and prefabs

`Prefab` in `engine_core` stores a configured actor/component hierarchy. Each
instantiation creates independent scene nodes and instance state while retaining
the existing `shared_ptr<const Asset>` resources. Applications own definitions,
registries and behaviour registration; the engine owns no global factory table.

## Define and instantiate

```cpp
#include "engine/prefab_components.hpp"

engine::ComponentFactoryRegistry factories;
engine::registerBuiltinPrefabComponents(factories);

engine::PrefabMeshSettings visual;
visual.mesh = engine::Mesh::cube(); // Prepare/import once, share across instances.

engine::PrefabNode root;
root.key = "root";
root.name = "Crate";
root.tags = {"movable"};

engine::PrefabNode body;
body.key = "visual";
body.kind = engine::PrefabNodeKind::Component;
body.name = "Visual";
body.component = engine::PrefabComponent{"MeshComponent", 1, visual};
root.children.push_back(body);
const engine::Prefab crate(std::move(root));

engine::PrefabOverride placement;
placement.key = "root";
engine::Transform transform;
transform.position = {3, 0, 2};
placement.transform = transform; // Local to the destination parent.

engine::Scene scene;
auto first = crate.instantiate(scene, factories, {&placement, 1});
auto second = crate.instantiate(scene, factories);
auto* object = scene.findActor(first.root);
auto* mesh = dynamic_cast<engine::MeshComponent*>(first.component(scene, "visual"));
// object/mesh are borrowed; first/second retain only scene-scoped handles.
```

The root is an actor. Children may be actors, components or plain grouping nodes,
including actors under components and components under grouping nodes. Component
ownership is the nearest actor ancestor. Ordered children and local transforms
follow the existing [scene hierarchy contracts](../README.md). Factories can
create required implementation children; skeletal/effect children retain their
normal protection and are constructed once per instance.

Definition keys are unique and nonempty across the whole prefab. Display names
may repeat. The definition is copied/moved into the prefab and exposed only as a
const view. Instance keys are local to that definition, not saved scene identities.
`PrefabInstance::actors` and `components` map authored keys to scene-scoped
handles. Plain grouping nodes and factory-created implementation children have
no keyed result entry; implementation components still have ordinary scene handles.
`root` resolves through `Scene::findActor`. Result lookup rejects foreign scenes,
pending destruction and stale/recycled handles. Removing one component or child
actor follows normal scene ownership; the result does not own an object or keep
its scene alive.

## Overrides and shared resources

Supply at most one `PrefabOverride` for each key. An engaged field replaces that
node's name, local transform, enabled state, tick flag or complete tag list. False
and empty values are explicit overrides. A component override replaces its full
type/version/configuration; copy the original typed settings and change selected
values when retaining the other fields. Overrides cannot change node kind or
hierarchy. Unknown keys, duplicate targets, empty/duplicate tags, invalid flags,
nonfinite/singular transforms and unknown factories/versions throw before
publication. Plain nodes have no tags/ticks; an actor's `tickEnabled` controls
both variable and fixed hooks, as in the existing actor API.

Disengaged definition tick flags preserve factory defaults, including automatic
particle-emitter ticking. Common name/transform/enabled/tags come from the
definition; specialized settings come from the factory. Instance overrides never
write back into the definition. Mesh geometry, textures, material assets, skeletons,
clips, collision shapes and effect assets retain their shared representations.
Material override values, morph weights, animation playback, particles, transforms
and behaviour state belong to each constructed component. Publish resources as
immutable assets and follow their existing validation/cache contracts.

## Registered component factories

Factories are synchronous trusted C++ construction code. Register an exact
configuration type and schema version; a mismatched `std::any` payload throws.
For example, with an application-defined `MovementBehaviour` component:

```cpp
struct MovementSettings { float speed = 2; };
factories.registerFactory<MovementSettings>("game.Movement", 1,
    [](const MovementSettings& config) -> std::unique_ptr<engine::Component> {
        if (!std::isfinite(config.speed) || config.speed < 0)
            throw std::invalid_argument("Invalid movement speed");
        return std::make_unique<MovementBehaviour>(config.speed);
    });
// PrefabComponent{"game.Movement", 1, MovementSettings{4}}
```

Factories must return a fresh, nonnull, detached component subtree. Validate
specialized configuration in the factory and override `Component::validate()`
when assembled hierarchy checks are needed. The generic path validates the full
detached hierarchy after construction, including disabled and implementation
children. It does not initialize scene-dependent physics or movement constraints;
those remain enforced at the normal scene reconciliation boundary. Configuration
contains definition data and shared immutable assets, never live components,
scene handles, playback state or mutable aliases to instance state.

The registry owns its callable captures. Any borrowed services captured by a
factory must outlive its registration and active instantiations. The prefab and
instance retain no factory callbacks. Duplicate registrations reject instead of
replacing an existing behaviour; explicitly unregister before changing one.
Multiple versions can coexist. Each instantiation snapshots the selected callables
before invoking any factory, so reentrant registry edits affect subsequent calls.
Calls run on the scene/hierarchy thread; concurrent mutation is unsupported.

Factory validation failures, null returns, exceptions and hierarchy-budget failures
destroy staged objects and leave the destination hierarchy untouched. Factories
should construct only their own subtree; side effects on captured external
services or scene objects cannot be rolled back. Owner-change hooks follow normal
detached construction and attachment semantics and must obey the existing
no-throw/hierarchy-mutation contract. Attach/enable/spawn hooks and ticking wait
for scene reconciliation. A destination traversal guard prevents captured
callbacks from detaching or flushing it during construction; publication rejects
it if a callback queued its destruction.

Default limits are 100,000 total constructed nodes and depth 128 (root depth 1),
including factory-created implementation children. Applications can lower these
through `PrefabLimits`. Limits bound hierarchy construction/validation; they are
not asset-byte budgets or a sandbox for executable factories. Construction is
synchronous, with no asynchronous job/cancellation ownership.

## Built-in configuration types

`registerBuiltinPrefabComponents` explicitly registers the following version-1
keys. Registration is atomic if any key already exists.

| Factory key | Exact configuration type |
| --- | --- |
| `Component` | `std::monostate` |
| `MeshComponent`, `SkeletalMeshComponent` | `PrefabMeshSettings` |
| `CameraComponent` | `PrefabCameraSettings` |
| `LightComponent` | `PrefabLightSettings` |
| `ColliderComponent` | `std::shared_ptr<const CollisionShape>` |
| `RigidBodyComponent` | `RigidBodySettings` |
| `ParticleEmitterComponent` | `std::shared_ptr<const ParticleEmitterDefinition>` |
| `EffectComponent` | `std::shared_ptr<const EffectAsset>` |

Mesh settings cover shared geometry/texture, per-slot material overrides, initial
morph weights and the existing render/mask/shadow flags. Camera settings use the
existing validated lens/viewport setters. Physics/effect factories use the
existing validated constructors. Other components and executable game behaviour
use application registration, without extending a central type switch. Register
the exact const shared-pointer type shown in the table; `std::any` deliberately
does not perform pointer conversions.

## Persistence and scope

For saved built-in definitions, variants and linked-instance propagation, use the
separate [prefab asset editor](prefab-editor.md) / `PrefabAsset` API. It reuses
scene codecs rather than serializing this factory API's `std::any` payloads.

Definitions/configurations are in-memory C++ authoring data. This API does not
serialize `std::any`, callbacks, a factory registry, definition links or overrides.
Constructed built-in scene instances use the existing
[scene archives](scene-archives.md), including shared-resource deduplication.
Unregistered custom subclasses remain rejected by archives; prefab registration
does not install a `ComponentCodecs` entry. Register archive codecs separately
when persisting custom components. Applications reconstruct their runtime
behaviours deliberately and choose any definition-file format or asset loading
policy. Resource loading/caching remains a separate service.

The module is portable and optional. It changes no browser adapter, renderer or
demo controls. Instantiation during a tick uses ordinary guarded additions; new
objects initialize/tick on the next update. Presentation and simulation remain
separate, and creating a visual prefab never initializes physics.

## Validation

`prefab_tests` covers nested ownership/placement, independent identities, shared
geometry/materials/textures/shapes/effects, morph/material/playback independence,
overrides, disabled state and tick defaults, lifecycle/cleanup, tick-time additions,
stale/foreign handles, registry versions/removal/reentrancy, rollback, budgets,
protected children and built-in instance archives. The same source runs through
the repository-local Emscripten toolchain under Node using `tests/wasm-prefab`.
See [validation commands and evidence](prefabs-validation.md) for actual results
and platform limits.
