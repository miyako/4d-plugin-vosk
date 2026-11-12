/*
 * Synthetic 4D engine boundary for ownership regressions.
 *
 * Compiles the real plugin entry point (4DPlugin-vosk.cpp, included below)
 * and links the real support/4DPlugin-JSON.cpp, the built SDK class
 * (C_TEXT.cpp), the unmodified 4DPluginAPI.c and jsoncpp 1.9.4 (the version
 * of include/json). Only gCall4D is replaced. Every engine resource
 * (Unistrings, pictures, objects/collections, handles) is a real heap block
 * tracked in a live set, so leaks are detected exactly and a dispose of
 * something the engine no longer owns (double free) is reported by name
 * instead of being freed a second time.
 *
 * Vosk and PortAudio are stubbed: no model is loaded and no audio device is
 * opened. The recognizer returns synthetic JSON results, and the "microphone"
 * delivers synthetic in-memory PCM frames.
 *
 * Engine semantics modelled (per the SDK contract):
 *  - PA_CreateVariable(eVK_Unistring/eVK_Picture) allocates an empty value.
 *  - PA_GetStringVariable returns a view borrowed from the variable.
 *  - PA_SetObjectProperty deep-copies strings and pictures and retains
 *    objects/collections; the caller still owns its variable.
 *  - PA_GetObjectProperty returns an owned copy (or a retained object) that
 *    must be cleared; on error the result is left uninitialised (poisoned
 *    here) and must be neither read nor cleared.
 *  - PA_ExecuteCommandByID and PA_ExecuteMethodByID return an owned result,
 *    with the same error rule. EXECUTE METHOD (1007) without `*` returns
 *    Undefined.
 *  - Parameters passed to commands and methods stay owned by the caller.
 */

/* Resolved through -I <source-root>, so the same harness tests baseline and fixed trees. */
#include "4DPlugin-vosk.cpp"

#include "PrivateTypes.h"
#include "EntryPoints.h"

#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

extern "C" Call4DProcPtr gCall4D;

