# Runner image for local `act` replays of .github/workflows/ci.yml — used by the
# orchestrator driver (orchestrator/driver.py) and manual runs (`act -j lint`).
#
# act's medium image ships gcc/python/git; this layers on the rest ci.yml wants
# from the GitHub-hosted ubuntu-latest runner.
#
# clang comes from apt.llvm.org pinned to 20: noble's default clang is 18, whose
# packaging ships no static ASan/UBSan runtime, so the clang-asan lane fails to
# link (libclang_rt.asan*.a missing). libclang-rt-20-dev provides those. Keeping
# the image and ci.yml on the same clang 20 means one toolchain covers all lanes.
#
# The graphics stack (SDL3 build deps + offscreen software GL) is baked in as a
# cached layer so ci.yml's "install graphics stack" step is a skipped no-op under
# `act` (it guards on dpkg): otherwise that step re-runs `apt-get update` + a
# fetch on EVERY lane of EVERY orchestrator iteration — a runtime step Docker
# never caches. On GitHub-hosted runners the packages are absent, so the ci.yml
# step still installs them there. Keep this list in sync with that step.
FROM catthehacker/ubuntu:act-latest

RUN . /etc/os-release \
 && curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
      -o /etc/apt/keyrings/llvm.asc \
 && echo "deb [signed-by=/etc/apt/keyrings/llvm.asc] http://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-20 main" \
      > /etc/apt/sources.list.d/llvm.list \
 && apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      cmake make ccache clang-20 libclang-rt-20-dev llvm-20 \
      libegl-dev libgles-dev libgl1-mesa-dri libgbm-dev libdrm-dev \
      libx11-dev libxext-dev libwayland-dev libxkbcommon-dev \
 && ln -s /usr/bin/clang-20 /usr/local/bin/clang \
 && ln -s /usr/bin/clang++-20 /usr/local/bin/clang++ \
 # ASan/UBSan auto-discover `llvm-symbolizer` on PATH; without it, sanitizer
 # reports come back as `<unknown module>` and are undebuggable. llvm-20 ships
 # the versioned binary — expose the unversioned name the runtime looks for.
 && ln -s /usr/bin/llvm-symbolizer-20 /usr/local/bin/llvm-symbolizer \
 && rm -rf /var/lib/apt/lists/*

# ci.yml's lint/coverage jobs pip install these per run; pre-installing makes
# those steps no-op resolves. PIP_BREAK_SYSTEM_PACKAGES matches the hosted
# runner's behaviour on noble's PEP-668 python.
ENV PIP_BREAK_SYSTEM_PACKAGES=1
RUN pip install --no-cache-dir clang-format==19.1.7 gcovr diff-cover
