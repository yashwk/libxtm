# libxtm CI/CD — Workflows and Platform Policy

This document records how libxtm's CI/CD is organized and — more importantly — the
platform/compiler constraints that made the current setup the way it is. If CI
behavior and this document ever disagree, CI wins; update this file when the
workflows change.

---

## 1. Workflows

### `.github/workflows/ci.yml` — CI (push / PR)

Single `build` job on `ubuntu-latest`:

1. Install dependencies (`libgdal-dev ninja-build python3-dev`)
2. Configure (`Release`, Ninja, `XTM_BUILD_PYTHON=ON`)
3. Build, `ctest`
4. Python bindings smoke test
5. Encode & verify roundtrip on `data/canyons/grand_canyon_512.tif`
6. Upload binaries/artifacts

### `.github/workflows/release.yml` — Release artifacts (workflow_dispatch / release published)

Three jobs on `ubuntu-latest`:

- **`package`**:

  - **wheel**: `python3 -m pip wheel .` via the **scikit-build-core** backend
    (`[tool.scikit-build]` in pyproject.toml: `XTM_BUILD_PYTHON=ON`, flat
    install at the wheel root, `abi3` tag). CMake's `install()` destination is
    the wheel root when `SKBUILD` is defined, else `${CMAKE_INSTALL_LIBDIR}`.
    Then `auditwheel repair` bundles system `libgdal` (and its deps) into the
    wheel and re-tags it `manylinux_2_39_x86_64` — the runner's glibc 2.39
    sets the floor, so the wheel runs on Ubuntu 24.04+, Debian 12+, RHEL 9+
    and newer. Wider coverage (manylinux_2_17) would require cibuildwheel +
    a manylinux container (docker) — not set up.
  - **native**: configure with `-DBUILD_TESTING=OFF -DXTM_BUILD_PYTHON=OFF`,
    build, `cpack TGZ` + `DEB`
  - stage artifacts for the `publish` job

- **`conda-package`**: builds `recipe/meta.yaml` via `conda build` (bindings
  via `pip install .` in the build env; nanobind comes from conda-forge, see
  the `find_package(nanobind)` fallback in CMakeLists) and uploads the
  package to anaconda.org with `anaconda-client`. Users install with
  `conda install -c yashwk libxtm`. Requires the `ANACONDA_TOKEN` secret
  (anaconda.org API token with upload rights on your channel).

- **`publish`** (only on real release events): attach `dist/*` to the GitHub
  Release and publish **wheel + sdist** to PyPI with OIDC attestations.

### PyPI → conda via grayskull

The recipe is **regenerated from the PyPI release**, not written by hand:

1. Publish a GitHub release (`gh release create vX.Y.Z`) — publish job
   uploads `libxtm-X.Y.Z-cp312-abi3-manylinux_2_39_x86_64.whl` and
   `libxtm-X.Y.Z.tar.gz` to PyPI (requires trusted publishing configured on
   the PyPI project for this repo's `Release artifacts` workflow).
2. Regenerate the recipe:
   `grayskull pypi libxtm --version X.Y.Z -o recipe/`
   (parse pyproject `[project]` metadata — that's why `license`/`urls` live
   in pyproject.toml).
3. **Patch the generated recipe**: grayskull sees `dependencies = []` in
   pyproject and cannot know the C++ side links GDAL — add `libgdal` to
   `host` and `run` requirements manually.
4. Commit, then run the release workflow (`workflow_dispatch`) so the
   `conda-package` job builds and uploads it.

---

## 2. Compiler policy

CI is **Linux-only** and uses the runner's default toolchain (gcc + system
GDAL). CC/CXX are not pinned in the workflows.

| Context      | Compiler      | Bindings    | Notes                          |
| ------------ | ------------- | ----------- | ------------------------------ |
| CI / release | gcc (default) | ✅ nanobind | LTO on — links cleanly        |

Bindings are compiled with gcc + LTO — the combination that consistently
links cleanly (see §3 for the combinations that do not).

---

## 3. Why things are the way they are (pitfall log)

Historical record of platform issues (macOS/Windows CI was later dropped in
favor of the Linux-only matrix; the entries explain the constraints that were
hit and the CMakeLists guards still in place for local macOS/Windows builds):

### 3.1 nanobind + GCC on macOS is broken

Brew GCC on Apple Silicon fails to link the Python module (`undefined reference
to std::__cxx11::basic_string::basic_string(basic_string&&)` referenced from
nanobind's `wrap_move<...>` instantiations) — with or without LTO. Do not install
GCC on macOS CI; use the runner's AppleClang.

### 3.2 nanobind + MinGW is unsupported

The exact same `wrap_move` undefined-symbol failure occurs with MinGW GCC, and
is independent of:
- LTO on/off (`-flto=auto` / none)
- `-static-libgcc -static-libstdc++` (symbol not emitted in the static archive
  either — it is inline-only in the headers, and the compiler emits an
  out-of-line call nothing provides)

Consequence: `-DXTM_BUILD_PYTHON=OFF` on Windows everywhere (CI + release
wheels). Windows releases ship the native TGZ only. Revisit only via clang-cl /
MSVC-built GDAL (large lift, out of scope). The `-static-libstdc++` link option
was later removed from CMakeLists as ineffective.

### 3.3 LTO guards in CMakeLists.txt

`CMAKE_INTERPROCEDURAL_OPTIMIZATION` is only set when:
- IPO is supported by the toolchain, AND
- `CMAKE_BUILD_TYPE == Release`, AND
- NOT (Apple AND GNU compiler)
- NOT (MinGW AND GNU compiler)

### 3.4 `-march=native` on Apple Silicon GCC

GCC translates `-march=native -mtune=native` on Apple Silicon to
`-march=apple-m1`, which it rejects. CMakeLists guards: on Apple arm64 only
`-O3 -ftree-vectorize` is used. Not triggered with AppleClang (accepts native
flags), kept as a safety net for local GCC builds.

### 3.5 `#pragma GCC ivdep` vs clang + `-Werror`

AppleClang errors on it (`unknown pragma ignored -Werror,-Wunknown-pragmas`).
All occurrences in `src/` are wrapped in:

```cpp
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC ivdep
#endif
```

### 3.6 `std::filesystem::path` → `std::string` assignment

Assigning a `path` (e.g. result of `temp_directory_path() / name`) to a
`std::string` used to compile via libstdc++'s implicit conversion; **removed in
GCC 15+/libstdc++ 16** (MinGW 16.x rejects it). Always call `.string()`:

```cpp
temp_dir_ = (std::filesystem::temp_directory_path() / sub).string();
```

---

## 4. Misc operational notes

- `opencode.json` holds a local MCP GitHub token and is **untracked**.
  Push protection blocks any commit containing it — use precise `git add`
  paths, never `git add -A`.
- `concurrency: cancel-in-progress` on CI keeps a single run per branch;
  release.yml uses `cancel-in-progress: false` on purpose.
- repository rule: push protection is active on `fix/review`; secrets in
  commits are rejected server-side.