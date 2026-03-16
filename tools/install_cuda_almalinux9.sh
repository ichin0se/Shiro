#!/usr/bin/env bash
set -euo pipefail

packages=(
  cuda-toolkit
  nvidia-sdk-optix
)

if [[ "${1:-}" == "--with-driver" ]]; then
  packages+=(
    kmod-nvidia-open-dkms
  )
fi

sudo dnf install -y "${packages[@]}"

cat <<'EOF'

CUDA toolkit and OptiX SDK installation completed.

Recommended shell setup:
  export PATH=/usr/local/cuda/bin:$PATH
  export LD_LIBRARY_PATH=/usr/local/cuda/lib64:${LD_LIBRARY_PATH:-}

Validation:
  nvcc --version
  nvidia-smi

If nvidia-smi still fails, the toolkit is installed but the NVIDIA driver or GPU visibility is not ready.
EOF
