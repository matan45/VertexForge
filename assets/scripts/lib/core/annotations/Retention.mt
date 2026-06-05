// Retention.mt - marker classes used as values of the built-in @Retention
// meta-annotation. v1 supports SOURCE (stripped before bytecode) and RUNTIME
// (surviving into bytecode / reflection). CLASS-level retention is reserved
// for a future addition.

class SOURCE {}

class RUNTIME {}
