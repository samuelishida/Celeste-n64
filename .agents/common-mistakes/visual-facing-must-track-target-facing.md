# Visual facing follows completed-tick target intent

OG Player.cs LateUpdate rotates visual Facing toward targetFacing at 4pi rad/s after all state movement, collision, corner reanchors and ground/contact events. Do this for every state. Dash entry and skid entry have explicit source snaps; climbing entry and corner adoption do not.

The earlier animation-alignment recommendation to snap climbing/corner facing was a local adaptation, not OG behavior, and is superseded. A 90-degree inner corner leaves about78 degrees after the first60Hz update and then converges; that transient is correct. Outer corners first ease targetFacing and camera direction over .2s; visual Facing follows that target.

Guardrails: climb_animation_alignment_smoke checks source peak/settling rather than2-degree immediate alignment; og_behavior_trace_smoke checks smooth entry and exact dash snap. Commands must come from source state methods, not final velocity flags.
