// Deliberately does not compile: tools/injected_failures.py checks that dev.cmd fails at the build step and reports
// this error (C2065, undeclared identifier) as the first diagnostic.
int injected_compile_failure() { return undeclared_on_purpose; }
