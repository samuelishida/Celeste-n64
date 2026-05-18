# Room fixture

`assets/og_converted/models/car_collider.t3dm` is the known-good static room-path
fixture for renderer diagnostics. The ROM diagnostic path loads it as
`room_fixture.t3dm` and scales it to room size, so a future Ares screenshot proves
the static `.t3dm` room path independently of the Quake-map baker.

Expected source SHA-256 is stored beside this file in `room_fixture.sha256`.
