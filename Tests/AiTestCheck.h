#pragma once

/// Records one check; prints the message when the value is false.
void Check(bool value, const char* message);

/// World-tier regressions: each creates a plWorld and steps it at a fixed timestep.
void RunBrainWorldTests();
void RunEqsWorldTests();
void RunNavigationTests();
void RunVoxelTests();
void RunEncounterTests();
