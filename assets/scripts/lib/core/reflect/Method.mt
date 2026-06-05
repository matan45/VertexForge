// Method.mt - Method reflection wrapper
// Part of the mType reflection API

import * from "AccessibleObject.mt";
import * from "Modifier.mt";
import * from "Annotation.mt";
import * from "Parameter.mt";

class Method extends AccessibleObject {
    private int _nativeHandle;
    private int _classHandle;
    private string _name;

    public constructor(int handle, int classHandle, string name) : super() {
        this._nativeHandle = handle;
        this._classHandle = classHandle;
        this._name = name;
    }

    // Get the method name
    public function getName(): string {
        return this._name;
    }

    // Get the return type as string
    public function getReturnType(): string {
        return __reflect_getMethodReturnType(this._nativeHandle);
    }

    // Get parameter types as string array
    public function getParameterTypes(): string[] {
        return __reflect_getMethodParamTypes(this._nativeHandle);
    }

    // Get parameter count
    public function getParameterCount(): int {
        return __reflect_getMethodParamCount(this._nativeHandle);
    }

    // Get parameter names
    public function getParameterNames(): string[] {
        return __reflect_getMethodParameters(this._nativeHandle);
    }

    // Get the declaring class handle
    public function getDeclaringClassHandle(): int {
        return this._classHandle;
    }

    // Get modifier flags
    public function getModifiers(): int {
        return __reflect_getMethodModifiers(this._nativeHandle);
    }

    // Check if method is static
    public function isStatic(): bool {
        return Modifier::isStatic(this.getModifiers());
    }

    // Check if method is abstract
    public function isAbstract(): bool {
        return Modifier::isAbstract(this.getModifiers());
    }

    // Check if method is final
    public function isFinal(): bool {
        return Modifier::isFinal(this.getModifiers());
    }

    // Check if method is async
    public function isAsync(): bool {
        return __reflect_isMethodAsync(this._nativeHandle);
    }

    // Check if method is public
    public function isPublic(): bool {
        return Modifier::isPublic(this.getModifiers());
    }

    // Check if method is private
    public function isPrivate(): bool {
        return Modifier::isPrivate(this.getModifiers());
    }

    // Check if method is protected
    public function isProtected(): bool {
        return Modifier::isProtected(this.getModifiers());
    }

    // Check if method has generic type parameters
    public function isGeneric(): bool {
        return __reflect_isMethodGeneric(this._nativeHandle);
    }

    // Invoke this method on an object instance with arguments
    // Returns the method's return value as Object
    public function invoke(Object instance, Object[] args): Object {
        return __reflect_invokeMethod(instance, this._nativeHandle, args, this._accessible);
    }

    // Get the class handle (for direct native invocation calls)
    public function getClassHandle(): int {
        return this._classHandle;
    }

    // Get the native handle
    public function getNativeHandle(): int {
        return this._nativeHandle;
    }

    // ===== MYT-108: per-method annotation reflection =====
    public function hasAnnotation(string name): bool {
        return __reflect_hasMethodAnnotation(this._nativeHandle, name);
    }

    public function getAnnotation(string name): Annotation? {
        int handle = __reflect_getMethodAnnotation(this._nativeHandle, name);
        if (handle == 0) {
            return null;
        }
        return new Annotation(handle);
    }

    // MYT-110: Parameter[] with per-parameter annotation access.
    public function getParameters(): Parameter[] {
        string[] names = __reflect_getMethodParameters(this._nativeHandle);
        string[] types = __reflect_getMethodParamTypes(this._nativeHandle);
        Parameter[] result = new Parameter[names.length];
        for (int i = 0; i < names.length; i = i + 1) {
            result[i] = new Parameter(names[i], types[i], i, this._nativeHandle, false);
        }
        return result;
    }
}
