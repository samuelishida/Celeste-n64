## session-facts (2026-05-15)
- The `Actor` class lacks a constructor and default member initializer for the `position` variable, leading to uninitialized values.
- The recommended fix for `position` is to either zero-initialize it in the declaration or provide a constructor that initializes it to (0, 0, 0).
- The verification step involves a smoke test that checks if an `Actor` instance has its position initialized to (0.0f, 0.0f, 0.0f).
- The `SpringActor` class is missing an override for the `IsCollectible()` method, despite having properties that suggest it should be collectible.
- **CORRECTION**: WRONG: The assistant did not specify the necessary changes to the `SpringActor` class. RIGHT: Clearly state that `SpringActor` needs to implement the `IsCollectible()` method to align with its intended functionality.
