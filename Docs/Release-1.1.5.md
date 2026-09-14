# plasma.ai 1.1.5

Requires Plasma Engine 0.9.1 (win64-msvc19-0.9.1-abi2-dev).

- Exposes independent EQS filter and score inversion in test cards.
- Adds explicit filter conditions, measurement bounds, and missing-data policies.
- Evaluates new response curves at their authored normalized X coordinates and holds endpoint values outside the authored range. Existing assets retain legacy curve behavior until explicitly migrated.
- Aligns editor previews with runtime evaluation and adds validation, candidate traces, and rejection diagnostics.
- Includes crowd navigation fixes for acceleration-limited avoidance and unobstructed movement.

Validation: runtime and editor plugin builds, 103 automated checks including transformed legacy Shatterfall queries, and user testing in Shatterfall. Release DLLs match the tested deployment.

See [AI curves and EQS authoring](AI_Curves_and_EQS.md) for behavior and migration details.
