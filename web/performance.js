'use strict';
// C++ gathers frame samples; this shell updates at most four times per second.
(() => {
  let latest = null;
  const history = [], assets = [];
  let activeAsset = null;
  const fields = Object.fromEntries([...document.querySelectorAll('[data-perf]')].map(node => [node.dataset.perf, node]));
  const number = value => Number.isFinite(value) ? value.toFixed(2) : '—';
  const mib = bytes => `${number(bytes / 1048576)} MiB`;
  const show = (key, value) => { if (fields[key]) fields[key].textContent = value; };
  const timing = value => value ? `${number(value.mean)} / ${number(value.p95)} ms` : '—';
  function drawAsset(asset) {
    if (!asset) return;
    show('asset', `${asset.name} · ${asset.success ? 'loaded' : 'failed'}`);
    show('assetRead', `${number(asset.readMs)} ms`);
    show('assetCopy', `${number(asset.copyMs)} ms`);
    show('assetParse', asset.native ? `${number(asset.native.parseMs)} ms` : '—');
    show('assetDecode', asset.native ? `${number(asset.native.decodeMs)} ms` : '—');
    show('assetConvert', asset.native ? `${number(asset.native.convertMs)} ms` : '—');
    show('assetOptimize', asset.native ? `${number(asset.native.optimizeMs)} ms` : '—');
    show('assetVertices', asset.native?.success ? `${asset.native.inputVertices.toLocaleString()} / ${asset.native.outputVertices.toLocaleString()}` : '—');
    show('assetFit', asset.native ? `${number(asset.native.fitMs)} ms` : '—');
    show('assetTotal', `${number(asset.wallMs)} ms`);
  }
  window.engineAssetMetrics = metrics => { if (activeAsset) activeAsset.native = metrics; };
  window.EnginePerformance = {
    beginAsset(name, kind) {
      activeAsset = { name, kind, startedAt: performance.now(), readMs: 0, copyMs: 0 };
      return activeAsset;
    },
    completeAsset(asset, success) {
      if (!asset) return;
      const completed = { ...asset, success: Boolean(success), wallMs: performance.now() - asset.startedAt, completedAt: new Date().toISOString() };
      delete completed.startedAt;
      assets.push(completed); if (assets.length > 20) assets.shift();
      if (activeAsset === asset) activeAsset = null;
      drawAsset(completed);
    },
    snapshot() {
      return {
        schemaVersion: 1, capturedAt: new Date().toISOString(), browser: navigator.userAgent,
        scene: document.querySelector('#scene-description').textContent,
        shipStress: window.__shipStressSnapshot ? structuredClone(window.__shipStressSnapshot) : null,
        detailSettings: window.engineDetailSettings?.(),
        rendering: window.EngineQuality?.snapshot(),
        paused: document.querySelector('#pause').getAttribute('aria-pressed') === 'true',
        definitions: {
          intervalMs: 'Submitted frame callback interval; includes browser scheduling and stalls. First interval is omitted.',
          cpuMs: 'CPU simulation/host update and renderer work; excludes inspector processing and GPU execution.',
          prepareMs: 'Uniform preparation, cache maintenance, CPU mip generation, and queue write calls.',
          resourceMs: 'Scene mesh/texture creation, CPU mip generation and queue writes; subset of prepareMs. GUI preparation runs during encodeMs.',
          submitMs: 'CPU time calling queue.submit, not GPU execution time.',
          submittedInstances: 'Submitted mesh material instances and particles; batching reduces drawCalls while retaining instance and triangle counts.',
          particles: 'Visible particle instances this frame. Particle draw calls obey alpha order; contiguous compatible particles are instanced.',
          particleUploadBytes: 'Changed 64-byte billboard records uploaded to the pooled GPU vertex buffer; unchanged paused frames do not rewrite them.',
          instancedDrawCalls: 'Draw calls submitting more than one instance. maxInstancesPerDraw is the largest batch this frame.',
          boneUploadBytes: 'GPU skinning palette writes (model and inverse-transpose normal matrices per binding); only changed palettes upload. Vertex deformation runs in the vertex shader; clip evaluation/blending runs in C++.',
          gpu: 'Sum of directional shadow maps plus Background, World and Overlay timestamp durations, sampled at most 10 Hz. Excludes GUI, transfers, queue wait, presentation and readback; may be quantized.',
          gui: 'GUI draw calls, triangles and queue writes are included in totals and also exported separately as guiDrawCalls, guiTriangles, guiUploadCalls and guiUploadBytes. GUI GPU texture/buffer payload is included in memory estimates; font source bytes, mutable atlas pages and widget metadata are excluded. GUI layout, text rasterization and command preparation run during encodeMs.',
          memory: 'CPU vertex/index/skin/image/environment payload and nominal GPU buffer/texture/depth payload estimates; excludes skeleton/clip metadata, pose scratch, allocator, driver and presentation overhead. Wasm is linear-memory capacity, not live allocation.',
          frameStorageBytes: 'Reserved CPU vector payload for reusable scene draws, lights, layer lists/batches, instance staging and dirty ranges; excludes asset data, per-node caches and container overhead.',
          frameStorageGrowths: 'CPU frame vector capacity growth events this frame; totalFrameStorageGrowths counts events since reset. This is not a general heap-allocation counter.',
          objectBufferGrowths: 'GPU instance buffer creations/growths this frame; totalObjectBufferGrowths counts events since reset. Other GPU resources are excluded.',
          changedObjects: 'Instance slots requiring upload; includes all active slots when the GPU object buffer is recreated. Adjacent slots are coalesced.',
          objectUploadBytes: 'Actual instance write bytes, including inter-slot padding; final trailing padding is omitted. totalObjectUploadBytes counts bytes since reset.',
          transformBuilds: 'Local/world matrices and mesh normal matrices rebuilt during scene collection. Camera inverse caching outside collection is excluded.',
        },
        latest: latest ? structuredClone(latest) : null,
        history: structuredClone(history), assets: structuredClone(assets),
        timeline: window.EngineTimeline?.snapshot() || null,
      };
    },
  };
  window.enginePerformance = snapshot => {
    snapshot.wasmCapacityBytes = window.Module?.HEAPU8?.byteLength || 0;
    latest = snapshot;
    history.push({ atMs: performance.now(), ...structuredClone(snapshot) });
    if (history.length > 240) history.shift();
    const t = snapshot.timings, c = snapshot.current;
    show('cpu', timing(t.cpuMs)); show('interval', timing(t.intervalMs));
    show('update', timing(t.updateMs)); show('collect', timing(t.collectMs));
    show('prepare', timing(t.prepareMs)); show('encode', timing(t.encodeMs)); show('submit', timing(t.submitMs));
    document.querySelector('#detail-cost').textContent=`Camera LOD parts: ${c.cameraLodParts ?? 0} · Shadow LOD parts: ${c.shadowLodParts ?? 0} · Culled caster parts: ${c.shadowCulledParts ?? 0} · Shadow triangles: ${(c.shadowTriangles ?? 0).toLocaleString()}`;
    show('draws', c.drawCalls.toLocaleString()); show('triangles', c.triangles.toLocaleString());
    show('instances', `${c.submittedInstances} / ${c.instancedDrawCalls} · max ${c.maxInstancesPerDraw}`);
    show('skeletons', `${c.skinnedInstances} / ${c.cachedSkeletons}`);
    show('boneUploads', `${c.boneUploadCalls} / ${c.boneUploadBytes} B`);
    show('particles', `${c.particles} / ${c.particleDrawCalls}`);
    show('particleUploads', `${c.particleUploadCalls} / ${c.particleUploadBytes} B`);
    show('uploads', `${c.uploadCalls} · ${(c.uploadBytes / 1024).toFixed(1)} KiB/frame`);
    show('uploadTotal', mib(snapshot.totalUploadBytes)); show('resource', `${number(snapshot.lastResourceMs)} ms`);
    show('cache', `${c.cachedMeshes} meshes · ${c.cachedTextures} textures`);
    show('cpuMemory', mib(c.cpuAssetBytes)); show('gpuMemory', mib(c.gpuPayloadBytes));
    show('frameMemory', mib(c.frameStorageBytes));
    show('frameGrowth', `${c.frameStorageGrowths} / ${c.objectBufferGrowths}`);
    show('frameGrowthTotal', `${snapshot.totalFrameStorageGrowths} / ${snapshot.totalObjectBufferGrowths}`);
    show('changedObjects', `${c.changedObjects} / ${c.submittedInstances}`);
    show('objectUploads', `${c.objectUploadCalls} / ${c.objectUploadBytes} B`);
    show('frameUploads', `${c.frameUploadCalls} / ${c.frameUploadBytes} B`);
    show('transformBuilds', `${c.localTransformBuilds} / ${c.worldTransformBuilds} / ${c.normalTransformBuilds}`);
    show('wasmMemory', mib(snapshot.wasmCapacityBytes)); show('samples', `${snapshot.windowSamples} / ${snapshot.windowCapacity}`);
    const gpu = snapshot.gpu;
    show('gpu', gpu.state === 'enabled' ? (gpu.totalMs ? timing(gpu.totalMs) : 'Waiting for sample…') : `Unavailable (${gpu.state})`);
    show('gpuLayers', gpu.state === 'enabled' && gpu.totalMs ? gpu.layersMs.map(layer => number(layer.mean)).join(' / ') + ' ms' : '—');
    show('gpuShadows', gpu.state === 'enabled' && gpu.shadowMs ? timing(gpu.shadowMs) : '—');
    show('gpuSamples', `${gpu.windowSamples} samples${gpu.ageMs === null ? '' : ` · ${Math.round(gpu.ageMs)} ms old`}`);
    show('build', snapshot.build);
    document.body.dataset.performance = 'ready';
    document.querySelector('#export-performance').disabled = false;
    document.querySelector('#reset-performance').disabled = false;
  };
  document.querySelector('#reset-performance').addEventListener('click', () => {
    window.Module._engine_reset_metrics(); history.length = 0; latest = null;
    document.body.dataset.performance = 'resetting';
    document.querySelector('#export-performance').disabled = true;
  });
  document.querySelector('#export-performance').addEventListener('click', () => {
    const report = window.EnginePerformance.snapshot();
    const blob = new Blob([JSON.stringify(report, null, 2)], { type: 'application/json' });
    const url = URL.createObjectURL(blob), link = document.createElement('a');
    link.href = url; link.download = `webengine-performance-${Date.now()}.json`;
    link.click(); setTimeout(() => URL.revokeObjectURL(url), 1000);
  });
})();