namespace {

typedef std::basic_string<PA_Unichar> u16;

struct Obj {
    int rc;
    bool coll;
    std::map<u16, PA_Variable> props;
    std::vector<PA_Variable> items;
};

/* data first: the SDK dereferences array handles directly (*(T**)handle). */
struct Handle { char *data; size_t size; int locks; };

std::set<void *> g_strings;
std::set<void *> g_pictures;
std::set<Obj *> g_objects;
std::set<void *> g_handles;
std::vector<std::string> g_violations;

/* PA_GetObjectProperty for this key fails: eb.fError is set and the result
   is left uninitialised. The poison is readable (tests fail by assertion,
   not by crashing) but never engine-owned, so clearing it is counted. */
std::string g_fail_key;
/* File (1566) / Folder (1567) fail the same way. */
bool g_fail_file_command = false;
/* JSON Stringify (1217) fails the same way. */
bool g_fail_stringify = false;
const char kPoisonMark = 0x5A;
int g_poison_clears = 0;
PA_Unichar kPoisonBuf[] = { 'B', 'A', 'D', '!', 0 };
PA_Unichar *const kPoison = kPoisonBuf;

/* Project methods known to the synthetic engine. */
const PA_long32 kOnDataID = 42;
enum MethodResult { kReturnBoolean, kReturnObject, kReturnText, kFail };
MethodResult g_method_result = kReturnBoolean;
std::vector<std::string> g_method_status;      /* $1.text per PA_ExecuteMethodByID call */
std::vector<void *> g_method_userdata;         /* $2 per call */
std::vector<std::string> g_execute_method;     /* method name per EXECUTE METHOD call */
std::vector<std::string> g_execute_status;     /* $2.text per EXECUTE METHOD call ($1 = result flag) */

PA_Variable undefined_var();

void poison(PA_Variable *out) {
    *out = undefined_var();
    out->fFiller = kPoisonMark;
    out->fType = eVK_Unistring;
    out->uValue.fString.fString = kPoison;
    out->uValue.fString.fLength = 4;
}

void violation(const std::string &what) {
    fprintf(stderr, "  ENGINE CONTRACT: %s\n", what.c_str());
    g_violations.push_back(what);
}

[[noreturn]] void die(const char *msg) {
    fprintf(stderr, "HARNESS ERROR: %s\n", msg);
    fflush(stderr);
    abort();
}

std::string narrow(const u16 &s) { std::string r; for(PA_Unichar c : s) r.push_back((char)c); return r; }
u16 widen(const std::string &s) { u16 r; for(char c : s) r.push_back((PA_Unichar)(unsigned char)c); return r; }

PA_Unistring new_string(const PA_Unichar *src, size_t len) {
    PA_Unichar *buf = (PA_Unichar *)malloc((len + 1) * sizeof(PA_Unichar));
    if(len) memcpy(buf, src, len * sizeof(PA_Unichar));
    buf[len] = 0;
    g_strings.insert(buf);
    PA_Unistring s;
    s.fLength = (PA_long32)len;
    s.fString = buf;
    s.fReserved1 = 0;
    s.fReserved2 = 0;
    return s;
}

void free_string(PA_Unistring *s) {
    if(!s->fString) return;
    if(s->fString == kPoison) {
        g_poison_clears++;
        fprintf(stderr, "  cleared the uninitialised result of a failed engine call\n");
    } else if(!g_strings.erase(s->fString)) {
        violation("double_dispose_string");
    } else {
        free(s->fString);
    }
    s->fString = 0;
    s->fLength = 0;
}

void free_picture(void *p) {
    if(!p) return;
    if(!g_pictures.erase(p)) { violation("double_dispose_picture"); return; }
    free(p);
}

Handle *new_handle(size_t size) {
    Handle *h = (Handle *)malloc(sizeof(Handle));
    h->size = size;
    h->locks = 0;
    h->data = (char *)calloc(1, size ? size : 1);
    g_handles.insert(h);
    return h;
}

void free_handle(Handle *h) {
    if(!h) return;
    if(!g_handles.erase(h)) { violation("double_dispose_handle"); return; }
    free(h->data);
    free(h);
}

Handle *live_handle(void *p) {
    if(!g_handles.count(p)) die("used an unknown handle");
    return (Handle *)p;
}

void release_object(Obj *o);

void clear_var(PA_Variable *v) {
    if(v->fFiller == kPoisonMark) {
        g_poison_clears++;
        fprintf(stderr, "  cleared the uninitialised result of a failed engine call\n");
        v->fType = eVK_Undefined;
        v->fFiller = 0;
        return;
    }
    switch(v->fType) {
        case eVK_Unistring: free_string(&v->uValue.fString); break;
        case eVK_Picture: free_picture(v->uValue.fPicture); break;
        case eVK_Blob: free_handle((Handle *)v->uValue.fBlob.fHandle); break;
        case eVK_Object: if(v->uValue.fObject) release_object((Obj *)v->uValue.fObject); break;
        case eVK_Collection: if(v->uValue.fCollection) release_object((Obj *)v->uValue.fCollection); break;
        default: break;
    }
    v->fType = eVK_Undefined;
}

Obj *live(void *p) {
    if(!g_objects.count((Obj *)p)) die("used a dead object/collection");
    return (Obj *)p;
}

PA_Variable undefined_var() { PA_Variable v; memset(&v, 0, sizeof(v)); v.fType = eVK_Undefined; return v; }

PA_Variable copy_var(const PA_Variable &src) {
    PA_Variable dst = src;
    switch(src.fType) {
        case eVK_Unistring:
            if(src.uValue.fString.fString && src.uValue.fString.fString != kPoison && !g_strings.count(src.uValue.fString.fString))
                violation("stored_a_disposed_string");
            dst.uValue.fString = new_string(src.uValue.fString.fString, src.uValue.fString.fLength);
            break;
        case eVK_Object:
            if(src.uValue.fObject) live(src.uValue.fObject)->rc++;
            break;
        case eVK_Collection:
            if(src.uValue.fCollection) live(src.uValue.fCollection)->rc++;
            break;
        case eVK_Picture: die("picture copy not modelled");
        default: break;
    }
    return dst;
}

Obj *new_object(bool coll = false) {
    Obj *o = new Obj();
    o->rc = 1;
    o->coll = coll;
    g_objects.insert(o);
    return o;
}

void release_object(Obj *o) {
    if(!g_objects.count(o)) { violation("double_release_object"); return; }
    if(--o->rc == 0) {
        for(auto &kv : o->props) clear_var(&kv.second);
        for(auto &v : o->items) clear_var(&v);
        g_objects.erase(o);
        delete o;
    }
}

u16 key_of(const PA_Unistring &k) { return u16(k.fString, k.fLength); }

PA_Variable object_result(Obj *o) {
    PA_Variable v = undefined_var();
    v.fType = o->coll ? eVK_Collection : eVK_Object;
    if(o->coll) v.uValue.fCollection = (PA_CollectionRef)o; else v.uValue.fObject = (PA_ObjectRef)o;
    return v;
}

std::string var_text(const PA_Variable *v) {
    if(v->fType != eVK_Unistring || v->fFiller == kPoisonMark) return "<not text>";
    if(!g_strings.count(v->uValue.fString.fString)) { violation("read_a_disposed_string"); return "<disposed>"; }
    return narrow(u16(v->uValue.fString.fString, v->uValue.fString.fLength));
}

std::string status_text(const PA_Variable *v) {
    if(v->fType != eVK_Object || !v->uValue.fObject) return "<no status>";
    Obj *o = live(v->uValue.fObject);
    auto it = o->props.find(widen("text"));
    return it == o->props.end() ? "<no text>" : var_text(&it->second);
}

void put_text(Obj *o, const char *key, const char *value);

void execute_command(EngineBlock *eb) {
    PA_Variable **params = (PA_Variable **)eb->fPtr1;
    PA_Variable *ret = (PA_Variable *)eb->fPtr2;
    switch(eb->fParam1) {
        case 1007: { /* EXECUTE METHOD(name; result; $2; $3): no `*`, so no result */
            g_execute_method.push_back(var_text(params[0]));
            g_execute_status.push_back(status_text(params[2]));
            break;
        }
        case 1217: { /* JSON Stringify */
            live(params[0]->uValue.fObject);
            if(g_fail_stringify) { poison(ret); eb->fError = -1; break; }
            u16 json = widen("{}");
            ret->fType = eVK_Unistring;
            ret->uValue.fString = new_string(json.c_str(), json.size());
            break;
        }
        case 1230: { /* OB Get type */
            Obj *o = live(params[0]->uValue.fObject);
            auto it = o->props.find(key_of(params[1]->uValue.fString));
            ret->fType = eVK_Longint;
            ret->uValue.fLongint = it == o->props.end() ? eVK_Undefined
                : it->second.fType == eVK_Unistring ? eVK_Text : it->second.fType;
            break;
        }
        case 1231: { /* OB Is defined */
            Obj *o = live(params[0]->uValue.fObject);
            ret->fType = eVK_Boolean;
            ret->uValue.fBoolean = o->props.count(key_of(params[1]->uValue.fString)) ? 1 : 0;
            break;
        }
        case 1471: *ret = object_result(new_object(false)); break; /* New Object */
        case 1566:   /* File(platformPath; fk platform path) */
        case 1567: { /* Folder(platformPath; fk platform path) */
            if(params[1]->fType != eVK_Longint || params[1]->uValue.fLongint != 1) die("unexpected path type");
            std::string platformPath = var_text(params[0]);
            if(g_fail_file_command) { poison(ret); eb->fError = -1; break; }
            Obj *f = new_object(false);
            put_text(f, "path", platformPath.c_str());
            put_text(f, "platformPath", platformPath.c_str());
            *ret = object_result(f);
            break;
        }
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected PA_ExecuteCommandByID %d", (int)eb->fParam1);
            die(msg);
        }
    }
}

