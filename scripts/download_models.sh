#!/usr/bin/env bash
# Downloads the Demucs v4 model files used by Stem Splitter.
#
#   scripts/download_models.sh [destination] [4s] [6s] [ft]
#
# Default destination: the per-user model folder the app looks in.
# Default models: 4s and 6s. "ft" adds the four fine-tuned models used by "4 Stems HQ" (~324 MB).
set -euo pipefail

BASE_URL="${STEMSPLITTER_MODEL_BASE_URL:-https://huggingface.co/datasets/Retrobear/demucs.cpp/resolve/main}"

if [[ "$(uname)" == "Darwin" ]]; then
  DEFAULT_DEST="$HOME/Library/Application Support/Stem Splitter/Models"
else
  DEFAULT_DEST="$HOME/.local/share/StemSplitter/models"
fi

DEST="${1:-$DEFAULT_DEST}"
shift || true
WHICH=("$@")
[[ ${#WHICH[@]} -eq 0 ]] && WHICH=(4s 6s)

files=()
for w in "${WHICH[@]}"; do
  case "$w" in
    4s) files+=("ggml-model-htdemucs-4s-f16.bin") ;;
    6s) files+=("ggml-model-htdemucs-6s-f16.bin") ;;
    ft) files+=("ggml-model-htdemucs_ft_drums-4s-f16.bin" "ggml-model-htdemucs_ft_bass-4s-f16.bin"
                "ggml-model-htdemucs_ft_other-4s-f16.bin" "ggml-model-htdemucs_ft_vocals-4s-f16.bin") ;;
    *) echo "Unknown model '$w' (use 4s, 6s or ft)" >&2; exit 1 ;;
  esac
done

mkdir -p "$DEST"

for f in "${files[@]}"; do
  if [[ -s "$DEST/$f" ]]; then
    echo "✓ $f (already present)"
    continue
  fi
  echo "↓ $f"
  curl -fL --retry 4 --retry-delay 3 --progress-bar -o "$DEST/$f.download" "$BASE_URL/$f"
  # Sanity check: Demucs ggml files start with the magic "4cmd"/"6cmd" (little-endian dmc4/dmc6)
  magic=$(head -c 4 "$DEST/$f.download" | od -An -tx1 | tr -d ' \n')
  if [[ "$magic" != "34636d64" && "$magic" != "36636d64" ]]; then
    echo "✗ $f doesn't look like a Demucs model (magic $magic)" >&2
    rm -f "$DEST/$f.download"
    exit 1
  fi
  mv "$DEST/$f.download" "$DEST/$f"
done

echo "Models are in: $DEST"
