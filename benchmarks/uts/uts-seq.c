/*
 *         ---- The Unbalanced Tree Search (UTS) Benchmark ----
 *  
 *  Copyright (c) 2010 See AUTHORS file for copyright holders
 *
 *  This file is part of the unbalanced tree search benchmark.  This
 *  project is licensed under the MIT Open Source license.  See the LICENSE
 *  file for copyright and licensing information.
 *
 *  UTS is a collaborative project between researchers at the University of
 *  Maryland, the University of North Carolina at Chapel Hill, and the Ohio
 *  State University.  See AUTHORS file for more information.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "uts.h"

#define GET_NUM_THREADS  1
#define GET_THREAD_NUM   0


/***********************************************************
 *  Global state                                           *
 ***********************************************************/
counter_t nNodes  = 0;
counter_t nLeaves = 0;
counter_t maxTreeDepth = 0;


/***********************************************************
 *  UTS Implementation Hooks                               *
 ***********************************************************/

// The name of this implementation
char * impl_getName() {
  return "Sequential Recursive Search";
}

int  impl_paramsToStr(char *strBuf, int ind) { 
  ind += sprintf(strBuf+ind, "Execution strategy:  %s\n", impl_getName());
  return ind;
}

// Not using UTS command line params, return non-success
int  impl_parseParam(char *param, char *value) { return 1; (void)param; (void)value; }

void impl_helpMessage() {
  printf("   none.\n");
}

void impl_abort(int err) {
  exit(err);
}


/***********************************************************
 * Recursive depth-first implementation                    *
 ***********************************************************/

typedef struct {
  counter_t maxdepth, size, leaves;
} Result;

Result parTreeSearch(int depth, Node *parent) {
  int numChildren, childType;
  counter_t parentHeight = parent->height;

  Result r = { depth, 1, 0 };

  numChildren = uts_numChildren(parent);
  childType   = uts_childType(parent);

  // record number of children in parent
  parent->numChildren = numChildren;
  
  // Recurse on the children
  if (numChildren > 0) {
    int i, j;
    for (i = 0; i < numChildren; i++) {
      Node child;
      child.type = childType;
      child.height = parentHeight + 1;
      child.numChildren = -1;    // not yet determined
      for (j = 0; j < computeGranularity; j++) {
        rng_spawn(parent->state.state, child.state.state, i);
      }
      Result c = parTreeSearch(depth+1, &child);
      if (c.maxdepth>r.maxdepth) r.maxdepth = c.maxdepth;
      r.size += c.size;
      r.leaves += c.leaves;
    }
  } else {
    r.leaves = 1;
  }

  return r;
}

int main(int argc, char *argv[]) {
  Node root;
  double t1, t2;

  uts_parseParams(argc, argv);

  uts_printParams();
  uts_initRoot(&root, type);

  t1 = uts_wctime();

  Result r = parTreeSearch(0, &root);

  t2 = uts_wctime();

  maxTreeDepth = r.maxdepth;
  nNodes  = r.size;
  nLeaves = r.leaves;

  uts_showStats(GET_NUM_THREADS, 0, t2-t1, nNodes, nLeaves, maxTreeDepth);

  printf("Time: %f\n", t2-t1);

  return 0;
}
