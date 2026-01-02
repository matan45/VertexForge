// Method.mt - Method reflection wrapper
// Part of the mType reflection API

import * from "AccessibleObject.mt";
import * from "Modifier.mt";

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

    // Get the native handle
    public function getNativeHandle(): int {
        return this._nativeHandle;
    }
}
