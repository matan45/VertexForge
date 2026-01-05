// Class.mt - Main reflection entry point for class introspection
// Part of the mType reflection API

import * from "Modifier.mt";
import * from "Field.mt";
import * from "Method.mt";
import * from "Constructor.mt";
import * from "Annotation.mt";

class Class {
    // Native handle to ClassDefinition
    private int _nativeHandle;
    private string _className;

    // Private constructor - instances created via forName()
    private constructor(int handle, string name) {
        this._nativeHandle = handle;
        this._className = name;
    }

    // Get Class object from class name
    public static function forName(string className): Class {
        int handle = __reflect_forName(className);
        return new Class(handle, className);
    }

    // Get the fully qualified class name
    public function getName(): string {
        return this._className;
    }

    // Get the simple class name (without package)
    public function getSimpleName(): string {
        return __reflect_getSimpleName(this._nativeHandle);
    }

    // Get the superclass Class object (or null if no parent)
    public function getSuperclass(): Class {
        int parentHandle = __reflect_getSuperclass(this._nativeHandle);
        if (parentHandle == 0) {
            return null;
        }
        string parentName = __reflect_getSimpleName(parentHandle);
        return new Class(parentHandle, parentName);
    }

    // Get implemented interfaces as string array
    public function getInterfaces(): string[] {
        return __reflect_getInterfaces(this._nativeHandle);
    }

    // Check if this represents an interface (currently always false for classes)
    public function isInterface(): bool {
        return __reflect_isInterface(this._nativeHandle);
    }

    // Check if class is abstract
    public function isAbstract(): bool {
        return __reflect_isAbstract(this._nativeHandle);
    }

    // Check if class is final
    public function isFinal(): bool {
        return __reflect_isFinal(this._nativeHandle);
    }

    // Check if this class is assignable from another class
    public function isAssignableFrom(Class other): bool {
        return __reflect_isAssignableFrom(this._nativeHandle, other._nativeHandle);
    }

    // Get modifier flags for this class
    public function getModifiers(): int {
        return __reflect_getClassModifiers(this._nativeHandle);
    }

    // Check if this is a generic class
    public function isGeneric(): bool {
        return __reflect_isGenericClass(this._nativeHandle);
    }

    // Get generic type parameter names
    public function getTypeParameters(): string[] {
        return __reflect_getTypeParameters(this._nativeHandle);
    }

    // Get a public field by name (includes inherited fields)
    public function getField(string name): Field {
        int fieldHandle = __reflect_getField(this._nativeHandle, name, false);
        return new Field(fieldHandle, this._nativeHandle, name);
    }

    // Get a declared field by name (this class only, any access level)
    public function getDeclaredField(string name): Field {
        int fieldHandle = __reflect_getField(this._nativeHandle, name, true);
        return new Field(fieldHandle, this._nativeHandle, name);
    }

    // Get all public fields (includes inherited)
    public function getFields(): Field[] {
        int[] fieldHandles = __reflect_getFields(this._nativeHandle, false);
        Field[] fields = new Field[fieldHandles.length];
        for (int i = 0; i < fieldHandles.length; i = i + 1) {
            string fieldName = __reflect_getFieldName(fieldHandles[i]);
            fields[i] = new Field(fieldHandles[i], this._nativeHandle, fieldName);
        }
        return fields;
    }

    // Get all declared fields (this class only, any access level)
    public function getDeclaredFields(): Field[] {
        int[] fieldHandles = __reflect_getFields(this._nativeHandle, true);
        Field[] fields = new Field[fieldHandles.length];
        for (int i = 0; i < fieldHandles.length; i = i + 1) {
            string fieldName = __reflect_getFieldName(fieldHandles[i]);
            fields[i] = new Field(fieldHandles[i], this._nativeHandle, fieldName);
        }
        return fields;
    }

