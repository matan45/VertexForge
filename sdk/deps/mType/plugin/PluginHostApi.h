/*
 * mType native plugin C ABI (MYT-289).
 *
 * This is the only header a plugin author needs to include. It is pure C and
 * intentionally pulls no STL or C++ runtime types across the DLL boundary so a
 * plugin compiled with one toolchain (MSVC vNNN, g++, clang) can be loaded by
 * an mType interpreter built with a different toolchain.
 *
 * Lifetime rule for MTypeValue*:
 *   Pointers handed to a plugin (via args, makeXxx, arrayGet, objGet) are
 *   valid only for the duration of the current native call. The host destroys
 *   the per-call arena right after the call returns. If a plugin needs to
 *   retain a primitive across calls, it must copy the underlying scalar out
 *   (e.g. via getInt/getFloat/getString) into its own storage.
 *
 * Error rule:
 *   raiseError does NOT throw. It records the pending error on the call
 *   context; the host trampoline rethrows into the calling .mt script after
 *   the plugin's C frame returns. After calling raiseError, the plugin
 *   should return immediately (e.g. `return host->makeNull(ctx);`).
 *
 * Reentrancy and threading:
 *   All plugin natives run on the single VM thread; the host does not call
 *   them concurrently. From ABI v2 onward, plugins MAY call back into the VM
 *   via host->callFunction / host->callMethod — the inner call runs on the
 *   same thread, and the VM saves and restores its IP/call stack across the
 *   reentrant execution. Plugins must not spawn their own threads that touch
 *   any MTypeContext / MTypeValue.
 */
#ifndef MTYPE_PLUGIN_HOST_API_H
#define MTYPE_PLUGIN_HOST_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bumped on any breaking change to the MTypePluginHost layout. */
#define MTYPE_PLUGIN_ABI_VERSION 2u

#if defined(_WIN32)
#  define MTYPE_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define MTYPE_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* Opaque value handle. Backed internally by a host-arena-owned mType Value. */
typedef struct MTypeValue   MTypeValue;
/* Opaque per-call context. Carries env/vm/arena/error-slot. */
typedef struct MTypeContext MTypeContext;

typedef enum {
    MT_TAG_VOID   = 0,
    MT_TAG_NULL   = 1,
    MT_TAG_BOOL   = 2,
    MT_TAG_INT    = 3,
    MT_TAG_FLOAT  = 4,
    MT_TAG_STRING = 5,
    MT_TAG_ARRAY  = 6,
    MT_TAG_OBJECT = 7,
    MT_TAG_OTHER  = 255  /* lambda, promise, etc. */
} MTypeTag;

typedef enum {
    MT_OK          = 0,
    MT_ERR_TYPE    = 1,
    MT_ERR_RANGE   = 2,
    MT_ERR_GENERIC = 3
} MTypeStatus;

/* Plugin-implemented native function signature. */
typedef MTypeValue* (*MTypeNativeFn)(void* userData,
                                     MTypeContext* ctx,
                                     const MTypeValue* const* args,
                                     int argc);

/* Callback invoked once per name during listClasses / listFunctions. The
 * `name` pointer is valid only for the duration of the callback. */
typedef void (*MTypeNameCallback)(void* userData, const char* name);

/*
 * Function-pointer table the host hands to the plugin. The plugin uses these
 * for ALL interactions with mType. abiVersion is the FIRST field so the plugin
 * can validate before touching anything else.
 */
