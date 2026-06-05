// Parameter.mt - Method/Constructor parameter information
// Part of the mType reflection API

import * from "Annotation.mt";

class Parameter {
    private string _name;
    private string _typeName;
    private int _index;
    // MYT-110: parent handle (either a method handle or a constructor
    // handle, disambiguated by _isCtor) so native annotation lookups can
    // resolve back to the declaring {Method,Constructor}Definition.
    private int _parentHandle;
    private bool _isCtor;

    public constructor(string name, string typeName, int index, int parentHandle, bool isCtor) {
        this._name = name;
        this._typeName = typeName;
        this._index = index;
        this._parentHandle = parentHandle;
        this._isCtor = isCtor;
    }

    // Get the parameter name
    public function getName(): string {
        return this._name;
    }

    // Get the parameter type name
    public function getTypeName(): string {
        return this._typeName;
    }

    // Get the parameter index (0-based)
    public function getIndex(): int {
        return this._index;
    }

    // ===== MYT-110: per-parameter annotation reflection =====
    public function hasAnnotation(string name): bool {
        if (this._isCtor) {
            return __reflect_hasConstructorParameterAnnotation(this._parentHandle, this._index, name);
        }
        return __reflect_hasMethodParameterAnnotation(this._parentHandle, this._index, name);
    }

    public function getAnnotation(string name): Annotation? {
        int handle = 0;
        if (this._isCtor) {
            handle = __reflect_getConstructorParameterAnnotation(this._parentHandle, this._index, name);
        } else {
            handle = __reflect_getMethodParameterAnnotation(this._parentHandle, this._index, name);
        }
        if (handle == 0) {
            return null;
        }
        return new Annotation(handle);
    }

    public function getAnnotations(): Annotation[] {
        int[] handles = new int[0];
        if (this._isCtor) {
            handles = __reflect_getConstructorParameterAnnotations(this._parentHandle, this._index);
        } else {
            handles = __reflect_getMethodParameterAnnotations(this._parentHandle, this._index);
        }
        Annotation[] result = new Annotation[handles.length];
        for (int i = 0; i < handles.length; i = i + 1) {
            result[i] = new Annotation(handles[i]);
        }
        return result;
    }
}
