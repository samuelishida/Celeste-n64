# Tiny3D matrix pointers must outlive queued RSP work

Tiny3D queues matrix DMA asynchronously. Never pass a stack-local
`T3DMat4FP` to `t3d_matrix_push` and then overwrite/reuse that storage for the
next actor or draw call before RSP completion. Keep one uncached matrix per
queued transform (or use an explicitly buffered lifetime). The frame-end
`rspq_wait()` protects reuse across frames, not between actors emitted in one
frame. Dynamic solids therefore keep per-actor matrices in uncached storage.

Audit temporary reveal matrices too before enabling `kEnableCellReveal`; those
paths must keep each submitted matrix alive across every grouped draw call.

Tiny3D source documentation: https://github.com/HailToDodongo/tiny3d#readme
