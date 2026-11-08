struct Cloud { layer:vec4f,eye:vec4f,sun:vec4f,shadow:vec4f };
struct Light { positionType: vec4f, directionRange: vec4f, colorIntensity: vec4f, cone: vec4f };
struct Shadow { viewProjection: mat4x4f, options: vec4f, selection: vec4f }; // layer, depth bias, normal bias, texel size
struct Frame {
    viewProjection: mat4x4f, ambientCount: vec4f, lights: array<Light,8>,
    cameraEnvironment: vec4f, environmentOptions: vec4f,
    shadows: array<Shadow,4>, waterClock: vec4f, inverseViewProjection: mat4x4f, viewport: vec4f, extent: vec4f, viewForward: vec4f, reflectionViewProjection: mat4x4f, reflectionPlane: vec4f, reflectionOptions: vec4f, interactionOptions: vec4f, interactionData: array<vec4f,64>, fog:Fog,
    lightingZones: array<vec4f,4>, lightingZoneRotations: vec4f,
    reflectionProbe: array<vec4f,3>, cloud:Cloud,
};
