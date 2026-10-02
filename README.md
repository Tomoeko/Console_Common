# Console Common

First-party C support, resource decoding, rendering contracts, GLES2 and Metal backends.

```sh
cmake -S . -B Files/build
cmake --build Files/build --parallel
```

Include headers from `console_common/`; link `console_common`.
Use `cc_add_platform(target metal)` or `cc_add_platform(target gles2)` for a backend.

CC0-1.0. See [LICENSE](LICENSE).
