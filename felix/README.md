# Felix

A personal soft fork of [Lix](https://lix.systems). Not Lix: please don't report Felix bugs upstream.

Felix patches are written with AI assistance, which [Lix's AI policy](https://lix.systems/ai-policy/) does not accept as contributions. Nothing here is intended for upstreaming.

## Patch stack

The branch is upstream's release tag plus one commit per change. It gets rebased onto new Lix releases, never merged. Fork-only tooling lives in this directory.

| Patch | Reference | Effect |
| --- | --- | --- |
| Layered attrset updates | NixOS/nix#13987, NixOS/nix@ec2fd2dc2 | `a // b` with a small `b` stores `b` as a layer over `a` instead of copying `a`. |

## Rules for a patch

- `felix/bench.py` reports identical drvPaths against stock Lix for every host on a pinned config revision.
- The `check` and `installcheck` meson suites pass, apart from failures that also happen on the unpatched upstream tag.
- Benchmark numbers go in the commit message.

## Deployment

Felix is the whole Nix on every host in [nixconf](https://github.com/cramt/nixconf), root daemon included. It's built with this repo's own `package.nix` (nixpkgs' Lix recipe can't cross-compile for armv7l).

Because Felix is the root daemon, **Lix security releases come first**: rebase Felix onto the fixed tag, rerun the rules above, and bump nixconf's pinned input before doing anything else. Stock Lix users get those fixes from nixpkgs; Felix only gets them through that rebase.

Any patch outside `libexpr` (store, daemon, builds) must also pass the full `installcheck` suite, since every host runs it as root.

## Benchmark

```sh
felix/bench.py "$(command -v nix)" build/lix/nix/nix --flake 'git+file:///home/cramt/nixconf?rev=<sha>'
```

Pin `--flake` to a commit. A working tree that another process is editing makes the comparison meaningless.
