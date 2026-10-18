#pragma once

class Mtl;

// Scene-wide viewport switch. The material passed to Set is the one every
// geometry node shows, and the same switch is shared by every Form Clay rollout.
bool FormClayViewportOverrideEnabled();
void FormClaySetViewportOverride(bool on, Mtl* source);
void FormClayShutdownViewportOverride();
