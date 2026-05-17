# chunking/

This directory keeps each chunking algorithm family in its own source file so the implementation boundary is easy to find and maintain.

## File layout

- `rabin_chunking.c`: standard Rabin, normalized Rabin, and Rabin TTTD.
- `rabinjump_chunking.c`: RabinJump variant built on Rabin's rolling fingerprint state.
- `ae_chunking.c`: AE chunking.
- `fastcdc_chunking.c`: FastCDC only.
- `fastcdc_gpu.c`: CUDA driver bootstrap and current CPU fallback wrappers for FastCDC and JC GPU paths.
- `gear_common.c` / `gear_common.h`: shared Gear lookup-table initialization and condition masks.
- `gear_chunking.c`: standard Gear and Gear TTTD.
- `gearjump_chunking.c`: GearJump, GearJump TTTD, and normalized GearJump.
- `leap_chunking.c`: Leap chunking.
- `chunking.h`: public chunking API used by the rest of the system.
- `rabin_shared.h`: private Rabin shared declarations used internally by RabinJump.

## Maintenance notes

- Prefer adding new chunking variants to a dedicated source file when they are algorithmically distinct.
- Keep cross-file sharing private unless the rest of the project needs the symbol in `chunking.h`.
- If a new file is added, update `Makefile.am`, `Makefile.in`, and `Makefile` together in this repository layout.