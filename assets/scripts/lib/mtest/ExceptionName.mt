// Shared helper — extract a best-effort runtime class name from an
// Exception instance. Relies on the `<ClassName>: <message>` convention
// used by the stdlib exception toString() overrides in lib/exceptions/.
// If no colon is present, the entire toString() is returned as-is.
//
// Wrapped as a static-method holder (rather than a top-level function) so
// the barrel import + consumer-file imports don't produce a duplicate
// function-signature error.
//
// v1 limitation: this is string-parsing, not true runtime type lookup.
// When mType exposes a runtime-class accessor on Object, replace the body.
import * from "../core/exceptions/Exception.mt";

public class ExceptionName {
    public static function of(Exception e): string {
        string repr = e.toString();
        int colonIdx = indexOf(repr, ":");
        if (colonIdx <= 0) {
            return repr;
        }
        return substring(repr, 0, colonIdx);
    }
}
