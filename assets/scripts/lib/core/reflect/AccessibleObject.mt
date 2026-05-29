// AccessibleObject.mt - Base class for accessible members (Field, Method, Constructor)
// Part of the mType reflection API

class AccessibleObject {
    protected bool _accessible;

    public constructor() {
        this._accessible = false;
    }

    // Enable or disable access control bypass
    // When set to true, allows access to private/protected members
    // Also bypasses final field restrictions
    public function setAccessible(bool flag): void {
        this._accessible = flag;
    }

    // Check if access control bypass is enabled
    public function isAccessible(): bool {
        return this._accessible;
    }
}
