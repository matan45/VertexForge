// Modifier.mt - Constants and utilities for access modifiers
// Part of the mType reflection API

class Modifier {
    // Modifier flag constants (bit flags)
    public static final int PUBLIC = 1;
    public static final int PRIVATE = 2;
    public static final int PROTECTED = 4;
    public static final int STATIC = 8;
    public static final int FINAL = 16;
    public static final int ABSTRACT = 32;
    public static final int ASYNC = 64;

    // Check if modifier flags include PUBLIC
    public static function isPublic(int mod): bool {
        return (mod & PUBLIC) != 0;
    }

    // Check if modifier flags include PRIVATE
    public static function isPrivate(int mod): bool {
        return (mod & PRIVATE) != 0;
    }

    // Check if modifier flags include PROTECTED
    public static function isProtected(int mod): bool {
        return (mod & PROTECTED) != 0;
    }

    // Check if modifier flags include STATIC
    public static function isStatic(int mod): bool {
        return (mod & STATIC) != 0;
    }

    // Check if modifier flags include FINAL
    public static function isFinal(int mod): bool {
        return (mod & FINAL) != 0;
    }

    // Check if modifier flags include ABSTRACT
    public static function isAbstract(int mod): bool {
        return (mod & ABSTRACT) != 0;
    }

    // Check if modifier flags include ASYNC
    public static function isAsync(int mod): bool {
        return (mod & ASYNC) != 0;
    }

    // Combine two modifier flags (bitwise OR)
    public static function combine(int mod1, int mod2): int {
        return mod1 | mod2;
    }

    // Bitwise AND of modifiers
    public static function and(int mod1, int mod2): int {
        return mod1 & mod2;
    }

    // Bitwise XOR of modifiers
    public static function xor(int mod1, int mod2): int {
        return mod1 ^ mod2;
    }

    // Bitwise NOT of modifier
    public static function not(int mod): int {
        return ~mod;
    }

    // Convert modifier flags to string representation
    public static function toString(int mod): string {
        string result = "";
        if (isPublic(mod)) {
            result = result + "public ";
        }
        if (isPrivate(mod)) {
            result = result + "private ";
        }
        if (isProtected(mod)) {
            result = result + "protected ";
        }
        if (isStatic(mod)) {
            result = result + "static ";
        }
        if (isFinal(mod)) {
            result = result + "final ";
        }
        if (isAbstract(mod)) {
            result = result + "abstract ";
        }
        if (isAsync(mod)) {
            result = result + "async ";
        }
        return result;
    }
}
