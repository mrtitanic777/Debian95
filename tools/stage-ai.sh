#!/bin/bash
# Stage "AI Clippy"'s offline brain into the image: a llama.cpp engine (llama-cli,
# built inside the bookworm chroot so it matches the image's glibc) + a small
# instruct model (SmolLM2-360M, ~260MB). Failure-tolerant and CACHED: if the model
# or the build can't be fetched, the image simply ships without AI and Clippy says
# so. Build tools are purged afterwards so the image stays lean.
#   tools/stage-ai.sh [arch]
set -uo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
ARCH="${1:-amd64}"
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$HOME/debian95/debian-$ARCH"; RD="$OUT/rootdir"
[ -d "$RD" ] || { echo "stage-ai: no rootdir ($RD)"; exit 0; }
AI="$RD/usr/share/debian95/ai"; mkdir -p "$AI"
CACHE="$OUT/ai-cache"; mkdir -p "$CACHE"

# the one-shot generation wrapper the shell calls
install -Dm755 "$REPO/tools/debian95-ai.sh" "$RD/usr/local/bin/debian95-ai"

# --- 1. the model (cached) ---
MODEL="$AI/model.gguf"
if [ ! -f "$MODEL" ]; then
  if [ -f "$CACHE/model.gguf" ]; then cp -f "$CACHE/model.gguf" "$MODEL"
  else
    URL="https://huggingface.co/unsloth/SmolLM2-360M-Instruct-GGUF/resolve/main/SmolLM2-360M-Instruct-Q4_K_M.gguf"
    echo "  AI: downloading the model..."
    if wget -q -O "$CACHE/model.gguf.tmp" "$URL" && [ "$(stat -c%s "$CACHE/model.gguf.tmp" 2>/dev/null || echo 0)" -gt 100000000 ]; then
      mv -f "$CACHE/model.gguf.tmp" "$CACHE/model.gguf"; cp -f "$CACHE/model.gguf" "$MODEL"
    else echo "  AI: model download failed"; rm -f "$CACHE/model.gguf.tmp"; fi
  fi
fi

# --- 2. the engine (llama-cli), built in the chroot (cached) ---
if [ ! -x "$RD/usr/local/bin/llama-cli" ] && [ -f "$MODEL" ]; then
  if [ -x "$CACHE/llama-cli" ]; then install -Dm755 "$CACHE/llama-cli" "$RD/usr/local/bin/llama-cli"
  else
    echo "  AI: building llama.cpp in the chroot (one-time)..."
    rm -rf "$RD/root/llama.cpp"
    if git clone --depth 1 https://github.com/ggml-org/llama.cpp "$RD/root/llama.cpp" >/dev/null 2>&1; then
      printf 'nameserver 1.1.1.1\nnameserver 8.8.8.8\n' > "$RD/etc/resolv.conf"
      echo 'APT::Sandbox::User "root";' > "$RD/etc/apt/apt.conf.d/00sandbox-off"
      unshare --user --map-root-user --mount --pid --fork --mount-proc chroot "$RD" /bin/bash -c '
        set -e; export DEBIAN_FRONTEND=noninteractive PATH=/usr/sbin:/usr/bin:/sbin:/bin
        apt-get update -o APT::Sandbox::User=root -qq
        apt-get install -y --no-install-recommends -o APT::Sandbox::User=root build-essential cmake libgomp1
        cd /root/llama.cpp
        # NOTE: tools/cli (the llama-cli executable) is gated behind LLAMA_BUILD_SERVER
        # in current llama.cpp, so that must be ON even though we never ship the server.
        cmake -B build -DGGML_NATIVE=OFF -DLLAMA_CURL=OFF -DBUILD_SHARED_LIBS=OFF \
              -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_SERVER=ON >/dev/null 2>&1
        cmake --build build --target llama-cli -j"$(nproc)" >/dev/null 2>&1
        bin=$(find build -type f -name llama-cli | head -1)
        [ -n "$bin" ] && install -Dm755 "$bin" /usr/local/bin/llama-cli
        # reclaim space: drop the build toolchain (keep libgomp1 - llama-cli needs it)
        apt-get purge -y -o APT::Sandbox::User=root build-essential cmake >/dev/null 2>&1 || true
        apt-get autoremove -y -o APT::Sandbox::User=root >/dev/null 2>&1 || true
        apt-get clean
      '
      [ -x "$RD/usr/local/bin/llama-cli" ] && cp -f "$RD/usr/local/bin/llama-cli" "$CACHE/llama-cli" || echo "  AI: llama-cli build failed"
      rm -rf "$RD/root/llama.cpp"
    else echo "  AI: llama.cpp clone failed"; fi
  fi
fi

if [ -x "$RD/usr/local/bin/llama-cli" ] && [ -f "$MODEL" ]; then
  echo "  AI: Clippy's brain installed ($(du -h "$MODEL" | cut -f1) model)."
else
  echo "  AI: Clippy ships without a brain (optional - the UI still works)."
fi
