// Constructor.mt - Constructor reflection wrapper
// Part of the mType reflection API

import * from "AccessibleObject.mt";
import * from "Modifier.mt";
import * from "Annotation.mt";
import * from "Parameter.mt";

class Constructor extends AccessibleObject {
    private int _nativeHandle;
    private int _classHandle;

    public constructor(int handle, int classHandle) : super() {
        this._nativeHandle = handle;
        this._classHandle = classHandle;
    }

    // Get the declaring class handle
    public function getDeclaringClassHandle(): int {
        return this._classHandle;
    }

    // Get parameter types as string array
    public function getParameterTypes(): string[] {
        return __reflect_getConstructorParamTypes(this._nativeHandle);
    }

    // Get parameter count
    public function getParameterCount(): int {
        return __reflect_getConstructorParamCount(this._nativeHandle);
    }

    // Get parameter names
    public function getParameterNames(): string[] {
        return __reflect_getConstructorParameters(this._nativeHandle);
    }

    // Get modifier flags
    public function getModifiers(): int {
        return __reflect_getConstructorModifiers(this._nativeHandle);
    }

    // Check if constructor is public
    public function isPublic(): bool {
        return Modifier::isPublic(this.getModifiers());
    }

    // Check if constructor is private
    public function isPrivate(): bool {
        return Modifier::isPrivate(this.getModifiers());
    }

    // Check if constructor is protected
    public function isProtected(): bool {
        return Modifier::isProtected(this.getModifiers());
    }

    // Create a new instance using this constructor with arguments
    // Returns the newly created object as Object
    public function newInstance(Object[] args): Object {
        return __reflect_newInstanceWithArgs(this._classHandle, this._nativeHandle, args, this._accessible);
    }

    // Get the class handle (for direct native invocation calls)
    public function getClassHandle(): int {
        return this._classHandle;
    }

    // Get the native handle
    public function getNativeHandle(): int {
        return this._nativeHandle;
    }

    // MYT-109: annotation reflection on constructors.

    public function getAnnotations(): Annotation[] {
        int[] handles = __reflect_getConstructorAnnotations(this._nativeHandle);
        Annotation[] result = new Annotation[handles.length];
        for (int i = 0; i < handles.length; i = i + 1) {
            result[i] = new Annotation(handles[i]);
        }
        return result;
    }

    public function getAnnotation(string annotationName): Annotation? {
        int handle = __reflect_getConstructorAnnotation(this._nativeHandle, annotationName);
        if (handle == 0) {
            return null;
        }
        return new Annotation(handle);
    }

    public function hasAnnotation(string annotationName): bool {
        return __reflect_hasConstructorAnnotation(this._nativeHandle, annotationName);
    }

    // MYT-110: Parameter[] with per-parameter annotation access.
    public function getParameters(): Parameter[] {
        string[] names = __reflect_getConstructorParameters(this._nativeHandle);
        string[] types = __reflect_getConstructorParamTypes(this._nativeHandle);
        Parameter[] result = new Parameter[names.length];
        for (int i = 0; i < names.length; i = i + 1) {
            result[i] = new Parameter(names[i], types[i], i, this._nativeHandle, true);
        }
        return result;
    }
}
