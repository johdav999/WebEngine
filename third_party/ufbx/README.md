# ufbx

Vendored without source modifications from [ufbx v0.23.1](https://github.com/ufbx/ufbx/tree/26a482ae66871d7de36eb722aa060bce95bce274).

Commit: `26a482ae66871d7de36eb722aa060bce95bce274`.

SHA-256:

- `ufbx.c`: `4a956c26a708e40d82ecb0aeaee54da709d5ba7c144c9642174970059bda7e1d`
- `ufbx.h`: `f787be529af577efc04f3e3e735844a08556718dbb613d1211c2b313d72895dc`

The upstream dual MIT/public-domain license is in `LICENSE`. WebEngine uses the MIT option and includes its license in browser distributions.

CMake disables unused animation/evaluation, subdivision, tessellation, geometry-cache, OBJ, and stdio functionality. The engine loads local bytes through `ufbx_load_memory()`; native path loading is handled by the engine wrapper.
