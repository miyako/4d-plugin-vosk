# Ownership regression harness

`run.sh` builds the real plugin sources (`4DPlugin-vosk.cpp`,
`support/4DPlugin-JSON.cpp`, `C_TEXT.cpp`), the unchanged SDK
`4DPluginAPI.c` and jsoncpp 1.9.4 (pinned commit, the version of
`include/json`) with ASan/UBSan against a synthetic `Call4D` engine
(`engine_harness.cpp`). The engine tracks every string, object and handle it
hands out, poisons the results of failed calls, and reports leaks and
contract violations.

Vosk and PortAudio are stubbed: the recognizer returns synthetic JSON
results, the WAV input is a synthetic temporary file and the "microphone" is
a synthetic in-memory buffer. No model is downloaded and no audio device is
opened.

```sh
tests/ownership/run.sh . fixed                 # every test must pass
git archive <base> | tar -x -C /tmp/base
tests/ownership/run.sh /tmp/base baseline      # named defects must fail,
                                               # controls must pass
```

Set `JSONCPP_SRC` to an existing jsoncpp checkout at the pinned commit to
skip the fetch.

Contracts covered:

- `PA_GetObjectProperty`, `PA_ExecuteCommandByID` (File/Folder, JSON
  Stringify) and `PA_ExecuteMethodByID` results are owned: clear them, but
  only when `PA_GetLastError()` taken immediately after the call is
  `eER_NoErr`.
- A variable that receives a shallow `PA_Set*Variable` must start as
  `eVK_Undefined`, otherwise the empty owner leaks; a string borrowed that
  way must not be disposed twice.
- An object or collection read from an object property is borrowed from its
  parent, so a borrowed `userData` passed to the callback is never cleared.
- Callback parameters owned by the plugin (method name, status object) are
  released on every path, including when no model is loaded.

Each test runs in its own process. Test names prefixed `defect_live_` are
reachable from a command, `defect_dormant_` are built but uncalled,
`hardening_` cover error paths, and `control_` must pass on both trees.