void call_method(EngineBlock *eb) {
    if(eb->fParam1 != kOnDataID) die("unknown method id");
    Handle *h = live_handle(eb->fHandle);
    PA_Variable *slots = (PA_Variable *)h->data;
    if(eb->fParam2 != 2) die("unexpected method parameter count");
    g_method_status.push_back(status_text(&slots[1]));
    g_method_userdata.push_back(slots[2].fType == eVK_Object ? (void *)slots[2].uValue.fObject : NULL);
    switch(g_method_result) {
        case kReturnBoolean: slots[0] = undefined_var(); slots[0].fType = eVK_Boolean; break;
        case kReturnObject: {
            Obj *r = new_object(false);
            put_text(r, "note", "method-result");
            slots[0] = object_result(r);
            break;
        }
        case kReturnText: {
            u16 s = widen("method-text");
            slots[0] = undefined_var();
            slots[0].fType = eVK_Unistring;
            slots[0].uValue.fString = new_string(s.c_str(), s.size());
            break;
        }
        case kFail: poison(&slots[0]); eb->fError = -1; break;
    }
}

void get_obj_value(EngineBlock *eb) {
    PA_Variable *out = (PA_Variable *)eb->fPtr2;
    if(!eb->fPtr1) { violation("property_of_null_object"); poison(out); eb->fError = -1; return; }
    Obj *o = live(eb->fPtr1);
    if(eb->fShort == eVK_Collection) die("collection get not modelled");
    if(o->coll) die("object get on a collection");
    u16 k = key_of(eb->fUniString1);
    if(!g_fail_key.empty() && narrow(k) == g_fail_key) { poison(out); eb->fError = -1; return; }
    auto it = o->props.find(k);
    *out = it == o->props.end() ? undefined_var() : copy_var(it->second);
}

void engine(short selector, EngineBlock *eb) {
    switch(selector) {
        case EX_CREATE_UNISTRING: {
            const PA_Unichar *src = (const PA_Unichar *)eb->fHandle;
            size_t len = 0;
            while(src[len]) len++;
            eb->fUniString1 = new_string(src, len);
            break;
        }
        case EX_DISPOSE_UNISTRING:
            free_string(&eb->fUniString1);
            break;
        case EX_VARIABLE_TO_STRING: {
            PA_Variable *v = (PA_Variable *)eb->fHandle;
            if(v->fFiller == kPoisonMark) violation("read_uninitialised_result");
            if(v->fType == eVK_Unistring) eb->fUniString1 = v->uValue.fString; /* borrowed */
            break;
        }
        case EX_CLEAR_VARIABLE:
            clear_var((PA_Variable *)eb->fPtr1);
            break;
        case EX_GET_INFORMATION:
            eb->fParam2 = 0x00002000; /* 4D v20: PA_CreateObject uses New Object */
            break;
        case EX_SET_OBJ_VALUE: {
            Obj *o = live(eb->fPtr1);
            if(eb->fShort == eVK_Collection) die("collection set not modelled");
            PA_Variable value = copy_var(*(PA_Variable *)eb->fPtr2);
            u16 k = key_of(eb->fUniString1);
            auto it = o->props.find(k);
            if(it != o->props.end()) clear_var(&it->second);
            o->props[k] = value;
            break;
        }
        case EX_GET_OBJ_VALUE:
            get_obj_value(eb);
            break;
        case EX_EXECUTE_COMMAND_BY_ID:
            execute_command(eb);
            break;
        case EX_GET_METHOD_ID: {
            u16 name;
            for(int i = 0; eb->fUName[i]; i++) name.push_back(eb->fUName[i]);
            eb->fParam1 = narrow(name) == "onData" ? kOnDataID : 0;
            break;
        }
        case EX_CALL_BY_PROCID:
            call_method(eb);
            break;
        case EX_YIELD_ABSOLUTE:
            break;
        case EX_HANDLE_MANAGER: {
            switch(eb->fParam1) {
                case 1: eb->fHandle = (PA_Handle)new_handle((size_t)eb->fParam2); break;
                case 2: free_handle((Handle *)eb->fHandle); break;
                case 4: eb->fParam2 = (sLONG_PTR)live_handle(eb->fHandle)->size; break;
                case 5: { Handle *h = live_handle(eb->fHandle); h->locks++; eb->fParam3 = (sLONG_PTR)h->data; break; }
                case 6: {
                    Handle *h = live_handle(eb->fHandle);
                    if(h->locks <= 0) violation("unlock_of_unlocked_handle"); else h->locks--;
                    break;
                }
                default: die("unexpected handle manager op");
            }
            break;
        }
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unmodelled engine selector %d", selector);
            die(msg);
        }
    }
}

} // namespace

