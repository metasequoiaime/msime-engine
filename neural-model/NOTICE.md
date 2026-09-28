# chinese-ime-lm sentence models

The two neural sentence models shipped here are from the **chinese-ime-lm** project
(<https://github.com/metasequoiaime/chinese-ime-lm>), release `model-v1`:

- `sentence-model.safetensors` — keyboard preset.
- `sentence-model-desktop.safetensors` — desktop preset.

Both are character-level Transformer language models used to rerank pinyin whole-sentence candidates.

## License

The model weights and the C++ inference code in `neural/` are licensed under Apache-2.0.

## Attribution

The models were trained on the Chinese portion of C4 (ODC-BY) and LCCC (MIT).
