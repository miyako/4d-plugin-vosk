![version](https://img.shields.io/badge/version-20%2B-E23089)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-vosk)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-vosk/total)

# 4d-plugin-vosk

Offline speech recognition for 4D, built on [Vosk](https://github.com/alphacep/vosk-api) (Kaldi). The plugin loads a Vosk language model from a folder, then transcribes either a `.wav` file or live audio from the default input device (via PortAudio). Commands return a 4D `Object` with the recognized `text`. During a transcription the plugin can call back a project method with the text recognized so far.

| Command | Returns | Purpose |
|---|---|---|
| [`vosk`](#vosk) | `Object` | Transcribe a `.wav` file, or record and transcribe the default audio input |
| [`vosk set options`](#vosk-set-options) | — | Load the model (and optional speaker model) and set the sample rate |
| [`vosk get options`](#vosk-get-options) | `Object` | Read back the current model paths and sample rate |

**Platforms:** macOS, Windows (x64)

---

## Requirements & platform notes

- **A Vosk model is required.** Download one from [alphacephei.com/vosk/models](https://alphacephei.com/vosk/models), unzip it (for example into the database's `Resources/models/` folder), and pass the folder to [`vosk set options`](#vosk-set-options) before calling [`vosk`](#vosk). Without a loaded model, [`vosk`](#vosk) returns `success: False`. Large models give better recognition but take longer to load and use more memory.
- **Settings are global.** The model, speaker model and sample rate are shared by every process in the 4D application and stay loaded until 4D quits. Set them once, for example in the `On Startup` database method.
- **One transcription at a time.** While a [`vosk`](#vosk) call is running, a second call from any other process returns immediately with `success: False`.
- **The second parameter of [`vosk`](#vosk) is mandatory.** Pass at least an empty object `{}`. If it is missing, nothing is transcribed and the result is `{success: False}`.
- **Audio format.** File transcription expects 16-bit PCM mono `.wav` at the sample rate set with [`vosk set options`](#vosk-set-options) (16000 Hz by default). Live recording uses that same rate.
- **Microphone access.** Live recording uses the system's default input device. On macOS the system asks the user for microphone permission for the 4D application the first time. If access is refused, expect the recording to contain no sound and `text` to be empty.
- **Thread-safe.** All three commands are declared thread-safe, so they can run in a preemptive worker (the test database uses `CALL WORKER`).
- **macOS binary of release 2.0.0.** The macOS binary built for `2.0.0` (the `vosk.zip` committed with that release) contains only an Apple silicon (`arm64`) slice, so it does not load on Intel Macs. A universal build needs the prebuilt Kaldi/OpenBLAS static libraries for both architectures (see [Building from source](#building-from-source)).
- Requires 4D 20 or later (see the badge). The samples below use object literals (`{...}`); if your version doesn't support them, build the same objects with `New object`.

---

## vosk

### Syntax

```4d
$result := vosk ( file ; options ) -> Object
```

| Parameter | Type | Description |
|---|---|---|
| `file` | `4D.File` \| `Null` | The `.wav` file to transcribe. Pass `Null` to record and transcribe the default audio input instead |
| `options` | `Object` | Transcription options, see below. **Mandatory**: pass `{}` for none |
| Result | `Object` | `{success: Boolean; text: Text}` |

`options` properties:

| Property | Type | Description |
|---|---|---|
| `onData` | `Text` | Name of a project method to call while transcribing (see Description). Optional |
| `userData` | `Object` | Passed unchanged to the `onData` method as `$2`. Optional |
| `duration` | `Real` | **Audio input only.** Recording length in seconds. Default `3` |
| `output` | `4D.File` | **Audio input only.** File to write the recorded audio to, as a 16-bit PCM mono `.wav` at the configured sample rate. Optional |

Result properties:

| Property | Type | Description |
|---|---|---|
| `success` | `Boolean` | `True` if a transcription ran to the end |
| `text` | `Text` | The recognized text, utterances separated by a space. Only present when `success` is `True` |

### Description

`vosk` transcribes speech with the model loaded by [`vosk set options`](#vosk-set-options). The command is synchronous: it returns when the file has been read to the end, or when the recording `duration` has elapsed, or when the `onData` method asks to stop.

**File mode.** The plugin skips the first 44 bytes of the file (the canonical WAV header) and feeds the rest to the recognizer as raw samples. The header is not checked, so a file that is not 16-bit PCM mono at the configured rate, or that has a longer header, is transcribed as noise rather than rejected.

**Audio input mode.** The plugin opens the default input device (mono, 16-bit, at the configured rate), records for `duration` seconds and transcribes the audio as it arrives. If `output` is given, the full recording is written to that file at the end. If the audio device cannot be opened, the command still returns `success: True` with an empty `text`.

**The `onData` callback.** If `onData` names a project method, the plugin calls it about every 100 ms during the transcription, then once more with the final text:

- `$1` is an object `{text: Text}` holding the text recognized so far. It grows by whole utterances: Vosk completes an utterance when it detects a pause, so `text` can stay the same for several calls.
- `$2` is the `userData` object.
- Return `True` to stop the transcription early, `False` to continue. The return value of the final call is ignored.

The callback runs in the process that called `vosk`. To update a form from a worker, use `CALL FORM` as the test database does. A `Formula` object is not accepted as `onData`: pass the method name as text, otherwise the callback is silently not called.

Since the fixes made after release 2.0.0:
- an invalid `duration` (negative, `NaN`, infinite) is ignored and the default of 3 seconds applies; values above 86400 seconds (24 hours) are capped.
- a call made while no model is loaded no longer leaves the plugin busy, so later calls work once a model is loaded (in 2.0.0, every later call returned `success: False` until 4D was restarted).

### Example

From the test database (`transcribe_file.4dm`), run in a worker with `CALL WORKER`:

```4d
//%attributes = {"invisible":true,"preemptive":"capable"}
#DECLARE($params : Object)

/*

transcribe the contents of a .wav audio file using a model

*/

var $wav : 4D:C1709.File
$wav:=File:C1566("/RESOURCES/test.wav")
$status:=vosk($wav; {onData: $params.onData; userData: {window: $params.window}})
```

From the test database (`transcribe_audio_input.4dm`), recording 9 seconds of microphone input and saving it:

```4d
//%attributes = {"invisible":true,"preemptive":"capable"}
#DECLARE($params : Object)

/*

pass null instead of file to transcribe default audio input

*/

var $wav : 4D:C1709.File
$wav:=Folder:C1567(fk desktop folder:K87:19).file("test.wav")
//output is optional

$status:=vosk(Null:C1517; {onData: $params.onData; userData: {window: $params.window}; duration: 9; output: $wav})
```

The callback method from the test database (`onData.4dm`). It shows the text in the calling window, and stops when Caps Lock is down:

```4d
//%attributes = {}
#DECLARE($status : Object; $userData : Object) : Boolean

If (Caps lock down:C547)
	TRACE:C157
	return True:C214  //stop
End if 

CALL FORM:C1391($userData.window; Formula:C1597(Form:C1466.input:=$status.text))

return False:C215
```

From the test form, starting the transcription in a worker:

```4d
If (FORM Event:C1606.code=On Clicked:K2:4)
	
	Form:C1466.input:=""
	
	CALL WORKER:C1389("vosk"; "transcribe_file"; {window: Current form window:C827; onData: "onData"})
	
End if 
```

Without a callback, check the result directly:

```4d
var $result : Object
$result:=vosk(File("/RESOURCES/test.wav"); {})
If ($result.success)
	ALERT($result.text)
Else 
	ALERT("No transcription: is a model loaded, or is another transcription running?")
End if 
```

---

## vosk set options

### Syntax

```4d
vosk set options ( options )
```

| Parameter | Type | Description |
|---|---|---|
| `options` | `Object` | Settings to change, see below |
| Result | — | None |

`options` properties (all optional; a property that is not passed keeps its current value):

| Property | Type | Description |
|---|---|---|
| `model` | `4D.Folder` | Folder of a Vosk model, e.g. `vosk-model-small-en-us-0.15` |
| `speaker` | `4D.Folder` | Folder of a Vosk speaker-identification model, e.g. `vosk-model-spk-0.4` |
| `rate` | `Real` | Sample rate in Hz, used for file transcription and recording. Default `16000` |

### Description

`vosk set options` loads the models synchronously, which can take several seconds for a large model. Each call that ends with a model path set reloads the model from disk and rebuilds the recognizer, even if only `rate` changed.

- `model` and `speaker` must be `4D.Folder` objects. A text path is ignored.
- Once set, `speaker` cannot be removed: later calls keep using it.
- If a model fails to load (wrong folder, incompatible model), nothing is reported. [`vosk get options`](#vosk-get-options) still shows the path, and [`vosk`](#vosk) returns `success: False`.
- Since the fixes made after release 2.0.0, a `rate` that is not a positive number up to 192000 is ignored.
- **Don't call `vosk set options` while a [`vosk`](#vosk) transcription is running** in another process: the running transcription uses the recognizer that this command replaces.

### Example

From the test database's `On Startup` database method:

```4d
var $model : 4D:C1709.Folder
$model:=Folder:C1567("/RESOURCES/models/vosk-model-small-en-us-0.15/")
//var $speaker : 4D.Folder
//$speaker:=Folder("/RESOURCES/speakers/vosk-model-spk-0.4/")
//speaker is optional

vosk set options({model: $model; /* speaker: $speaker;*/rate: 16000})
```

---

## vosk get options

### Syntax

```4d
$options := vosk get options -> Object
```

| Parameter | Type | Description |
|---|---|---|
| Result | `Object` | `{model: Text; speaker: Text; rate: Real}` |

### Description

Returns the settings last passed to [`vosk set options`](#vosk-set-options). `model` and `speaker` are the folder paths as text: a POSIX path on macOS, a native path on Windows. They are empty strings if never set. The paths are returned even if loading the model failed.

### Example

```4d
var $options : Object
$options:=vosk get options
If ($options.model="")
	vosk set options({model: Folder("/RESOURCES/models/vosk-model-small-en-us-0.15/")})
End if 
```

---

## Error handling & troubleshooting

- **No error is raised.** None of the commands raises a 4D error. Check `success` in the result of [`vosk`](#vosk).
- **`success` is always `False`.** Either no model is loaded ([`vosk set options`](#vosk-set-options) not called, or the model folder failed to load), the second parameter of [`vosk`](#vosk) is missing, the file could not be opened, or another transcription is running.
- **Text is garbage or empty for a file.** The file must be 16-bit PCM mono at the configured `rate`, with a 44-byte header. Convert other files first, or match `rate` to the file.
- **Text is empty for live audio.** The input device could not be opened, or (on macOS) microphone access was refused for 4D.
- **The callback is never called.** `onData` must be the name of a project method as text, not a `Formula`.
- **The callback text lags behind.** `text` grows only when Vosk completes an utterance at a pause in the speech.
- **Intel Mac fails to load the plugin.** The release 2.0.0 macOS binary is `arm64` only.
- **Crash after changing options.** Don't call [`vosk set options`](#vosk-set-options) while a [`vosk`](#vosk) call is running in another process.

---

## Quick reference

```4d
vosk set options({model: Folder("/RESOURCES/models/vosk-model-small-en-us-0.15/"); rate: 16000})

$r:=vosk(File("/RESOURCES/test.wav"); {})  // file
$r:=vosk(Null; {duration: 5; output: Folder(fk desktop folder).file("rec.wav")})  // microphone
$r:=vosk(Null; {duration: 9; onData: "onData"; userData: {window: Current form window}})  // with callback

If ($r.success)
	ALERT($r.text)
End if 

$o:=vosk get options  // {model; speaker; rate}
```

---

## Building from source

The macOS build links Vosk and Kaldi statically (`a/`); the Windows build links `lib/libvosk.lib` and ships `libvosk.dll` and its MinGW runtime DLLs in `Contents/Windows64`.

```
brew install cmake swig python3
```

```
git clone -b vosk --single-branch --depth=1 https://github.com/alphacep/kaldi.git ~/kaldi
cd ~/kaldi/tools
make openfst cub 
./extras/install_openblas_clapack.sh
cd ../src
./configure --mathlib=OPENBLAS --shared --static --static-fst --static-math
make -j$(sysctl -n hw.ncpu) online2 lm rnnlm
```

```
cd vosk-api/src
export KALDI_ROOT=~/kaldi 
make -j$(sysctl -n hw.ncpu)
ar rcs libvosk.a *.o  
```
