# Vendored third-party code

| File | Version | Source | SHA-256 |
|---|---|---|---|
| `doctest/doctest.h` | 2.4.11 | https://github.com/doctest/doctest (copied from Litharia's `libs/doctest`) | `28846c518fc824eb37354bba40fb9a6372d67f891562d243b663b76190dc6bd7` |
| `nlohmann/json.hpp` | 3.12.0 | https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp | `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63` |

Both are MIT-licensed single headers. They are used only by the unit tests and
the test harness; the emulator core depends on neither.
