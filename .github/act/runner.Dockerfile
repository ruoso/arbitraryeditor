# Runner image for local `act` replays of .github/workflows/ci.yml — used by the
# orchestrator driver (orchestrator/driver.py) and manual runs (`act -j lint`).
#
# act's medium image ships gcc/python/git; this layers on the rest ci.yml wants
# from the GitHub-hosted ubuntu-latest runner. GUI build deps (SDL3/OpenGL/mesa/
# xvfb) for the headless-GL e2e lanes are added once foundation.app_shell needs
# them; the current skeleton needs only the C++ toolchain.
FROM catthehacker/ubuntu:act-latest

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      cmake make ccache clang \
 && rm -rf /var/lib/apt/lists/*

# ci.yml's lint/coverage jobs pip install these per run; pre-installing makes
# those steps no-op resolves. PIP_BREAK_SYSTEM_PACKAGES matches the hosted
# runner's behaviour on noble's PEP-668 python.
ENV PIP_BREAK_SYSTEM_PACKAGES=1
RUN pip install --no-cache-dir clang-format==19.1.7 gcovr diff-cover
