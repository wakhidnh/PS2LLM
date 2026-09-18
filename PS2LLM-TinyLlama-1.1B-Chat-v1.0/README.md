# PS2LLM — Local LLM Inference Engine on PlayStation 2

PS2LLM is an experimental homebrew project designed to run language models natively on the Sony PlayStation 2's Emotion Engine (R5900) hardware using the **TinyLlama-1.1B-Chat-v1.0** architecture with Q8 full 22 layers and full dimension.

> **Disclaimer & Purpose:** This project is developed strictly for **educational, experimental, and entertainment purposes**. There is no practical commercial use case for running a 1.1-billion-parameter model on a 2000-era console; this exists purely for the joy and engineering challenge of pushing vintage hardware to its absolute limits.

---

## Support the Project

If you enjoyed seeing a billion-parameter language model run on vintage console hardware and want to support ongoing homebrew experimentation:

[![Ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/yourusername)

*All code, tools, and releases are 100% free and open-source. Donations are completely voluntary.*

---

## Key Features & Technical Overview

* **Target Hardware:** Sony PlayStation 2 (Emotion Engine R5900 @ 294.9 MHz, 32 MB RDRAM).
* **Model:** TinyLlama-1.1B-Chat-v1.0 (Q8 custom `.P2L` packed format).
* **Storage Optimization:** Bypasses legacy USB 1.1 bottlenecks by leveraging the Internal SATA/PATA HDD via Network Adapter, utilizing large block reads and optimized DMA modes for streaming layer data.
* **Optimized C Inference Pipeline:** Implements 32-way pointer unrolling, triple-buffered streaming, and aggressive compiler optimizations (`-O3 -flto -march=r5900 -mtune=r5900 -funroll-loops`) to maximize throughput on the 64-bit EE CPU core.
* **Dynamic Layer Chunk-Streaming:** Streams active 1.1B model weights dynamically on the fly to operate within the strict 32 MB RDRAM memory ceiling.

---

## Hardware Performance Overview (TinyLlama-1.1B Streaming)

Inference performance across different PlayStation 2 console revisions using TinyLlama-1.1B relies heavily on internal bus throughput and storage I/O handling:

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

## Low-Level Optimizations (EE, VU0, and GS)

* **Emotion Engine (R5900 CPU):** Hand-optimized 32-way pointer-unrolled GEMV loops and aggressive compiler configurations (`-O3 -flto -march=r5900`).
* **Vector Unit 0 (VU0 / MMI):** Leverages 128-bit Multimedia Instructions to parallelize quantization math and dot-product calculations.
* **Graphics Synthesizer (GS):**
  * **Direct Register Manipulation:** Interacts directly with GS hardware registers and local VRAM, bypassing bulky high-level display frameworks.
  * **VSync Double Buffering:** Prevents visual artifacting during real-time streaming updates.
  * **Embedded Bitmap Font Rendering:** Blits text straight to local video memory to keep the main CPU free for tensor math and disk I/O routines.
I haven't found a way to correctly use VU1 for this project.
---

## Project Structure

PS2LLM/
├── Makefile                # EE compilation configuration and optimization flags
├── MODEL.P2L               # Binary packed Q8 model weights generated from your PC (DIY)
├── PS2LLM.ELF              # Compiled final Homebrew executable
├── README.md               # Project documentation
├── src/                    # C Source Code & Headers for PS2 Runtime
│   ├── bpe.c               # Byte-Pair Encoding tokenizer core
│   ├── bpe.h               # Tokenizer header definitions
│   ├── config.h            # Model and system configuration parameters
│   ├── font.h              # GS bitmap font definitions
│   ├── gs_ui.c             # GS user interface rendering
│   ├── irx_data.h          # Embedded IRX module binary data buffers
│   ├── irx_loader.h        # IRX driver loading interface
│   ├── keyboard.c          # USB / PS2 keyboard scanning and input
│   ├── keyboard.h          # Keyboard definitions
│   ├── main.c              # Core application entry point and chat loop
│   ├── model.c             # Q8 GEMV kernels and streaming forward pass
│   ├── model.h             # Model state structs and declarations
│   ├── storage.c           # Low-level file system / internal SATA HDD access
│   ├── storage.h           # Storage driver interface
│   ├── storage_internal.h  # Internal storage implementation details
│   └── vocab.h             # Static token vocabulary definitions and lookup table
└── tools/                  # PC Python Exporter Scripts
    └── convert_llama_to_p2l.py # Exporter script for TinyLlama packaging

Before running inference on the PS2, you must convert and quantize your target model into the custom Q8 binary format (MODEL.P2L) using a clean Python environment on your host machine.

    Install Python & Create a Virtual Environment
    Ensure you have Python 3.10+ installed on your system. Open your terminal in the project root folder and set up a virtual environment:

# Create the virtual environment
python -m venv venv

# Activate the virtual environment
# On Linux / macOS:
source venv/bin/activate
# On Windows (Command Prompt):
# venv\Scripts\activate.bat
# On Windows (PowerShell):
# .\venv\Scripts\Activate.ps1

    Install Dependencies
    With your virtual environment active, upgrade pip and install the required machine learning libraries to handle model weights and tokenization:

python -m pip install --upgrade pip
pip install torch transformers numpy

    Run the Exporter Script
    Execute the packaging script inside the tools/ directory. This script pulls the source model from Hugging Face, quantizes its weight matrices down to 8-bit integer format (Q8), and compiles them into the sequential MODEL.P2L binary layout:

python tools/convert_llama_to_p2l.py

Once completed, the generated MODEL.P2L file will be ready for transfer to your PlayStation 2 internal hard drive.
Building and Running the Homebrew

    Prerequisites: Ensure you have the official PS2 Toolchain (ee-gcc, ps2sdk) installed on your development host.

    Compile the ELF:

make clean
make

    Deploy: Transfer PS2LLM.ELF and your generated MODEL.P2L weight file to your PlayStation 2 internal HDD environment (via OPL or LaunchELF).

    Execute: Launch the homebrew ELF on your console, type your prompts using an attached keyboard, and experience local AI generation!

Software, Models, and Open Source Licenses

To maintain complete transparency and respect community guidelines, all software components, toolchains, and model architectures utilized in this project are cataloged below with their respective licenses:

    PS2SDK: Homebrew Development Toolchain | PS2DEV Community | Academic Free License (AFL) / BSD

    Llama-based 1.1B Model Variants: Base Transformer Architecture | Meta AI / Open-Source Community | Llama Community License / Custom Open Source

    Python / PyTorch: Model Exporter & Quantization Tooling | Python Software Foundation / Meta | PSF License / BSD-style

    PS2LLM Engine (src/): Custom C Inference & UI Runtime | Wakhid Nurhidayat (@wakhidnh) | MIT / Open Source Homebrew License

Note again:
## This project is entirely non-commercial, educational, no specific use case and just for fun.
## Distributed freely to the PlayStation 2 homebrew community without any monetization.
## Support me for the the other homebrew project
## If you enjoyed seeing an LLM run on vintage 2000s console hardware and want to support further experimentation: