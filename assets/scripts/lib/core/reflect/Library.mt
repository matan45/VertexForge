// Library.mt - Runtime library loading and unloading API
// Part of the mType reflection API

class Library {

    // Load a .mtcLib library at runtime by file path.
    // Resolves and loads all transitive dependencies automatically.
    // Throws if the file does not exist, is not a valid .mtcLib, or a dependency is missing.
    public static function load(string path): void {
        loadLibrary(path);
    }

    // Unload a previously loaded library by name.
    // Removes all symbols (classes, interfaces, functions) that the library exported.
    // Throws if the library is not loaded or if another loaded library depends on it.
    public static function unload(string name): void {
        unloadLibrary(name);
    }
}
