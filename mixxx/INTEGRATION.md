# integrating halftime into Mixxx as a native effect

## files to copy

copy into the Mixxx source tree:

```
# headers — symlink or copy the include dir
ln -s /path/to/halftime-lv2/include /path/to/mixxx/src/effects/backends/builtin/halftime

# effect wrapper
cp mixxx/halftimeeffect.h   /path/to/mixxx/src/effects/backends/builtin/
cp mixxx/halftimeeffect.cpp /path/to/mixxx/src/effects/backends/builtin/
```

## register the effect

in `src/effects/backends/builtin/builtinbackend.cpp`:

```cpp
#include "effects/backends/builtin/halftimeeffect.h"
```

and in the `BuiltInBackend()` constructor:

```cpp
registerEffect<HalftimeEffect>();
```

## add to meson build

in `src/effects/backends/builtin/meson.build` (or equivalent CMakeLists), add:

```
'halftimeeffect.cpp',
```

## include path

the halftime headers live in `include/` relative to the halftime-lv2 repo root.
add that directory to the include path for the builtin effects target, or
adjust the `#include "HalftimePlugin.hpp"` path in `halftimeeffect.h` to match
wherever the headers land in the Mixxx tree.

## no LV2 dependency

the wrapper does NOT define `HALFTIME_HAS_LV2`, so `BpmSync.hpp` and all LV2
headers are excluded. BPM comes directly from Mixxx's `GroupFeatureState::beat_length`.

## latency reporting

`HalftimeEffect::getGroupDelayFrames()` returns the current DSP latency
(OLA grain + PhaseVocoder FFT + optional lookahead). Mixxx's effect chain
uses this to delay the dry signal for phase-aligned dry/wet mixing.
