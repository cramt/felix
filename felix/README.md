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

What the patch stack touches decides where Felix runs:

- **Only `libexpr` is patched (today):** Felix runs as the client `nix`, which is where evaluation happens. The root daemon stays on stock Lix. The daemon never evaluates, so it would gain nothing from Felix and would get security fixes later than nixpkgs ships them.
- **A patch touches `libstore`, the daemon, or builds:** in the same nixconf commit that bumps to that patch, switch `nix.package` to Felix on the hosts that want it. That patch must also pass the full `installcheck` suite. From then on, Lix security releases have to be rebased onto Felix promptly, because Felix is now the root daemon.

## Benchmark

```sh
felix/bench.py "$(command -v nix)" build/lix/nix/nix --flake 'git+file:///home/cramt/nixconf?rev=<sha>'
```

Pin `--flake` to a commit. A working tree that another process is editing makes the comparison meaningless.