    // Get a public method by name and parameter types
    public function getMethod(string name, int paramCount): Method {
        int[] emptyTypes = new int[paramCount];
        int methodHandle = __reflect_getMethod(this._nativeHandle, name, emptyTypes, false);
        return new Method(methodHandle, this._nativeHandle, name);
    }

    // Get a declared method by name and parameter count
    public function getDeclaredMethod(string name, int paramCount): Method {
        int[] emptyTypes = new int[paramCount];
        int methodHandle = __reflect_getMethod(this._nativeHandle, name, emptyTypes, true);
        return new Method(methodHandle, this._nativeHandle, name);
    }

    // Get all public methods (includes inherited)
    public function getMethods(): Method[] {
        int[] methodHandles = __reflect_getMethods(this._nativeHandle, false);
        Method[] methods = new Method[methodHandles.length];
        for (int i = 0; i < methodHandles.length; i = i + 1) {
            string methodName = __reflect_getMethodName(methodHandles[i]);
            methods[i] = new Method(methodHandles[i], this._nativeHandle, methodName);
        }
        return methods;
    }

    // Get all declared methods (this class only, any access level)
    public function getDeclaredMethods(): Method[] {
        int[] methodHandles = __reflect_getMethods(this._nativeHandle, true);
        Method[] methods = new Method[methodHandles.length];
        for (int i = 0; i < methodHandles.length; i = i + 1) {
            string methodName = __reflect_getMethodName(methodHandles[i]);
            methods[i] = new Method(methodHandles[i], this._nativeHandle, methodName);
        }
        return methods;
    }

    // Get a public constructor by parameter count
    public function getConstructor(int paramCount): Constructor {
        int[] emptyTypes = new int[paramCount];
        int ctorHandle = __reflect_getConstructor(this._nativeHandle, emptyTypes, false);
        return new Constructor(ctorHandle, this._nativeHandle);
    }

    // Get a declared constructor by parameter count
    public function getDeclaredConstructor(int paramCount): Constructor {
        int[] emptyTypes = new int[paramCount];
        int ctorHandle = __reflect_getConstructor(this._nativeHandle, emptyTypes, true);
        return new Constructor(ctorHandle, this._nativeHandle);
    }

    // Get all public constructors
    public function getConstructors(): Constructor[] {
        int[] ctorHandles = __reflect_getConstructors(this._nativeHandle, false);
        Constructor[] ctors = new Constructor[ctorHandles.length];
        for (int i = 0; i < ctorHandles.length; i = i + 1) {
            ctors[i] = new Constructor(ctorHandles[i], this._nativeHandle);
        }
        return ctors;
    }

    // Get all declared constructors (any access level)
    public function getDeclaredConstructors(): Constructor[] {
        int[] ctorHandles = __reflect_getConstructors(this._nativeHandle, true);
        Constructor[] ctors = new Constructor[ctorHandles.length];
        for (int i = 0; i < ctorHandles.length; i = i + 1) {
            ctors[i] = new Constructor(ctorHandles[i], this._nativeHandle);
        }
        return ctors;
    }

    // Get all annotations on this class
    public function getAnnotations(): Annotation[] {
        int[] annotationHandles = __reflect_getClassAnnotations(this._nativeHandle);
        Annotation[] annotations = new Annotation[annotationHandles.length];
        for (int i = 0; i < annotationHandles.length; i = i + 1) {
            annotations[i] = new Annotation(annotationHandles[i]);
        }
        return annotations;
    }

    // Get a specific annotation by name
    public function getAnnotation(string name): Annotation {
        int annotationHandle = __reflect_getClassAnnotation(this._nativeHandle, name);
        if (annotationHandle == 0) {
            return null;
        }
        return new Annotation(annotationHandle);
    }

    // Check if class has a specific annotation
    public function hasAnnotation(string name): bool {
        return __reflect_hasClassAnnotation(this._nativeHandle, name);
    }

    // Get the native handle (for internal use)
    public function getNativeHandle(): int {
        return this._nativeHandle;
    }
}
