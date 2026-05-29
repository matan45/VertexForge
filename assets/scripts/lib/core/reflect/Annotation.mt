// Annotation.mt - Annotation reflection wrapper
// Part of the mType reflection API

class Annotation {
    private int _nativeHandle;

    public constructor(int handle) {
        this._nativeHandle = handle;
    }

    // Get the annotation name
    public function getName(): string {
        string[] params = __reflect_getAnnotationParams(this._nativeHandle);
        // The name is typically stored as a special parameter or derived from handle
        // For now, return empty - needs native implementation
        return "";
    }

    // Get a parameter value by key (legacy string-only accessor; preserved for back-compat)
    public function getParameter(string key): string {
        return __reflect_getAnnotationParam(this._nativeHandle, key);
    }

    // Check if annotation has a parameter
    public function hasParameter(string key): bool {
        return __reflect_hasAnnotationParam(this._nativeHandle, key);
    }

    // Get all parameter keys
    public function getParameterKeys(): string[] {
        return __reflect_getAnnotationParams(this._nativeHandle);
    }

    // ===== MYT-108: typed parameter accessors =====
    // Each typed getter throws at the native layer if the underlying value's
    // declared type does not match. Use isNull(key) to probe nullable params.

    public function getInt(string key): int {
        return __reflect_getAnnotationInt(this._nativeHandle, key);
    }

    public function getFloat(string key): float {
        return __reflect_getAnnotationFloat(this._nativeHandle, key);
    }

    public function getBool(string key): bool {
        return __reflect_getAnnotationBool(this._nativeHandle, key);
    }

    public function getString(string key): string {
        return __reflect_getAnnotationString(this._nativeHandle, key);
    }

    // Returns the class name declared on this parameter. Callers pipe through
    // Class::forName() when they need a Class handle. Kept as string-returning
    // here to avoid a circular import between Annotation.mt and Class.mt
    // (Class.mt itself imports Annotation.mt for getAnnotations()).
    public function getClassName(string key): string {
        return __reflect_getAnnotationClass(this._nativeHandle, key);
    }

    public function isNull(string key): bool {
        return __reflect_isAnnotationParamNull(this._nativeHandle, key);
    }

    // Get the native handle
    public function getNativeHandle(): int {
        return this._nativeHandle;
    }

    // MYT-109: meta-annotations on the annotation's declaration.
    // Returns the Annotation for meta-annotation `metaName` applied to this
    // annotation's declaration (e.g. @Retention, @Target). Null when absent.
    public function getMeta(string metaName): Annotation? {
        int handle = __reflect_getAnnotationMeta(this._nativeHandle, metaName);
        if (handle == 0) {
            return null;
        }
        return new Annotation(handle);
    }

    public function hasMeta(string metaName): bool {
        int handle = __reflect_getAnnotationMeta(this._nativeHandle, metaName);
        return handle != 0;
    }
}
