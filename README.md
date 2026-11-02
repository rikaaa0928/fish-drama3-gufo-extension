# Fish Audio Drama-3 Gufo Extension (`fish_drama3`)

Native C++20/HIP model extension for [Gufo](https://github.com/gufo-org/gufo) implementing **Fish Audio Drama-3 (`fishaudio/drama-3`)** text-to-speech and zero-shot voice cloning on AMD RDNA 3.5 (`gfx1151` / Strix Halo Radeon 8060S).

## Architecture & Optimizations

- **Slow-AR Hybrid MoE Backbone (40 layers)**:
  - 30 Gated DeltaNet (`linear_attention`) layers + 10 Full-Attention layers (`dim=2048`, 16 heads × 256 `head_dim`, 2 KV heads, Partial Rotary `0.25` + QK-Norm + Output Gate).
  - 512 routed experts (`top_k=10`, `moe_intermediate_size=1024`) + 1 shared expert per layer with native **FP8 (`F8_E4M3` + `weight_scale_inv`)** dequantization in registers.
  - Register-resident single-wave32 Gated DeltaNet decode kernel + full-GPU parallel MoE router.
- **Fast-AR Codebook Tower (13 layers)**:
  - Autoregressively predicts residual RVQ codebooks `1..9` (`dim=2048`, `intermediate_size=6144`).
- **Causal DAC Speech Codec (`44.1 kHz`, `hop_length=2048`, `21.53 Hz` frame rate)**:
  - Zero-Python direct C++ `codec.pth` (PyTorch ZIP archive) & `codec.safetensors` loader.
  - Asynchronous dual-stream GPU pipelining overlapping DAC waveform synthesis with the next token step (**`RTF = 0.844`**, faster than real-time).
- **Reference Prefix KV & Recurrent State Caching**:
  - Snapshots all 10 Full-Attention KV caches, 30 Gated DeltaNet recurrent states (`conv_state` + `ssm_state`), and 13 Fast-AR Tower KV caches in GPU memory after encoding a reference voice prompt, reducing warm TTFA on a 25.8s reference prompt from `6.55 s` to **`646 ms`** (`10.1x` speedup, `max_diff = 0`).

## Building with Gufo

### CMake (`GUFO_EXTENSION_DIRS`)

```bash
cmake -B build/gpu-release \
  -DENGINE_ENABLE_HIP=ON \
  -DCMAKE_HIP_ARCHITECTURES=gfx1151 \
  -DGUFO_EXTENSION_DIRS=/path/to/fish-drama3-gufo-extension
cmake --build build/gpu-release --target gufo fish_drama3_config_test fish_drama3_test -j
```

### Nix Flake

```bash
nix build
```

## Verification & Serving

Check extension registration:

```bash
./build/gpu-release/gufo diagnose --section extensions --json
```

Start the OpenAI-compatible HTTP + WebSocket TTS server:

```bash
./build/gpu-release/gufo serve tts \
  --model /path/to/drama-3 \
  --host 0.0.0.0 --port 8080
```
