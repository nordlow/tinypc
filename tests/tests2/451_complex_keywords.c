#if defined test_complex
_Complex double x;
#elif defined test_imaginary
_Imaginary double y;
#elif defined test_cfloat
cfloat cf;
#elif defined test_cdouble
cdouble cd;
#elif defined test_creal
creal cr;
#elif defined test_ifloat
ifloat ifl;
#elif defined test_idouble
idouble idl;
#elif defined test_ireal
ireal irl;
#endif

int main() { return 0; }
