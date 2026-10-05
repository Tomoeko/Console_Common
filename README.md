# Console Common

First-party C support, resource decoding, rendering contracts, GLES2 and Metal backends.

```sh
cmake -S . -B Files/build
cmake --build Files/build --parallel
```

Include headers from `console_common/`; link `console_common`.
Use `cc_add_platform(target metal)` or `cc_add_platform(target gles2)` for a backend.

The indexed renderer supports fixed offscreen targets and ordered passes within a
frame. Metal supports RGBA8 and RGBA16Float; the core GLES2 path supports RGBA8 and
explicitly rejects half-float targets. Prepare targets, pipelines and command
capacity outside frames. Target textures are borrowed from their target owner.
Passes can explicitly request independent top-left scissors and signed, extended
viewports. Attachment clipping preserves those requests; viewport requests beyond
GLES2's reported dimensions fail before changing native pass state.

CC0-1.0. See [LICENSE](LICENSE).