/* ---------- Vosk stub: synthetic JSON results, no model ---------- */

namespace {
struct FakeVosk { std::string path; };
std::set<void *> g_vosk_live;
std::vector<std::string> g_model_paths;
std::vector<std::string> g_speaker_paths;
int g_accept_calls = 0;
size_t g_accepted_bytes = 0;
/* Sleeps on the first accepted chunk so the 100 ms callback interval of the
   file loop elapses deterministically. */
bool g_slow_first_accept = false;
void *vosk_new(const char *path, std::vector<std::string> &log) {
    log.push_back(path ? path : "");
    FakeVosk *v = new FakeVosk();
    v->path = path ? path : "";
    g_vosk_live.insert(v);
    return v;
}
void vosk_free(void *v) {
    if(!g_vosk_live.erase(v)) die("vosk object freed twice");
    delete (FakeVosk *)v;
}
}

VoskModel *vosk_model_new(const char *model_path) { return (VoskModel *)vosk_new(model_path, g_model_paths); }
void vosk_model_free(VoskModel *m) { vosk_free(m); }
VoskSpkModel *vosk_spk_model_new(const char *model_path) { return (VoskSpkModel *)vosk_new(model_path, g_speaker_paths); }
void vosk_spk_model_free(VoskSpkModel *m) { vosk_free(m); }
VoskRecognizer *vosk_recognizer_new(VoskModel *, float) { std::vector<std::string> l; return (VoskRecognizer *)vosk_new("recognizer", l); }
VoskRecognizer *vosk_recognizer_new_spk(VoskModel *, float, VoskSpkModel *) { std::vector<std::string> l; return (VoskRecognizer *)vosk_new("recognizer-spk", l); }
void vosk_recognizer_free(VoskRecognizer *r) { vosk_free(r); }
int vosk_recognizer_accept_waveform(VoskRecognizer *, const char *, int length) {
    if(g_slow_first_accept && g_accept_calls == 0) usleep(150 * 1000);
    g_accepted_bytes += (size_t)length;
    /* Only the first chunk ends an utterance ("hello"); the rest is partial,
       so the transcript is always "hello" + final "world". */
    return (length > 0 && g_accept_calls++ == 0) ? 1 : 0;
}
const char *vosk_recognizer_result(VoskRecognizer *) { return "{\"text\" : \"hello\"}"; }
const char *vosk_recognizer_partial_result(VoskRecognizer *) { return "{\"partial\" : \"\"}"; }
const char *vosk_recognizer_final_result(VoskRecognizer *) { return "{\"text\" : \"world\"}"; }

/* ---------- PortAudio stub: synthetic in-memory frames, no device ---------- */

namespace {
PaStreamCallback *g_pa_callback = NULL;
void *g_pa_user = NULL;
int g_pa_dummy;
const unsigned long kSyntheticFrames = 1600;
}

PaError Pa_Initialize(void) { return paNoError; }
PaError Pa_Terminate(void) { return paNoError; }
PaError Pa_OpenDefaultStream(PaStream **stream, int numInputChannels, int numOutputChannels, PaSampleFormat sampleFormat,
                             double, unsigned long, PaStreamCallback *streamCallback, void *userData) {
    if(numInputChannels != 1 || numOutputChannels != 0 || sampleFormat != paInt16) die("unexpected stream format");
    g_pa_callback = streamCallback;
    g_pa_user = userData;
    *stream = (PaStream *)&g_pa_dummy;
    return paNoError;
}
PaError Pa_StartStream(PaStream *) {
    std::vector<short> frames(kSyntheticFrames);
    for(size_t i = 0; i < frames.size(); i++) frames[i] = (short)((i % 32) * 512 - 8192);
    g_pa_callback(frames.data(), NULL, frames.size(), NULL, 0, g_pa_user);
    return paNoError;
}
PaError Pa_StopStream(PaStream *) { return paNoError; }
PaError Pa_CloseStream(PaStream *) { g_pa_callback = NULL; return paNoError; }