typedef struct MTypePluginHost {
    uint32_t abiVersion;
    uint32_t _reserved;  /* alignment / future use */

    /* ---- Constructors. Returned MTypeValue lifetime is the current call. ---- */
    MTypeValue* (*makeNull) (MTypeContext* ctx);
    MTypeValue* (*makeVoid) (MTypeContext* ctx);
    MTypeValue* (*makeBool) (MTypeContext* ctx, int v);
    MTypeValue* (*makeInt)  (MTypeContext* ctx, int64_t v);
    MTypeValue* (*makeFloat)(MTypeContext* ctx, double v);
    /* utf8 must be valid for the duration of this call. Length in bytes. */
    MTypeValue* (*makeString)(MTypeContext* ctx, const char* utf8, size_t len);
    /* Allocate a fixed-size array of the given element tag. mType arrays do
     * not grow; size is final. Element-tag-driven storage selection mirrors
     * the engine's NativeArray. */
    MTypeValue* (*makeArray)(MTypeContext* ctx, MTypeTag elemTag, size_t length);
    /* Construct an instance of a class registered in the host environment. */
    MTypeValue* (*makeObject)(MTypeContext* ctx, const char* className);

    /* ---- Inspection. ---- */
    MTypeTag    (*getTag)   (const MTypeValue* v);
    int         (*getBool)  (const MTypeValue* v);
    int64_t     (*getInt)   (const MTypeValue* v);
    double      (*getFloat) (const MTypeValue* v);
    /* Returns NUL-terminated UTF-8. *outLen receives byte length when non-null.
     * Pointer remains valid only for the duration of the current call. */
    const char* (*getString)(const MTypeValue* v, size_t* outLen);

    /* ---- Arrays. arrayGet/arraySet are bounds-checked. ---- */
    size_t      (*arrayLen) (const MTypeValue* arr);
    MTypeValue* (*arrayGet) (MTypeContext* ctx, const MTypeValue* arr, size_t index);
    MTypeStatus (*arraySet) (const MTypeValue* arr, size_t index, const MTypeValue* v);

    /* ---- Objects. Field access by name. ---- */
    MTypeValue* (*objGet)   (MTypeContext* ctx, const MTypeValue* obj, const char* fieldName);
    MTypeStatus (*objSet)   (const MTypeValue* obj, const char* fieldName, const MTypeValue* v);

    /* ---- Registration. Only valid during mtype_plugin_register. ---- */
    void        (*registerFunction)(MTypeContext* ctx,
                                    const char* name,
                                    MTypeNativeFn fn,
                                    void* userData);

    /* ---- Errors. Records pending error; host trampoline rethrows after return. ---- */
    void        (*raiseError)(MTypeContext* ctx,
                              const char* exceptionType,
                              const char* message);

    /* ---- ABI v2: enumeration and reentrancy ---- */

    /* Existence checks. Return 1 if present, 0 otherwise. hasFunction looks at
     * both global mType functions and registered natives (in that order). */
    int         (*hasClass)   (MTypeContext* ctx, const char* className);
    int         (*hasFunction)(MTypeContext* ctx, const char* funcName);

    /* Enumerate every registered class / function name in alphabetical order.
     * The callback is invoked once per name. Each `name` is owned by the host
     * registry and is valid only for the duration of that callback invocation. */
    void        (*listClasses)  (MTypeContext* ctx, MTypeNameCallback cb, void* userData);
    void        (*listFunctions)(MTypeContext* ctx, MTypeNameCallback cb, void* userData);

    /* Reentrant calls back into mType. Both run on the VM thread (the same
     * thread as the calling native), saving and restoring the VM's IP and
     * call stack across the inner execution.
     *
     * Returns the function's return value (or makeVoid() equivalent if the
     * callee returned void). On exception during execution, the host records
     * a pending error via the same mechanism as raiseError — the trampoline
     * will surface it to the original `.mt` caller after the plugin's outer
     * function returns. After a failed callFunction/callMethod, the plugin
     * should return promptly. */
    MTypeValue* (*callFunction)(MTypeContext* ctx,
                                const char* funcName,
                                const MTypeValue* const* args,
                                int argc);
    MTypeValue* (*callMethod)  (MTypeContext* ctx,
                                const MTypeValue* receiver,
                                const char* methodName,
                                const MTypeValue* const* args,
                                int argc);
} MTypePluginHost;

/*
 * Plugin entry point. The loader calls this exactly once per loaded plugin.
 * Return 0 on success, non-zero on failure (e.g. ABI mismatch). On failure
 * the loader closes the library and reports the failure to the .mt script.
 */
MTYPE_PLUGIN_EXPORT int mtype_plugin_register(uint32_t hostAbiVersion,
                                              const MTypePluginHost* host,
                                              MTypeContext* registrationCtx);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* MTYPE_PLUGIN_HOST_API_H */
