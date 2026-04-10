# Multi-Agent LLM Orchestration for Accessible Education

## Overview

This repository implements a multi-agent framework for generating accessible HTML interfaces from educational texts and user cognitive profiles. The system orchestrates three specialized agents:

- **TextAnalyzer** - Computes readability metrics (Flesch-Kincaid, Coleman-Liau, Dale-Chall, SMOG)
- **FeedbackAgent** - Extracts cognitive profiles from dialog history
- **InterfaceGenerator** - Produces WCAG-compliant HTML with personalized accessibility features

## Requirements

- NVIDIA GPU with 8GB+ VRAM
- CMake 3.15+
- C++17 compiler
- llama.cpp with GGUF model support

## Models Tested

| Model | Params | Quantization |
|-------|--------|--------------|
| Phi-3-mini-4k | 4B | NF4 |
| CodeLlama-7B | 7B | NF4 |
| Qwen2.5-1.5B | 1.5B | NF4 |
| Llama-3.2-1B | 1B | NF4 |
| Gemma-3-1B | 1B | NF4 |

## Dataset

- **Texts**: 303 documents from W3C WAI, WAI technical specs, and WebAIM (2.6M characters)
- **Profiles**: 100 synthetic user profiles based on DSM-5-TR criteria (dyslexia, ADHD, ASD, comorbid)

## Key Results

- Multi-agent framework outperforms single-LLM baseline across all models
- Phi-3-mini achieved highest success rate: 65% (multi-agent) vs 52% (single-LLM)
- Cognitive features: 3.0/5 | WCAG compliance: 37.5%
- 1B models: 3-4x faster, proportionally lower quality

## Citation

If you use this code, please cite:
