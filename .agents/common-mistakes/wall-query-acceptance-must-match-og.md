# Wall-query acceptance must match OG after broad-phase inflation

## Symptom

Wall probes register just beyond OG's reach, especially at finite slice edges
or after transforms. Tiny false contacts can change grip, popout, or walljump
state even when broad geometry looks identical.

## Cause

A conservative broad-phase margin was also applied to the final plane-distance
and slice-distance tests. OG accepts plane distance `<= radius`, then accepts
slice contact only when `distanceSquared <= radiusSquared`; any positive
overrun is rejected. Static and dynamic query paths must use the same strict
acceptance.

## Fix

Keep any measured inflation only in candidate bounds. After a candidate is
visited, compare plane distance and finite-slice `distanceSquared` directly to
the OG radius values. Add source-derived exact-tangent and just-outside tests
for both plane and finite-slice boundaries; inspect the dynamic path separately
because it can duplicate the static predicate.
