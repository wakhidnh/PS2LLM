import struct
import torch
from transformers import AutoModelForCausalLM, AutoTokenizer

MODEL_NAME = "TinyLlama/TinyLlama-1.1B-Chat-v1.0"
OUTPUT_FILE = "MODEL.P2L"

FULL_DIM = 2048
FULL_INTERMEDIATE = 5632
TARGET_LAYERS = 22
VOCAB_SIZE = 32000
MAGIC = 0x4D4C3250  # "P2LM"
VERSION = 8         # Q8 Full Dim, 22-Layer O-Proj Composite

print(f"Loading {MODEL_NAME}...")
tokenizer = AutoTokenizer.from_pretrained(MODEL_NAME)
model = AutoModelForCausalLM.from_pretrained(
    MODEL_NAME, 
    torch_dtype=torch.float32, 
    low_cpu_mem_usage=True
)

state_dict = model.state_dict()

def quantize_q8(tensor):
    tensor = tensor.float()
    max_val = tensor.abs().max().item()
    if max_val == 0:
        max_val = 1.0
    scale = max_val / 127.0
    q = torch.clamp(torch.round(tensor / scale), -128, 127).to(torch.int8)
    uint8_data = (q.numpy().astype(int) + 128).astype("uint8")
    return uint8_data.tobytes(), scale

print(f"Packing Q8 Full-Dimension {TARGET_LAYERS}-Layer Composite MODEL.P2L...")
with open(OUTPUT_FILE, "wb") as f:
    # 1. Header (32 bytes)
    header = struct.pack(
        "<IIIIIIII",
        MAGIC,
        VERSION,
        FULL_DIM,
        TARGET_LAYERS,
        32,   # n_heads
        4,    # n_kv_heads
        FULL_INTERMEDIATE,
        VOCAB_SIZE
    )
    f.write(header)

    # 2. Tokenizer Vocab String Table (32 bytes per token)
    print("Writing vocabulary strings...")
    f.write(struct.pack("<I", VOCAB_SIZE))
    for i in range(VOCAB_SIZE):
        token_str = tokenizer.decode([i])
        raw_bytes = token_str.encode("utf-8", errors="ignore")[:31]
        padded = raw_bytes.ljust(32, b'\0')
        f.write(padded)

    # 3. Full Token Embeddings: [VOCAB_SIZE, 2048] Q8 (~65.5 MB)
    print("Exporting full embedding table (Q8)...")
    emb = state_dict["model.embed_tokens.weight"][:VOCAB_SIZE, :FULL_DIM].contiguous()
    emb_bytes, _ = quantize_q8(emb)
    f.write(emb_bytes)

    # 4. 22 Transformer Layers using o_proj (Attention Output Mix): [2048, 2048] Q8 (4 MB per layer)
    print(f"Exporting all {TARGET_LAYERS} attention output layers (o_proj)...")
    for l in range(TARGET_LAYERS):
        # o_proj mixes the attention heads back into the hidden dimension
        o_proj = state_dict[f"model.layers.{l}.self_attn.o_proj.weight"][:FULL_DIM, :FULL_DIM].contiguous()
        o_bytes, _ = quantize_q8(o_proj)
        f.write(o_bytes)

    # 5. Full LM Head: [VOCAB_SIZE, 2048] Q8 (~65.5 MB)
    print("Exporting full-dimension LM head projection (Q8)...")
    if "lm_head.weight" in state_dict:
        lm_head = state_dict["lm_head.weight"][:VOCAB_SIZE, :FULL_DIM].contiguous()
    else:
        lm_head = emb
    lm_bytes, _ = quantize_q8(lm_head)
    f.write(lm_bytes)

print(f"SUCCESS: Generated {OUTPUT_FILE} (~{len(open(OUTPUT_FILE, 'rb').read()) / (1024*1024):.2f} MB)")