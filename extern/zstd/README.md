# Zstandard decoder dependency

Pinned upstream: facebook/zstd **v1.5.7**, commit
`f8745da6ff1ad1e7bab384bd1f9d742439278e99` (tag object
`ac66b19e6bd6b83238bf008eecc1298105298532`). Source:
https://github.com/facebook/zstd/tree/f8745da6ff1ad1e7bab384bd1f9d742439278e99

`zstddeclib.c` is the unmodified decoder-only amalgamation generated from that
revision by its `build/single_file_libs/combine.py` with arguments:
`-r lib -x legacy/zstd_legacy.h -o zstddeclib.c build/single_file_libs/zstddeclib-in.c`.
Run the upstream Python script through the workspace's existing managed Python.
`zstd.h`, `zstd_errors.h` and `LICENSE` are copied unchanged from the same revision. This project
uses the BSD 3-Clause license in `LICENSE`; no encoder is linked.

Integration follows upstream's single-file decoder instructions:
https://github.com/facebook/zstd/blob/v1.5.7/build/single_file_libs/README.md

Compressed UTF-8 JSONL is fed incrementally into qgrep's existing line transformer
before any text EOL normalization. Decoder window, compressed input, one pending
JSON record and transformed output remain in memory; whole decoded JSONL is not
retained. Other configured transforms retain their existing whole-text behavior.
