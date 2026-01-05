// Constructor.mt - Constructor reflection wrapper
// Part of the mType reflection API

import * from "AccessibleObject.mt";
import * from "Modifier.mt";

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

    // Get the native handle
    public function getNativeHandle(): int {
        return this._nativeHandle;
    }
}
