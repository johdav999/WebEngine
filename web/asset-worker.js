// One isolated Wasm heap per active decode; no SharedArrayBuffer/COOP/COEP required.
importScripts('asset_decode.js');
const runtime = createAssetDecoder();
self.onmessage = async ({data: job}) => {
  try {
    const module = await runtime;
    const response = await fetch(job.url);
    if (!response.ok) throw new Error(`Asset HTTP status ${response.status}`);
    const total = Number(response.headers.get('Content-Length')) || 0;
    if (total > job.maxBytes) throw new Error('Asset response exceeds byte limit');
    const reader = response.body.getReader(), chunks = [];
    let size = 0;
    for (;;) {
      const {done, value} = await reader.read();
      if (done) break;
      size += value.length;
      if (size > job.maxBytes) { await reader.cancel(); throw new Error('Asset response exceeds byte limit'); }
      chunks.push(value); self.postMessage({progress: size, total});
    }
    if (!size) throw new Error('Empty asset response');
    const pointer = module._malloc(size);
    if (!pointer) throw new Error('Asset input allocation failed');
    try {
      let offset = pointer;
      for (const chunk of chunks) { module.HEAPU8.set(chunk, offset); offset += chunk.length; }
      const result = module._engine_decode_asset(pointer, size, job.kind, job.maxResultBytes);
      if (!result) throw new Error(module.UTF8ToString(module._engine_decode_error()));
      const output = module.HEAPU8.slice(result, result + module._engine_decode_size());
      self.postMessage({result: output.buffer}, [output.buffer]);
    } finally { module._free(pointer); }
  } catch (error) { self.postMessage({error: String(error)}); }
};
