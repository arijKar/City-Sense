# CitySense — Urban Sensor Simulator

## Overview
CitySense simulates smart-city sensor data (traffic, air quality, and noise) using modern C++20.  
The simulator reads CSV/JSON inputs, aggregates readings in rolling windows, detects anomalies,  
and outputs console and JSON summaries.

The project focuses on modular design, RAII, exception safety, concurrency, and testing.

---
## Project Reflection
This project helped me connect a lot of the topics of CSI 2372 into one pipeline. I had to think about how data flows end-to-end: 
from CSV files, into `SensorRecord`, through a sliding `Window`, and finally into summaries and detectors.

I tried to follow value semantics by default: `SensorRecord` owns its timestamp, ids and readings while `SensorView` 
is a non-owning view living within the parsing layer. Thus, it was much easier to reason about lifetimes and avoid dangling references 
when records move across threads or windows.

The RAII design patterns show up mainly in the I/O and aggregation pieces: file streams, windows, 
and summaries are automatically cleaned up when they go out of scope, so I didn’t have to manually “reset” state. 
Where errors can happen, bad CSV lines, missing fields, I prefer exceptions plus clear error messages instead of silent failures.
Malformed rows get skipped, but the program keeps running.

On the performance side, I focused on keeping parsing and aggregation simple but not wasteful: I avoid copying strings when not needed and 
try to reuse containers. Concurrency is designed around per-zone processing, but the main goal for this version was correctness and a 
clean API over raw speed.

Overall, CitySense forced me to think about  APIs, ownership, and failure modes  in a more realistic setting than small exercises and that matches
well with the learning goals of this course.

## AI
- Clarifying the project requirements and CMake / CI configuration.
- Drafting/refining some comments, docstrings. 
- Brainstorming and explanations for C++ features


## Quick Start

### Build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 
./build/citysense data/config.json 

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

cmake --build build -j

# Run main pipeline
./build/citysense data/config.json

# Run custom tests
./build/citysense_custom_tests

# Run Catch2 tests
cd build
ctest           


