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

    // Get a parameter value by key
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

    // Get the native handle
    public function getNativeHandle(): int {
        return this._nativeHandle;
    }
}
