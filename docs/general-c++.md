# C++ coding guidelines

These coding rules apply to all C++ code in the project.
PowerTOP uses the C++ 20 language standard


## Language standard

- [ ] Use only constructs in the C++ 20 standard that are non-deprecated or
    obsoleted.

- [ ] Undefined behavior (UB) language constructs are a violation of coding
    style

- [ ] Any use of fixed sized buffers (example: char foo[128]) must be
    **provably** correct to not overflow the buffer in any condition

- [ ] "using namespace std;" is a deprecated construct

## STL

- [ ] Strongly prefer STL constructs over open-coded equivalent constructs

- [ ] Prefer std::string over C style "char *", except when interfacing with
    C class libraries

- [ ] Use "std::<element>" instead of "<element>" even when code uses 
    "using namespace std".

## General C++ coding rules

- [ ] Use "const" when possible for method and function parameters

- [ ] Use the RAII pattern for containers whenever possible

- [ ] Follow industry best practies for C++ for any new code
    flag any existing code as a "nit" if not covered already by other review
    rules