# External code-plugin template

Configure this folder as a standalone CMake project with `WEBENGINE_SOURCE_DIR` pointing to WebEngine. It consumes only `WebEngine::Modules`, without renderer/physics dependencies. Copy `counter/`, rename the target/alias/namespace and replace `Registry` and `Counter` with domain-specific types. See [the contract](../../docs/modules.md).

Keep the public include folder and typed registration function. Declare dependencies/schema versions/capabilities in the descriptor. Install only into staging. Factories create independent session state; sessions keep the returned snapshot alive. Do not register in static constructors or add private engine include paths.

The root engine's optional `WEBENGINE_BUILD_MODULE_DEMO` builds the rendered browser or Windows hosts in this folder. The standalone project builds the console contract consumer, with `WEBENGINE_BUILD_MODULE_TESTS=ON` adding the shared tests. With `WEBENGINE_BUILD_MODULES=OFF`, only `module_disabled` is built, proving the feature remains optional.

For engine API details and exact validation commands see `docs/modules.md` and `docs/modules-validation.md` in WebEngine. No Paws headers, IDs, save formats or quest policy are referenced by this template.
