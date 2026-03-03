class Json {

    public static function <T> serialize(T obj): string {
        return __json_serialize(obj);
    }

    public static function <T> serializeWithOptions(T obj, bool includeStatic, bool prettyPrint): string {
        return __json_serializeWithOptions(obj, includeStatic, prettyPrint);
    }

    public static function <T> deserialize(string jsonString): T {
        return __json_deserialize(jsonString);
    }

    public static function <T> deserializeAs(string jsonString, string className): T {
        return __json_deserializeAs(jsonString, className);
    }

    public static function <T> stringify(T obj): string {
        return __json_stringify(obj, true);
    }

    public static function <T> compact(T obj): string {
        return __json_stringify(obj, false);
    }

    public static function <T> readFromFile(string filePath, string className): T {
        string content = __json_readFile(filePath);
        return __json_deserializeAs(content, className);
    }

    public static function <T> writeToFile(string filePath, T obj): void {
        string json = __json_serialize(obj);
        __json_writeFile(filePath, json);
    }

    public static function <T> writeToFileWithOptions(string filePath, T obj, bool includeStatic, bool prettyPrint): void {
        string json = __json_serializeWithOptions(obj, includeStatic, prettyPrint);
        __json_writeFile(filePath, json);
    }

    public static function format(string jsonString): string {
        return __json_format(jsonString);
    }
}
