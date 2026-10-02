# c_core — hybrid graph core in C

The pure graph/analysis algorithms of `code-review-graph` live here and are
exposed to Python through ctypes. Everything else (CLI, MCP, resolvers,
embeddings, eval, visualization) stays in Python; see `PLANO_PORTAGEM_C.md`.

## Layout

```text
c_core/
  include/code_review_graph/  public C API (.h)
  src/                        implementation (.c)
  third_party/unity/          vendored Unity test framework
  CMakeLists.txt              local build only (no CI wiring)
```

## Build and test (local)

```bash
cmake -S c_core -B c_core/build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build c_core/build -j
ctest --test-dir c_core/build --output-on-failure
```

Options: `CRG_BUILD_SHARED` (default ON), `CRG_BUILD_TESTS` (default ON),
`CRG_SANITIZE` (default OFF).

## Sanitizers

`CRG_SANITIZE=ON` builds with AddressSanitizer + UndefinedBehaviorSanitizer.
The toolchain's gcc does not ship the shared sanitizer runtimes on this
machine, so pass clang explicitly:

```bash
export PATH="/home/linuxbrew/.linuxbrew/opt/llvm/bin:$PATH"
cmake -S c_core -B c_core/build-asan -DCMAKE_C_COMPILER=clang \
      -DCMAKE_BUILD_TYPE=Debug -DCRG_SANITIZE=ON
cmake --build c_core/build-asan -j
ctest --test-dir c_core/build-asan --output-on-failure
```

## Conventions

- C11, `-Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wformat=2`.
- Only `CRG_API`-marked symbols are exported; everything else stays hidden.
- Every allocation is checked, freed on all paths including errors, and has a
  documented owner.
- Strings cross the boundary as `(const char *, size_t)` pairs; never rely on
  NUL alone, and never use `strcpy`/`strcat`/`atoi`.

## Backend selection from Python

```bash
CRG_BACKEND=python pytest -q   # reference path (default)
CRG_BACKEND=c      pytest -q   # C path, per module once parity is green
```

```python
from code_review_graph import backend

backend.set_backend("c")
backend.set_backend(None)   # back to the environment default
```

`CRG_C_CORE_LIB` overrides library discovery with an explicit path; otherwise
the loader searches `c_core/build/`, `c_core/build/lib*/` and `build/`.
