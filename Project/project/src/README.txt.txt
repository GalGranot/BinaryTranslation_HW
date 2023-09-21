Gal Granot 315681593
Golan Gershonowitz 208830257

Compilation command:
make PIN_ROOT="./pin-3.25-98650-g8f6168173-gcc-linux" obj-intel64/project.so

How to run tool:
Run the pintool with -prof in order to generate runtime data, then with -opt in order to use the optimizations.

Explanation of profile file:
For inlining, inline-count.csv automatically deducts the hottest called routines, and assigns a single hottest call site for each one. The result is a table of two addresses: callee followed by caller.
To profile appropriate routines for reordering, we chose to construct a graph with entry/exit points of BBLs as vertices and branch instructions as edges, the following of which are documented in edge-count.csv.
0xffffffff is an invalid address, and is written as an edge destination when the last instruction in a BBL isn't a branch. Else, the address is for the required BBL for reordering.

Explanation of candidates:
For inlining, we chose to focus on routines which met the criteria we learned in Lecture 8 - do not contain forward jumps, do contain only one return instruction, etc. Also, our goal was to inline the hottest call site for each inlinee.
For reordering, we chose to focus on routines which do not have overlapping BBLs. The reordering focused on improving branch prediction, making the fallthrough path the likeliest, as well as improving the rate of cache hits.

Explanation of threshold:
We chose a global knob for frequently and rarely executed instructions. The profiling data helped us understand the number of calls to each routine, and using that we were able to deduce what routines we should focus on.