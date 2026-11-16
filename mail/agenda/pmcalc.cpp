// PMCALC.CPP - the Agenda half's plain-C rules (core/mail/engine/cal:
// calmerge.c, calrpt.c) compiled as C++ with the program's own flags:
// makmake builds .c files for interworking, with BX, which the netBook's
// StrongARM lacks (core/tools/v4bx.py)
extern "C" {
#include "calmerge.c"
#include "calrpt.c"
}
