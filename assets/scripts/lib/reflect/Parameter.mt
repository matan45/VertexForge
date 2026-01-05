// Parameter.mt - Method/Constructor parameter information
// Part of the mType reflection API

class Parameter {
    private string _name;
    private string _typeName;
    private int _index;

    public constructor(string name, string typeName, int index) {
        this._name = name;
        this._typeName = typeName;
        this._index = index;
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
}
