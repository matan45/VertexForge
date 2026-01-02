// Field.mt - Field reflection wrapper
// Part of the mType reflection API

import * from "AccessibleObject.mt";
import * from "Modifier.mt";

class Field extends AccessibleObject {
    private int _nativeHandle;
    private int _classHandle;
    private string _name;

    public constructor(int handle, int classHandle, string name) : super() {
        this._nativeHandle = handle;
        this._classHandle = classHandle;
        this._name = name;
    }

    // Get the field name
    public function getName(): string {
        return this._name;
    }

    // Get the field type as string
    public function getType(): string {
        return __reflect_getFieldType(this._nativeHandle);
    }

    // Get the declaring class handle
    public function getDeclaringClassHandle(): int {
        return this._classHandle;
    }

    // Get modifier flags
    public function getModifiers(): int {
        return __reflect_getFieldModifiers(this._nativeHandle);
    }

    // Check if field is static
    public function isStatic(): bool {
        return Modifier::isStatic(this.getModifiers());
    }

    // Check if field is final
    public function isFinal(): bool {
        return Modifier::isFinal(this.getModifiers());
    }

    // Check if field is public
    public function isPublic(): bool {
        return Modifier::isPublic(this.getModifiers());
    }

    // Check if field is private
    public function isPrivate(): bool {
        return Modifier::isPrivate(this.getModifiers());
    }

    // Check if field is protected
    public function isProtected(): bool {
        return Modifier::isProtected(this.getModifiers());
    }

    // Get field value as int from instance (pass instance native handle, or 0 for static)
    public function getInt(int instanceHandle): int {
        if (this.isStatic()) {
            return __reflect_getStaticFieldValue(this._classHandle, this._nativeHandle);
        }
        return __reflect_getFieldValue(instanceHandle, this._nativeHandle, this._accessible);
    }

    // Set field value as int on instance (pass instance native handle, or 0 for static)
    public function setInt(int instanceHandle, int value): void {
        if (this.isStatic()) {
            __reflect_setStaticFieldValue(this._classHandle, this._nativeHandle, value);
            return;
        }
        __reflect_setFieldValue(instanceHandle, this._nativeHandle, value, this._accessible);
    }

    // Get the native handle
    public function getNativeHandle(): int {
        return this._nativeHandle;
    }
}