namespace {

/* ---------- test helpers ---------- */

PA_Unistring U(const char *ascii) {
    u16 s = widen(ascii);
    return new_string(s.c_str(), s.size());
}

void put(Obj *o, const char *key, PA_Variable v) {
    PA_Unistring k = U(key);
    PA_SetObjectProperty((PA_ObjectRef)o, &k, v);
    PA_DisposeUnistring(&k);
}

PA_Variable text_var(const char *value) {
    PA_Variable v = undefined_var();
    PA_Unistring u = U(value);
    PA_SetStringVariable(&v, &u);
    return v;
}

void put_text(Obj *o, const char *key, const char *value) {
    PA_Variable v = text_var(value);
    put(o, key, v);
    PA_ClearVariable(&v);
}

void put_real(Obj *o, const char *key, double d) {
    PA_Variable v = undefined_var();
    PA_SetRealVariable(&v, d);
    put(o, key, v);
}

void put_bool(Obj *o, const char *key, bool b) {
    PA_Variable v = undefined_var();
    PA_SetBooleanVariable(&v, b);
    put(o, key, v);
}

/* Stores a new object under key; the parent holds the only reference. */
Obj *put_object(Obj *o, const char *key) {
    Obj *c = new_object(false);
    PA_Variable v = object_result(c);
    put(o, key, v);
    PA_ClearVariable(&v);
    return c;
}

/* A 4D.File / 4D.Folder stand-in: an object with a platformPath text. */
Obj *put_path_object(Obj *o, const char *key, const char *platformPath) {
    Obj *c = put_object(o, key);
    put_text(c, "platformPath", platformPath);
    return c;
}

bool check(bool ok, const char *what) {
    if(!ok) fprintf(stderr, "  check failed: %s\n", what);
    return ok;
}

bool string_alive(const char *ascii) {
    u16 w = widen(ascii);
    for(void *p : g_strings) if(u16((PA_Unichar *)p) == w) return true;
    return false;
}

bool no_leaks() {
    bool ok = true;
    for(void *p : g_strings) {
        fprintf(stderr, "  LEAK: engine string \"%s\"\n", narrow(u16((PA_Unichar *)p)).c_str());
        ok = false;
    }
    if(!g_pictures.empty()) { fprintf(stderr, "  LEAK: %zu engine picture(s)\n", g_pictures.size()); ok = false; }
    if(!g_objects.empty()) { fprintf(stderr, "  LEAK: %zu engine object(s)/collection(s)\n", g_objects.size()); ok = false; }
    if(!g_handles.empty()) { fprintf(stderr, "  LEAK: %zu engine handle(s)\n", g_handles.size()); ok = false; }
    if(g_poison_clears) { fprintf(stderr, "  %d failed result(s) cleared\n", g_poison_clears); ok = false; }
    if(!g_violations.empty()) ok = false;
    return ok;
}

std::string text_prop(Obj *o, const char *key, bool *has) {
    auto it = o->props.find(widen(key));
    *has = it != o->props.end() && it->second.fType == eVK_Unistring;
    return *has ? narrow(u16(it->second.uValue.fString.fString, it->second.uValue.fString.fLength)) : std::string();
}

double real_prop(Obj *o, const char *key) {
    auto it = o->props.find(widen(key));
    return it != o->props.end() && it->second.fType == eVK_Real ? it->second.uValue.fReal : -99;
}

int bool_prop(Obj *o, const char *key) {
    auto it = o->props.find(widen(key));
    return it != o->props.end() && it->second.fType == eVK_Boolean ? it->second.uValue.fBoolean : -1;
}

/* Calls a plugin command with borrowed object parameters (4D keeps them),
   and returns its result object (owned by the test). */
Obj *call(PA_long32 selector, Obj *p1, Obj *p2) {
    PA_ObjectRef a1 = (PA_ObjectRef)p1, a2 = (PA_ObjectRef)p2;
    PA_ObjectRef result = NULL;
    void *args[2] = { &a1, &a2 };
    PluginBlock pb;
    pb.fParameters = args;
    pb.fResult = &result;
    pb.fData = NULL;
    PluginMain(selector, &pb);
    if(p1) check(g_objects.count(p1) && p1->rc >= 1, "caller parameter 1 alive");
    if(p2) check(g_objects.count(p2) && p2->rc >= 1, "caller parameter 2 alive");
    return (Obj *)result;
}

struct VoskResult { bool success = false; bool has_text = false; std::string text; };

/* vosk(file; options) */
VoskResult call_vosk(Obj *file, Obj *options) {
    Obj *r = call(1, file, options);
    VoskResult v;
    if(!r) { violation("no_result_object"); return v; }
    live(r);
    v.success = bool_prop(r, "success") == 1;
    v.text = text_prop(r, "text", &v.has_text);
    release_object(r);
    return v;
}

void deinit() {
    PluginMain(kDeinitPlugin, NULL);
    if(!g_vosk_live.empty()) violation("vosk_stubs_not_released_on_deinit");
}

/* Loads the stub model directly (as a successful "vosk set options" would). */
void load_stub_model() {
    model = vosk_model_new("stub-model");
    recognizer = vosk_recognizer_new(model, sample_rate);
}

/* Writes synthetic 16-bit PCM after a 44-byte header to a temporary file. */
std::string synthetic_wav() {
    char path[] = "/tmp/vosk-ownership-XXXXXX";
    int fd = mkstemp(path);
    if(fd < 0) die("mkstemp");
    std::vector<char> bytes(44 + 2 * 0x2000, 0);
    memcpy(&bytes[0], "RIFF", 4);
    for(size_t i = 44; i < bytes.size(); i++) bytes[i] = (char)(i * 7);
    if(write(fd, bytes.data(), bytes.size()) != (ssize_t)bytes.size()) die("write");
    close(fd);
    return path;
}

struct TempFile {
    std::string path;
    TempFile() : path(synthetic_wav()) {}
    ~TempFile() { unlink(path.c_str()); }
};

/* ---------- LIVE defects ---------- */

/* LIVE: vosk(File) -> _object_to_path shallow-copied the platformPath string
   borrowed from `p` into cbparams[0], then cleared both (double dispose); it
   also leaked the key, the empty eVK_Unistring owner and the File result. */
bool defect_live_vosk_file_platform_path_double_dispose() {
    TempFile wav;
    load_stub_model();
    Obj *file = new_object();
    put_text(file, "platformPath", wav.path.c_str());
    Obj *options = new_object();
    VoskResult r = call_vosk(file, options);
    bool ok = check(r.success && r.text == "hello world", "transcribed the synthetic file");
    ok &= check(g_accepted_bytes == 2 * 0x2000, "read every synthetic sample");
    release_object(file);
    release_object(options);
    deinit();
    return no_leaks() && ok;
}

/* LIVE: vosk set options with 4D.Folder objects -> same _object_to_path defects. */
bool defect_live_set_options_folder_path_leaks() {
    Obj *options = new_object();
    put_path_object(options, "model", "/models/en");
    put_path_object(options, "speaker", "/models/spk");
    put_real(options, "rate", 8000);
    Obj *r = call(2, options, NULL);
    bool ok = check(r == NULL, "no result");
    ok &= check(g_model_paths.size() == 1 && g_model_paths[0] == "/models/en", "model folder path resolved");
    ok &= check(g_speaker_paths.size() == 1 && g_speaker_paths[0] == "/models/spk", "speaker folder path resolved");
    ok &= check(sample_rate == 8000, "rate applied");
    release_object(options);
    deinit();
    return no_leaks() && ok;
}

/* LIVE: ob_get_n/ob_get_o never cleared the owned PA_GetObjectProperty copy,
   so wrong-kind options (text rate, text model) leaked. */
bool defect_live_set_options_wrong_kind_options_not_cleared() {
    Obj *options = new_object();
    put_text(options, "rate", "fast");
    put_text(options, "model", "not-a-folder");
    Obj *r = call(2, options, NULL);
    bool ok = check(r == NULL && sample_rate == 16000 && g_model_paths.empty(), "options ignored");
    release_object(options);
    ok &= check(!string_alive("fast") && !string_alive("not-a-folder"), "option copies released");
    deinit();
    return no_leaks() && ok;
}

/* LIVE: ob_get_o never released the retained object reference, so the
   output/userData objects outlived the caller's options. */
bool defect_live_vosk_object_options_not_released() {
    Obj *options = new_object();
    Obj *output = put_object(options, "output");
    Obj *userData = put_object(options, "userData");
    put_real(options, "duration", 0);
    VoskResult r = call_vosk(NULL, options);
    bool ok = check(!r.success && !r.has_text, "no model: nothing transcribed");
    ok &= check(output->rc == 1 && userData->rc == 1, "option objects borrowed from the options object");
    release_object(options);
    deinit();
    return no_leaks() && ok;
}

/* LIVE: the value returned by PA_ExecuteMethodByID (file loop and final call)
   was never cleared; the onData name copy (ob_get_a) leaked; userData is
   borrowed from the caller and must come back with its own reference only. */
bool defect_live_on_data_file_method_result_not_cleared() {
    TempFile wav;
    load_stub_model();
    g_slow_first_accept = true;
    g_method_result = kReturnObject;
    Obj *file = new_object();
    put_text(file, "platformPath", wav.path.c_str());
    Obj *options = new_object();
    put_text(options, "onData", "onData");
    Obj *userData = put_object(options, "userData");
    VoskResult r = call_vosk(file, options);
    bool ok = check(r.success && r.text == "hello world", "transcribed");
    ok &= check(g_method_status.size() == 2 && g_method_status[0] == "hello" && g_method_status[1] == "hello world",
                "onData called from the loop and at the end");
    ok &= check(g_method_userdata.size() == 2 && g_method_userdata[0] == userData && g_method_userdata[1] == userData,
                "userData passed as $2");
    ok &= check(g_objects.count(userData) && userData->rc == 1, "userData reference unchanged");
    release_object(file);
    release_object(options);
    deinit();
    return no_leaks() && ok;
}

/* LIVE: same for the audio-input path (synthetic microphone frames). */
bool defect_live_on_data_audio_input_method_result_not_cleared() {
    load_stub_model();
    g_method_result = kReturnText;
    Obj *options = new_object();
    put_text(options, "onData", "onData");
    put_real(options, "duration", 1);
    VoskResult r = call_vosk(NULL, options);
    bool ok = check(r.success && r.text == "hello world", "transcribed the synthetic frames");
    ok &= check(g_method_status.size() == 2 && g_method_status[0] == "hello" && g_method_status[1] == "hello world",
                "onData called from the loop and at the end");
    ok &= check(!string_alive("method-text"), "method results released");
    release_object(options);
    deinit();
    return no_leaks() && ok;
}

/* LIVE: EXECUTE METHOD path (method name unknown to PA_GetMethodID). The
   method name and status objects are owned; userData is borrowed. */
bool defect_live_execute_method_callback() {
    TempFile wav;
    load_stub_model();
    Obj *file = new_object();
    put_text(file, "platformPath", wav.path.c_str());
    Obj *options = new_object();
    put_text(options, "onData", "Formula");
    Obj *userData = put_object(options, "userData");
    VoskResult r = call_vosk(file, options);
    bool ok = check(r.success && r.text == "hello world", "transcribed");
    ok &= check(!g_execute_method.empty() && g_execute_method.back() == "Formula", "EXECUTE METHOD called");
    ok &= check(!g_execute_status.empty() && g_execute_status.back() == "hello world", "final status passed");
    ok &= check(g_objects.count(userData) && userData->rc == 1, "userData reference unchanged");
    release_object(file);
    release_object(options);
    deinit();
    return no_leaks() && ok;
}

/* LIVE: without a loaded model the callback parameters (EXECUTE METHOD name)
   were created and never cleared. */
bool defect_live_callback_params_leak_without_model() {
    Obj *options = new_object();
    put_text(options, "onData", "Formula");
    Obj *userData = put_object(options, "userData");
    VoskResult r = call_vosk(NULL, options);
    bool ok = check(!r.success, "no model: nothing transcribed");
    ok &= check(g_execute_method.empty(), "callback not called");
    ok &= check(g_objects.count(userData) && userData->rc == 1, "userData reference unchanged");
    release_object(options);
    ok &= check(!string_alive("Formula"), "method name released");
    deinit();
    return no_leaks() && ok;
}

/* HARDENING (LIVE): a failed PA_ExecuteMethodByID leaves its result
   uninitialised; it must be neither read nor cleared. (On the baseline this
   test also fails through the onData ob_get_a leak.) */
bool hardening_live_on_data_method_error_result_untouched() {
    load_stub_model();
    g_method_result = kFail;
    Obj *options = new_object();
    put_text(options, "onData", "onData");
    put_real(options, "duration", 0);
    VoskResult r = call_vosk(NULL, options);
    bool ok = check(r.success && r.text == "hello world", "transcribed");
    ok &= check(g_method_status.size() == 1, "onData called once at the end");
    release_object(options);
    deinit();
    return no_leaks() && ok;
}

/* HARDENING (LIVE): a failed PA_GetObjectProperty(platformPath) must be
   neither read nor cleared. */
bool hardening_live_platform_path_error_untouched() {
    load_stub_model();
    Obj *file = new_object();
    put_text(file, "platformPath", "/nonexistent.wav");
    Obj *options = new_object();
    g_fail_key = "platformPath";
    VoskResult r = call_vosk(file, options);
    g_fail_key.clear();
    bool ok = check(!r.success, "nothing transcribed");
    release_object(file);
    release_object(options);
    deinit();
    return no_leaks() && ok;
}

/* HARDENING (LIVE): a failed File (1566) call must be neither read nor cleared. */
bool hardening_live_file_command_error_untouched() {
    load_stub_model();
    Obj *file = new_object();
    put_text(file, "platformPath", "/nonexistent.wav");
    Obj *options = new_object();
    g_fail_file_command = true;
    VoskResult r = call_vosk(file, options);
    g_fail_file_command = false;
    bool ok = check(!r.success, "nothing transcribed");
    release_object(file);
    release_object(options);
    deinit();
    return no_leaks() && ok;
}

/* ---------- JSON helpers called directly ---------- */

/* LIVE helpers (ob_get_a/n/o) and DORMANT ones (ob_get_b/c): on error
   PA_GetObjectProperty leaves its result uninitialised. */
bool hardening_ob_get_error_result_untouched() {
    Obj *o = new_object();
    put_text(o, "a", "x");
    put_real(o, "n", 1.0);
    put_bool(o, "b", true);
    put_object(o, "o");
    Obj *c = new_object(true);
    { PA_Variable v = object_result(c); put(o, "c", v); PA_ClearVariable(&v); }
    CUTF8String s;
    g_fail_key = "a"; bool ra = ob_get_a((PA_ObjectRef)o, L"a", &s);
    g_fail_key = "n"; double d = ob_get_n((PA_ObjectRef)o, L"n");
    g_fail_key = "b"; bool b = ob_get_b((PA_ObjectRef)o, L"b");
    g_fail_key = "o"; PA_ObjectRef ro = ob_get_o((PA_ObjectRef)o, L"o");
    g_fail_key = "c"; PA_CollectionRef rc = ob_get_c((PA_ObjectRef)o, L"c");
    g_fail_key.clear();
    release_object(o);
    bool ok = check(!ra && s.empty() && d == 0.0 && !b && !ro && !rc, "failed reads yield nothing");
    return no_leaks() && ok;
}

/* DORMANT: ob_get_b/ob_get_c never cleared their result either. */
bool defect_dormant_ob_get_b_c_not_cleared() {
    Obj *o = new_object();
    put_text(o, "b", "yes");
    Obj *c = new_object(true);
    { PA_Variable v = object_result(c); put(o, "c", v); PA_ClearVariable(&v); }
    bool b = ob_get_b((PA_ObjectRef)o, L"b");
    PA_CollectionRef rc = ob_get_c((PA_ObjectRef)o, L"c");
    bool ok = check(!b && rc == (PA_CollectionRef)c, "values read");
    ok &= check(c->rc == 1, "collection borrowed from its parent");
    release_object(o);
    return no_leaks() && ok;
}

/* HARDENING (DORMANT): ob_stringify read and cleared a failed 1217 result. */
bool hardening_dormant_ob_stringify_error_untouched() {
    Obj *o = new_object();
    put_text(o, "a", "x");
    g_fail_stringify = true;
    CUTF8String json;
    ob_stringify((PA_ObjectRef)o, &json);
    g_fail_stringify = false;
    release_object(o);
    bool ok = check(json.empty(), "failed stringify yields nothing");
    return no_leaks() && ok;
}

/* ---------- controls (pass on both trees) ---------- */

bool control_get_options() {
    Obj *r = call(3, NULL, NULL);
    bool ok = check(r != NULL, "result returned");
    if(r) {
        bool has;
        ok &= check(real_prop(r, "rate") == 16000 && text_prop(r, "model", &has).empty() && has, "defaults returned");
        release_object(r);
    }
    return no_leaks() && ok;
}

bool control_set_options_real_rate() {
    Obj *options = new_object();
    put_real(options, "rate", 22050);
    Obj *r = call(2, options, NULL);
    release_object(options);
    bool ok = check(r == NULL && sample_rate == 22050 && g_model_paths.empty(), "rate applied, no model");
    deinit();
    return no_leaks() && ok;
}

bool control_vosk_no_model() {
    Obj *options = new_object();
    VoskResult r = call_vosk(NULL, options);
    release_object(options);
    bool ok = check(!r.success && !r.has_text, "no model: nothing transcribed");
    return no_leaks() && ok;
}

bool control_vosk_audio_input_synthetic() {
    load_stub_model();
    Obj *options = new_object();
    put_real(options, "duration", 0);
    VoskResult r = call_vosk(NULL, options);
    release_object(options);
    bool ok = check(r.success && r.text == "hello world", "transcribed the synthetic frames");
    ok &= check(g_accepted_bytes == kSyntheticFrames * sizeof(short), "every synthetic frame accepted");
    deinit();
    return no_leaks() && ok;
}

bool control_ob_set_get_scalars() {
    Obj *o = new_object();
    ob_set_n((PA_ObjectRef)o, L"n", 2.5);
    ob_set_b((PA_ObjectRef)o, L"b", true);
    ob_set_i((PA_ObjectRef)o, L"i", 7);
    ob_set_s((PA_ObjectRef)o, L"s", "text");
    bool ok = check(o->props.size() == 4, "four properties set");
    ok &= check(ob_get_n((PA_ObjectRef)o, L"n") == 2.5 && ob_get_b((PA_ObjectRef)o, L"b"), "real and boolean read back");
    ok &= check(ob_is_defined((PA_ObjectRef)o, L"n") && !ob_is_defined((PA_ObjectRef)o, L"x"), "ob_is_defined");
    release_object(o);
    return no_leaks() && ok;
}

bool control_ob_stringify() {
    Obj *o = new_object();
    put_text(o, "a", "x");
    CUTF8String json;
    ob_stringify((PA_ObjectRef)o, &json);
    release_object(o);
    bool ok = check(json == (const uint8_t *)"{}", "stringified");
    return no_leaks() && ok;
}

struct Test { const char *name; bool (*fn)(); };
const Test kTests[] = {
    { "defect_live_vosk_file_platform_path_double_dispose", defect_live_vosk_file_platform_path_double_dispose },
    { "defect_live_set_options_folder_path_leaks", defect_live_set_options_folder_path_leaks },
    { "defect_live_set_options_wrong_kind_options_not_cleared", defect_live_set_options_wrong_kind_options_not_cleared },
    { "defect_live_vosk_object_options_not_released", defect_live_vosk_object_options_not_released },
    { "defect_live_on_data_file_method_result_not_cleared", defect_live_on_data_file_method_result_not_cleared },
    { "defect_live_on_data_audio_input_method_result_not_cleared", defect_live_on_data_audio_input_method_result_not_cleared },
    { "defect_live_execute_method_callback", defect_live_execute_method_callback },
    { "defect_live_callback_params_leak_without_model", defect_live_callback_params_leak_without_model },
    { "hardening_live_on_data_method_error_result_untouched", hardening_live_on_data_method_error_result_untouched },
    { "hardening_live_platform_path_error_untouched", hardening_live_platform_path_error_untouched },
    { "hardening_live_file_command_error_untouched", hardening_live_file_command_error_untouched },
    { "hardening_ob_get_error_result_untouched", hardening_ob_get_error_result_untouched },
    { "defect_dormant_ob_get_b_c_not_cleared", defect_dormant_ob_get_b_c_not_cleared },
    { "hardening_dormant_ob_stringify_error_untouched", hardening_dormant_ob_stringify_error_untouched },
    { "control_get_options", control_get_options },
    { "control_set_options_real_rate", control_set_options_real_rate },
    { "control_vosk_no_model", control_vosk_no_model },
    { "control_vosk_audio_input_synthetic", control_vosk_audio_input_synthetic },
    { "control_ob_set_get_scalars", control_ob_set_get_scalars },
    { "control_ob_stringify", control_ob_stringify },
};

} // namespace

int main(int argc, char **argv) {
    gCall4D = engine;
    if(argc == 2 && !strcmp(argv[1], "--list")) {
        for(const Test &t : kTests) printf("%s\n", t.name);
        return 0;
    }
    if(argc != 2) { fprintf(stderr, "usage: %s --list | <test>\n", argv[0]); return 2; }
    for(const Test &t : kTests) {
        if(!strcmp(argv[1], t.name)) {
            bool ok = t.fn();
            printf("%s: %s\n", t.name, ok ? "PASS" : "FAIL");
            return ok ? 0 : 1;
        }
    }
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
