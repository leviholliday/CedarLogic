// --bench-drag: how long the canvas takes to draw while something is dragged,
// measured inside the app (see linux/SMOOTHNESS.md).

#ifndef CL_LINUX_BENCH_H
#define CL_LINUX_BENCH_H

class CircuitWindow;

namespace bench {

// Drags in the window's circuit through the canvas's own handlers, drawing
// after every step, and prints the times. Puts everything back as it was.
// Returns the process's exit code (0, or 1 when there was nothing to drag).
int dragBenchmark(CircuitWindow* window, int steps);

}  // namespace bench

#endif  // CL_LINUX_BENCH_H
