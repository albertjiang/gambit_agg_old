//#
//# FILE: glpsolv3.cc -- Instantiation of common LP solvers
//#
//# @(#)glpsolv3.cc	1.4 5/3/95
//#

#include "glpsolv3.imp"
#include "rational.h"
#include "glpsolve.imp"

#ifdef __GNUG__
#define TEMPLATE template
#elif defined __BORLANDC__
#define TEMPLATE
#pragma option -Jgd
#endif   // __GNUG__, __BORLANDC__

TEMPLATE class gLPTableau3<double>;
TEMPLATE class gLPTableau3<gDouble>;
TEMPLATE class gLPTableau3<gRational>;

