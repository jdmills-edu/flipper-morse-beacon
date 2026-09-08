#pragma once

#include <stdbool.h>

/** Append a timestamped line to /ext/apps_data/morse_beacon/id_log.txt.
 *  Doubles as the station's ID record and as the only way to see what the
 *  radio did when the app is running headless. */
void morse_log(const char* format, ...);
