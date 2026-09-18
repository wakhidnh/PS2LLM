For internal HDD please run it from __system partition 

# PS2LLM — Local LLM Inference Engine on PlayStation 2

PS2LLM is an experimental open-source homebrew engine designed to run quantized language models natively on the Sony PlayStation 2's Emotion Engine (R5900) hardware using a streaming forward-pass architecture.

> **Disclaimer & Purpose:** This project is developed strictly for **educational, experimental, and entertainment purposes**. There is no practical commercial use case for running transformer models on a 2000-era console; this project exists purely for the joy and engineering challenge of pushing vintage console hardware to its absolute limits.

---

## ☕ Support the Project

If you enjoy seeing language models run on vintage console hardware and want to support ongoing homebrew experimentation:

[![Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/yourusername)

*All engine code, export tools, and binary releases are 100% free and open-source. Donations are completely voluntary.*

---

## 🚀 Key Features & Technical Overview

* **Target Hardware:** Sony PlayStation 2 (Emotion Engine R5900 @ 294.9 MHz, 32 MB RDRAM).
* **Storage-Driven Execution:** Bypasses legacy USB 1.1 throughput limits by streaming layer matrices directly from an internal SATA/PATA HDD via the Network Adapter, utilizing large sector block reads and direct DMA.
* **Optimized C Inference Pipeline:** Implements 32-way pointer unrolling, multi-buffered streaming, and aggressive compiler flags (`-O3 -flto -march=r5900 -mtune=r5900 -funroll-loops`) to maximize integer execution on the 64-bit EE core.
* **Model-Agnostic Format (`.P2L`):** Uses a custom sequential binary layout storing vocabulary, embedding vectors, quantized layer weights, and projection matrices designed for dynamic block streaming within the strict 32 MB RDRAM boundary.

---

## 📊 Hardware Performance Overview

Inference throughput depends heavily on internal bus latency, adapter bridge translation, and storage access patterns during continuous streaming loops:

### Comparative Storage Performance Matrix
* **Official Sony Network Adapter (Original PATA IDE + PATA HDD):**
  * *Behavior:* Provides stable, low-latency DEV9 bus transactions, minimizing bus collisions during streaming loops.
  * *Token Speed:* ~15 to 22 seconds per token.
* **GameStar SATA Network Adapter (Third-Party Clone + SATA HDD):**
  * *Behavior:* Bridge chip translation introduces slight pipeline latency during continuous high-volume read requests.
  * *Token Speed:* ~20 to 28 seconds per token (mitigated via 32 KB+ sector block read alignment).

### Console Revision Breakdown
* **SCPH-35000 / SCPH-37000 (Early V6 - V8, GH-010 / GH-019 / GH-022 boards):**
  * *Performance:* ~19 - 28 s/token (GameStar) / ~18 - 22 s/token (Official Sony Adapter).
* **SCPH-3900X Series (Stable V7 - V8, GH-022 boards):**
  * *Performance:* ~18 - 26 s/token (GameStar) / ~16 - 20 s/token (Official Sony Adapter).
* **SCPH-5000X / SCPH-5500X Series (V9 - V11, GH-023 / later boards):**
  * *Performance:* ~18 - 24 s/token (GameStar) / ~15 - 19 s/token (Official Sony Adapter).
Performance is very tentative and not same each test

---

## ⚙️ Low-Level Hardware Optimizations

* **Emotion Engine (R5900 CPU):** Hand-unrolled 32-way pointer kernels maximize ALU pipeline fill and reduce branch prediction overhead during inner GEMV dot products.
* **Vector Unit 0 (VU0 / MMI):** Utilizes 128-bit Multimedia Instructions (MMI) for SIMD acceleration of vector math and activation scaling.
* **Graphics Synthesizer (GS):**
  * **Direct Hardware Registers:** Bypasses high-level rendering libraries by communicating directly with GS registers and local VRAM.
  * **VSync Double Buffering:** Keeps screen updates tear-free during active token generation.
  * **VRAM Bitmap Font Blitting:** Renders characters directly from embedded VRAM textures, leaving CPU cycles dedicated to disk streaming and inference.

---

## 🛠️ Project Structure

```text
PS2LLM/
├── Makefile                # EE toolchain build flags and linker options
├── MODEL.P2L               # Active quantized model binary (streamed at runtime)
├── PS2LLM.ELF              # Compiled homebrew executable
├── README.md               # Documentation
├── src/                    # C Inference Engine & PS2 Runtime
│   ├── bpe.c               # Tokenizer implementation
│   ├── bpe.h               # Tokenizer definitions
│   ├── config.h            # Engine runtime parameters
│   ├── font.h              # GS bitmap font textures
│   ├── gs_ui.c             # GS UI rendering and console layout
│   ├── irx_data.h          # Embedded IRX driver binaries
│   ├── irx_loader.h        # Storage and input IRX initialization
│   ├── keyboard.c          # USB keyboard input routines
│   ├── keyboard.h          # Input handling definitions
│   ├── main.c              # Application entry point and chat loop
│   ├── model.c             # Layer streaming and GEMV kernels
│   ├── model.h             # Model architecture structures
│   ├── storage.c           # Low-level HDD access routines
│   ├── storage.h           # Storage driver interface
│   ├── storage_internal.h  # Internal storage structures
│   └── vocab.h             # Token lookup and vocabulary definitions
└── tools/                  # Model Exporter & Quantization Scripts
    └── convert_llama_to_p2l.py # Model conversion and .P2L packaging script

```

---

## 📦 Setting Up the Exporter & Preparing Models

To prepare a model for the PS2, use the Python exporter script on your host machine to convert source weights into the custom `.P2L` binary format.

### 1. Set Up Python Environment

Ensure you are using Python 3.10+:

```bash
# Create virtual environment
python -m venv venv

# Activate virtual environment
# Linux / macOS:
source venv/bin/activate
# Windows (Command Prompt):
# venv\Scripts\activate.bat
# Windows (PowerShell):
# .\venv\Scripts\Activate.ps1

```

### 2. Install Dependencies

```bash
python -m pip install --upgrade pip
pip install torch transformers numpy

```

### 3. Convert Your Model

Run the exporter tool, passing your target model repository or local checkpoint directory:

```bash
python tools/convert_llama_to_p2l.py --model <model-id-or-path> --output MODEL.P2L

```

The script extracts the architecture configuration, quantizes weight matrices to 8-bit format (Q8), builds the vocabulary table, and packages everything into the sequential `MODEL.P2L` file.

*Pre-converted `.P2L` binaries for select models can also be found on the [Releases page](https://www.google.com/search?q=../../releases&utm_source=gemini).*

---

## 🕹️ Building and Running on PS2

* **Prerequisites:** A working [PS2DEV](https://www.google.com/search?q=https://github.com/ps2dev/ps2dev&utm_source=gemini) environment with `ee-gcc` and `ps2sdk`.
* **Compile:**
```bash
make clean
make

```


* **Installation:**
1. Transfer `PS2LLM.ELF` and your desired `MODEL.P2L` file to your PS2's internal hard drive partition (using wLaunchELF, OPL, or HDL Batch Installer).
2. Ensure `MODEL.P2L` is placed in the designated path accessible by the homebrew storage loader.


* **Run:** Launch `PS2LLM.ELF`, connect a compatible USB keyboard, and enter your prompts.

---

## ⚖️ Licenses & Attribution

* **PS2LLM Engine (`src/`):** Released under the [MIT License](https://www.google.com/search?q=LICENSE&utm_source=gemini).
* **PS2SDK Toolchain:** Copyright (c) PS2DEV Community, licensed under the Academic Free License (AFL) / BSD.
* **Target Models:** Pre-quantized `.P2L` weight distributions inherit the original open-source licenses of their respective base models (e.g., Apache 2.0, Llama Community License, MIT). Check the release notes of each specific model download for upstream author credits and license documentation.

*This project is non-commercial, experimental, and developed for homebrew hardware preservation and research.*
```

```
