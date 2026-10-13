# Outer-corner rays must stay on player side of finite walls

## Symptom

Climbing around a finite wall edge snaps Madeline through the adjoining face,
then leaves her embedded in the map texture or stuck on the wrong side.

## Cause

`QueryWallRay` orients its hit normal toward the ray origin. Starting a corner
ray beyond the finite edge can put that origin behind the target wall. The
returned normal is valid for the ray origin but wrong for the player, so
`AdoptCornerWall` anchors her through the face.

## Fix

Cast from the player's side along lateral intent. Reject candidate anchors
whose old-wall query resolves from the opposite side. If no reachable
front-side face remains, let climb grip expire; never force the snap.

## Guardrail

Keep a real-map seam replay that checks per-tick displacement, waist/head
pushout, wrong-side corner adoption, and clean release at unsupported edges.
