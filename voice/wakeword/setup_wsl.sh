#!/usr/bin/env bash
# One-time setup for training "Hey Dewy" on your own PC: Windows (inside WSL2 + Ubuntu) or Linux, with an NVIDIA GPU.
#
#   bash setup_wsl.sh
#
# It makes a folder (~/dewy-wakeword) with two Python environments and a start.sh that opens the notebook.
# Two environments because TensorFlow (trains the model) and PyTorch (makes the computer voices) can ask for
# different versions of the same NVIDIA libraries, and they fight when they share one.
#
# Takes about 15-30 minutes, mostly downloads. Safe to run again: it skips what is already done.
set -euo pipefail

WORK="${DEWY_WORK:-$HOME/dewy-wakeword}"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIPER_TAG="v2.0.0"   # newer versions of piper-sample-generator no longer have generate_samples.py

step() { printf '\n== %s\n' "$*"; }
die()  { printf '\nStopped: %s\n' "$*" >&2; exit 1; }

step "1/6  Checking the GPU"
if ! command -v nvidia-smi >/dev/null 2>&1; then
  die "nvidia-smi was not found. On Windows, install the latest NVIDIA driver from nvidia.com (it also covers WSL),
restart, and open Ubuntu again. Do NOT install a Linux NVIDIA driver inside Ubuntu."
fi
nvidia-smi --query-gpu=name,memory.total --format=csv,noheader || die "nvidia-smi can't see a GPU."

step "2/6  Checking Python"
command -v python3 >/dev/null || die "python3 is missing (sudo apt install python3)."
PYV="$(python3 -c 'import sys; print("%d.%d" % sys.version_info[:2])')"
case "$PYV" in
  3.10|3.11|3.12) echo "Python $PYV is fine." ;;
  *) die "Python $PYV is not supported here (need 3.10, 3.11 or 3.12). Ubuntu 24.04 comes with 3.12: run 'wsl --install -d Ubuntu-24.04' in PowerShell." ;;
esac

step "3/6  System packages (asks for your Ubuntu password)"
sudo apt-get update
sudo apt-get install -y python3-venv python3-dev build-essential git wget unzip ffmpeg libsndfile1

mkdir -p "$WORK"
cd "$WORK"
df -h . | tail -1
echo "(training needs roughly 50 GB free in this drive)"

step "4/6  Training environment (TensorFlow + microWakeWord)"
if [ ! -x venv/bin/python ]; then python3 -m venv venv; fi
# shellcheck disable=SC1091
source venv/bin/activate
pip install -U pip wheel
pip install "tensorflow[and-cuda]>=2.18"
# datasets 4 needs extra audio libraries that the notebook doesn't set up, so stay on 3.x
pip install "datasets<4" scipy tqdm jupyterlab ipywidgets tensorboard
pip install 'git+https://github.com/whatsnowplaying/audio-metadata@d4ebb238e6a401bb1a5aaaac60c9e2b3cb30929f'
[ -d microWakeWord ] || git clone https://github.com/kahrendt/microWakeWord
pip install -e ./microWakeWord
deactivate

step "5/6  Voice generator environment (PyTorch + Piper)"
if [ ! -x venv-piper/bin/python ]; then python3 -m venv venv-piper; fi
venv-piper/bin/pip install -U pip wheel
venv-piper/bin/pip install torch torchaudio piper-phonemize-cross==1.2.1 webrtcvad-wheels numpy
if [ ! -d piper-sample-generator ]; then
  git clone -b "$PIPER_TAG" https://github.com/rhasspy/piper-sample-generator
fi
MODEL=piper-sample-generator/models/en_US-libritts_r-medium.pt
if [ ! -s "$MODEL" ]; then
  wget -O "$MODEL" "https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt"
fi
# The voice model is a full PyTorch pickle, and PyTorch 2.6+ refuses to load those unless told it is trusted.
# It is: it comes from the official piper-sample-generator release.
sed -i 's/torch.load(model_path)/torch.load(model_path, weights_only=False)/' piper-sample-generator/generate_samples.py

step "6/6  Notebook and start script"
cp "$HERE/train_hey_dewy.ipynb" "$WORK/train_hey_dewy.ipynb"
# TensorFlow loads some NVIDIA libraries only while running and doesn't find the ones pip installed,
# so point it at them (and at WSL's own GPU driver library).
cat > "$WORK/gpu_env.sh" <<'EOF'
_here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_nv="$("$_here/venv/bin/python" -c "import glob,site; print(':'.join(glob.glob(site.getsitepackages()[0]+'/nvidia/*/lib')))")"
export LD_LIBRARY_PATH="$_nv:/usr/lib/wsl/lib:${LD_LIBRARY_PATH:-}"
EOF
# The voice generator (PyTorch) must NOT see the library path above: it would load TensorFlow's cuDNN
# instead of its own, and the two versions differ. This wrapper starts it with a clean library path.
cat > "$WORK/piper-python.sh" <<EOF
#!/bin/sh
unset LD_LIBRARY_PATH
exec "$WORK/venv-piper/bin/python" "\$@"
EOF
chmod +x "$WORK/piper-python.sh"
cat > "$WORK/start.sh" <<EOF
#!/usr/bin/env bash
# Opens the training notebook. Run:  bash ~/dewy-wakeword/start.sh   then open the link it prints in your browser.
cd "$WORK"
source venv/bin/activate
source "$WORK/gpu_env.sh"
export DEWY_PIPER_PYTHON="$WORK/piper-python.sh"
exec jupyter lab --no-browser --ServerApp.root_dir="$WORK"
EOF
chmod +x "$WORK/start.sh"

step "Does Python see the GPU?"
source "$WORK/gpu_env.sh"
venv/bin/python - <<'EOF' || true
import os
os.environ["TF_CPP_MIN_LOG_LEVEL"] = "2"
import tensorflow as tf
print("TensorFlow GPUs:", tf.config.list_physical_devices("GPU"))
EOF
"$WORK/piper-python.sh" -c "import torch; x = torch.randn(1, 4, 16, device='cuda'); torch.nn.Conv1d(4, 4, 3).cuda()(x); print('PyTorch GPU and cuDNN work: True')" || echo "PyTorch GPU check failed"

cat <<EOF

All set. To train:
  1. bash $WORK/start.sh
  2. Open the http://localhost:8888/lab?token=... link it prints (in your normal Windows browser).
  3. Open train_hey_dewy.ipynb and run the cells from the top. Skip the Colab steps.
Your files are in $WORK. From Windows, open them with:  explorer.exe $WORK
EOF
