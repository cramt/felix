---
name: eval-attrset-update-layer-rhs-threshold
internalName: bindingsUpdateLayerRhsSizeThreshold
type: unsigned int
defaultExpr: 'sizeof(void *) == 4 ? 8192 : 16'
defaultText: '`16` on 64-bit platforms, `8192` on 32-bit platforms'
---
Maximum size of the right-hand side of an [attribute set update](@docroot@/language/operators.md#update) (`//`) for which the result is stored as a layer on top of the left-hand side instead of a full copy.

Larger values allocate less memory but make attribute lookups and iteration on deeply layered sets slower.
`0` disables layering entirely.

The 32-bit default is much larger because there address space, not lookup speed, is what runs out first.

This is an advanced performance tuning option and typically should not be changed.
