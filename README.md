# Fast Lane

Fast Lane is a small CMake/C++17 demo for publishing binary data from one
leader process into shared memory and reading it from one or more follower
processes.

The leader owns the shared-memory segment and repeatedly writes a binary
sequence. Followers attach to that segment, copy stable snapshots into their
own process memory, then use copy-on-write buffers for private local mutation.

## What It Builds

- `fast_lane_shm`: shared-memory library
- `leader`: writer process
- `follower`: reader process
- `imgui_leader`: optional Dear ImGui writer client
- `imgui_follower`: optional Dear ImGui reader client
- `shared_memory_tests`: CTest test executable

## Layout

```text
.
├── CMakeLists.txt
├── include/
│   └── fast_lane/
│       └── shared_memory.hpp
├── src/
│   ├── follower.cpp
│   ├── imgui_follower.cpp   (requires FAST_LANE_BUILD_IMGUI=ON)
│   ├── imgui_leader.cpp     (requires FAST_LANE_BUILD_IMGUI=ON)
│   ├── leader.cpp
│   └── shared_memory.cpp
├── tests/
│   └── shared_memory_tests.cpp
└── third_party/             (git submodules, optional)
    ├── imgui/
    └── glfw/
```

## Requirements

- CMake 3.16 or newer
- A C++17 compiler
- Ninja, Make, Visual Studio, or another CMake-supported generator

Supported shared-memory backends:

- Windows: `CreateFileMapping`, `OpenFileMapping`, `MapViewOfFile`
- Linux: `shm_open`, `ftruncate`, `mmap`

## Build

Generic CMake flow:

```sh
cmake -S . -B build
cmake --build build
```

Windows with MinGW and Ninja:

```sh
cmake -S . -B build-ninja -G Ninja -DCMAKE_CXX_COMPILER=g++ -DCMAKE_MAKE_PROGRAM=ninja
ninja -C build-ninja
```

Windows with Visual Studio:

```sh
cmake -S . -B build-vs -G "Visual Studio 17 2022"
cmake --build build-vs --config Debug
```

## ImGui Clients

The ImGui clients are optional. The command-line `leader`, `follower`, and
tests build with no extra dependencies.

### Set up submodules (recommended)

ImGui and GLFW are pulled in as git submodules, so no manual installs are
needed:

```sh
git submodule add https://github.com/ocornut/imgui.git third_party/imgui
git submodule add https://github.com/glfw/glfw.git      third_party/glfw
git submodule update --init --recursive
```

If you cloned with `--recurse-submodules` they are already present.

### Build the GUI clients

```sh
cmake -S . -B build-imgui -G Ninja -DFAST_LANE_BUILD_IMGUI=ON
cmake --build build-imgui
```

CMake will:
1. Use `third_party/imgui` automatically (no `-DIMGUI_DIR` needed).
2. Build GLFW from `third_party/glfw` via `add_subdirectory` — no system
   GLFW install required.
3. Find OpenGL from the system (always present on Windows, Linux, macOS).

If the submodules are absent, CMake will print a clear error asking you to run
`git submodule update --init --recursive`.

### Run the GUI clients

Start the GUI leader first:

```sh
./build-imgui/imgui_leader
```

Then run one or more GUI followers in separate terminals:

```sh
./build-imgui/imgui_follower
```

The GUI leader lets you create a channel, choose capacity, choose the number of
`uint64_t` values per payload, publish once, or publish continuously. The GUI
follower lets you connect to a channel, poll snapshots, inspect decoded values,
and mutate a forked local buffer to see copy-on-write behavior.

## Run

Start the leader first:

```sh
./build-ninja/leader fast_lane_demo 65536 500
```

Then start one or more followers in separate terminals:

```sh
./build-ninja/follower fast_lane_demo
```

On Windows PowerShell, use:

```powershell
.\build-ninja\leader.exe fast_lane_demo 65536 500
.\build-ninja\follower.exe fast_lane_demo
```

## Arguments

Leader:

```text
leader [name] [capacity-bytes] [interval-ms]
```

- `name`: shared-memory channel name, default `fast_lane_demo`
- `capacity-bytes`: payload capacity, default `65536`
- `interval-ms`: delay between publishes, default `500`

Follower:

```text
follower [name] [max-updates]
```

- `name`: shared-memory channel name, default `fast_lane_demo`
- `max-updates`: stop after this many updates, default `0` which means run forever

## Protocol

The shared-memory region starts with a fixed header followed by raw payload
bytes.

The leader publishes with a seqlock-style protocol:

1. Store an odd sequence number to mark the payload as being written.
2. Copy the binary payload into shared memory.
3. Store the payload byte count.
4. Store the next even sequence number to publish a stable snapshot.

Followers read with the matching protocol:

1. Read the sequence number.
2. Ignore the payload if the sequence is odd.
3. Copy the advertised payload bytes into private process memory.
4. Read the sequence number again.
5. Accept the snapshot only if the sequence is even and unchanged.

This lets followers avoid locking while still rejecting torn writes.

## Copy-On-Write Follower Memory

Followers never mutate shared memory. After a stable snapshot is copied, it is
wrapped in `fast_lane::CopyOnWriteBuffer`.

Calling `fork()` creates another local view of the same bytes. The first write
to either view detaches that view, so local mutations do not affect:

- the shared-memory payload
- other follower processes
- other local forked views inside the same follower process

## Tests

Build and run tests:

```sh
cmake --build build-ninja --target shared_memory_tests
ctest --test-dir build-ninja --output-on-failure
```

The current test executable checks:

- `uint64_t` binary encoding and decoding
- copy-on-write detach behavior
- leader/follower shared-memory round trip in one process

## Notes

This is a focused demonstration, not a production IPC framework. Before using
the pattern in production, consider adding:

- permissions and access-control configuration
- cleanup and lifecycle policy for stale shared-memory segments
- crash recovery for an interrupted leader
- ABI/version negotiation for deployed readers and writers
- explicit byte-order and schema handling for cross-machine data formats
