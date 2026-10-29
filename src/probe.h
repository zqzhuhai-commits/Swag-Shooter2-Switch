#pragma once
#include <cstdio>
// Executes actual ARM/Thumb fixtures, then a strictly checked pure guest export.
// Does not initialize Unity, run constructors, or call any guest Android API.
int run_probe(FILE* log, const char* library_path);
