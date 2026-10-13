# Distant tier must yield to ready near residents

## Symptom

Platforms or terrain flicker when camera movement changes which renderer owns
a cell near the streaming boundary, especially in camera-forward preloads.

## Cause

The detailed draw mask was based on camera-frustum visibility, so READY
Cheb-2 preloads could switch between detailed near and coarse DLOD as the view
moved. The two passes use different projections and clear depth between them;
overlapping representations cannot rely on cross-pass depth to resolve their
differences. That can make a platform flicker or appear to float.

## Fix

Keep detailed rendering fixed to the Cheb-1 core. Cheb-2 residents are
preload-only, even when ready; leave them DLOD-owned until they enter the core.
Skip DLOD only for core cells whose detailed renderer is ready and submitted
this frame. Keep DLOD enabled for pending core cells and nonresident cells, so
streaming does not create holes. Query ownership from the current draw mask.

## Guardrail

Host-test ready Cheb-2, pending core, and ready core cases. Then verify the
handoff in Ares while moving and orbiting; host tests do not prove visual
stability or the frame-time target.
