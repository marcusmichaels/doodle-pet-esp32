// Virtual pet: a blob that lives in a room you draw in. Long strokes become floors and
// ramps, small drawings drop as food (the colour decides what it is), circles become
// balls, and a saved drawing can hang on the wall.
#pragma once

void petAppBegin();
void petAppLoop();

// Draws your pet (or its egg) for the start screen. cx = centre, by = where it stands.
void petDrawPreview(int cx, int by);
