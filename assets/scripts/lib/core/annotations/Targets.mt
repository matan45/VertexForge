// Targets.mt - marker classes used as values of the built-in @Target
// meta-annotation. Each marker names a host kind that an annotation may be
// applied to. Declared as empty classes so user code can reference them
// through `import` and so the compile-time validator can cross-check target
// sets against host kinds by name.

class METHOD {}

class FIELD {}

class CONSTRUCTOR {}

class CLASS {}

class FUNCTION {}

class ANNOTATION {}

// MYT-110
class PARAMETER {}
