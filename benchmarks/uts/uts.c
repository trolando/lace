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
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#include <common.h>

#include "uts.h"

/***********************************************************
 *  tree generation and search parameters                  *
 *                                                         *
 *  Tree generation strategy is controlled via various     *
 *  parameters set from the command line.  The parameters  *
 *  and their default values are given below.              *
 ***********************************************************/

char * uts_trees_str[]     = { "Binomial", "Geometric", "Hybrid", "Balanced", "Budget" };
char * uts_geoshapes_str[] = { "Linear decrease", "Exponential decrease", "Cyclic", 
                              "Fixed branching factor" };

/* Tree type
 *   Trees are generated using a Galton-Watson process, in 
 *   which the branching factor of each node is a random 
 *   variable.
 *   
 *   The random variable can follow a binomial distribution
 *   or a geometric distribution.  Hybrid tree are
 *   generated with geometric distributions near the
 *   root and binomial distributions towards the leaves.
 *   Budget trees (utsb) instead split a fixed number of
 *   nodes among the children of each node.
 */
tree_t type  = UTS_GEO; // Default tree type
double b_0   = 4.0; // default branching factor at the root
int   rootId = 0;   // default seed for RNG state at root

/*  Tree type BIN (BINOMIAL)
 *  The branching factor at the root is specified by b_0.
 *  The branching factor below the root follows an 
 *     identical binomial distribution at all nodes.
 *  A node has m children with prob q, or no children with 
 *     prob (1-q).  The expected branching factor is q * m.
 *  In utsb, a maximum depth bin_mx can be given (-d): nodes
 *     at that depth have no children, and with q * m > 1 the
 *     tree grows wide rather than deep.
 *
 *  Default parameter values 
 */
int    nonLeafBF   = 4;            // m
double nonLeafProb = 15.0 / 64.0;  // q
#ifdef UTSB
int    bin_mx      = 0;            // maximum depth, 0: unbounded
#endif

/*  Tree type GEO (GEOMETRIC)
 *  The branching factor follows a geometric distribution with
 *     expected value b.
 *  The probability that a node has 0 <= n children is p(1-p)^n for
 *     0 < p <= 1. The distribution is truncated at MAXNUMCHILDREN.
 *  The expected number of children b = (1-p)/p.  Given b (the
 *     target branching factor) we can solve for p.
 *
 *  A shape function computes a target branching factor b_i
 *     for nodes at depth i as a function of the root branching
 *     factor b_0 and a maximum depth gen_mx.
 *   
 *  Default parameter values
 */
int        gen_mx   = 6;      // default depth of tree
geoshape_t shape_fn = UTS_LINEAR; // default shape function (b_i decr linearly)

/*  In type HYBRID trees, each node is either type BIN or type
 *  GEO, with the generation strategy changing from GEO to BIN 
 *  at a fixed depth, expressed as a fraction of gen_mx
 */
double shiftDepth = 0.5;         

/*  Tree type BUDGET (utsb only)
 *  Each node has a budget: the number of nodes in its subtree,
 *     itself included.  The budget of the root is the tree size n.
 *  A node with budget b has m children, which share the other
 *     b-1-m nodes in proportion to weights 2^U with U uniform on
 *     0..s, or b-1 children (leaves) if b-1 < m.  The tree has
 *     exactly n nodes, the expected size of a subtree falls by a
 *     factor m per level, and s sets how unevenly a node splits
 *     its work (s = 0: equal shares).  m is nonLeafBF, as for BIN.
 *
 *  Default parameter values
 */
#ifdef UTSB
counter_t treeSize    = 100000000; // n
int       splitSpread = 8;         // s
#endif

/* compute granularity - number of rng evaluations per tree node */
int computeGranularity = 1;

/* display parameters */
int debug    = 0;
int verbose  = 1;


/***********************************************************
 *                                                         *
 *  FUNCTIONS                                              *
 *                                                         *
 ***********************************************************/

/* fatal error */
void uts_error(char *str) {
  printf("*** Error: %s\n", str);
  impl_abort(1);
}


// Interpret 32 bit positive integer as value on [0,1)
double rng_toProb(int n) {
  if (n < 0) {
    printf("*** toProb: rand n = %d out of range\n",n);
  }
  return ((n<0)? 0.0 : ((double) n)/2147483648.0);
}


void uts_initRoot(Node * root, int type) {
  root->type = type;
  root->height = 0;
  root->numChildren = -1;      // means not yet determined
#ifdef UTSB
  root->budget = (type == UTS_BUDGET) ? treeSize : 0;
#endif
  rng_init(root->state.state, rootId);

  if (debug & 1)
    printf("root node of type %d at %p\n",type, (void*)root);
}


int uts_numChildren_bin(Node * parent) {
  // distribution is identical everywhere below root
  int    v = rng_rand(parent->state.state);	
  double d = rng_toProb(v);

  return (d < nonLeafProb) ? nonLeafBF : 0;
}


int uts_numChildren_geo(Node * parent) {
  double b_i = b_0;
  int depth = parent->height;
  int numChildren, h;
  double p, u;
  
  // use shape function to compute target b_i
  if (depth > 0){
    switch (shape_fn) {
      
      // expected size polynomial in depth
    case UTS_EXPDEC:
      b_i = b_0 * pow((double) depth, -log(b_0)/log((double) gen_mx));
      break;
      
      // cyclic tree size
    case UTS_CYCLIC:
      if (depth > 5 * gen_mx){
        b_i = 0.0;
        break;
      } 
      b_i = pow(b_0, 
                sin(2.0*3.141592653589793*(double) depth / (double) gen_mx));
      break;

      // identical distribution at all nodes up to max depth
    case UTS_FIXED:
      b_i = (depth < gen_mx)? b_0 : 0;
      break;
      
      // linear decrease in b_i
    case UTS_LINEAR:
    default:
      b_i =  b_0 * (1.0 - (double)depth / (double) gen_mx);
      break;
    }
  }

  // given target b_i, find prob p so expected value of 
  // geometric distribution is b_i.
  p = 1.0 / (1.0 + b_i);

  // get uniform random number on [0,1)
  h = rng_rand(parent->state.state);
  u = rng_toProb(h);

  // max number of children at this cumulative probability
  // (from inverse geometric cumulative density function)
  numChildren = (int) floor(log(1 - u) / log(1 - p)); 

  return numChildren;
}


#ifdef UTSB
// weight of child i of a BUDGET node: 2^U with U uniform on
// 0..splitSpread, from an integer hash (splitmix64) of the
// node state and i: the same tree on every platform
static counter_t uts_childWeight(Node *parent, int i) {
  uint64_t x = 0;
  int j;

  for (j = 0; j < 8; j++)
    x = (x << 8) | parent->state.state[j];
  x += (uint64_t) (i + 1) * 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  x ^= x >> 31;

  return (counter_t) 1 << (x % (uint64_t) (splitSpread + 1));
}


int uts_numChildren_budget(Node * parent) {
  counter_t b = parent->budget;
  int i;

  // too few nodes left: they are all leaves
  if (b - 1 < (counter_t) nonLeafBF)
    return (int) (b - 1);

  parent->wsum = 0;
  for (i = 0; i < nonLeafBF; i++)
    parent->wsum += uts_childWeight(parent, i);

  return nonLeafBF;
}


// budget of child i of a BUDGET node, once the driver has recorded
// its number of children: 1 plus its share of the other nodes,
// rounded down; the last child also gets what the rounding leaves
counter_t uts_childBudget(Node *parent, int i) {
  int n = parent->numChildren;
  counter_t rest, given = 0;
  int j;

  if (n < nonLeafBF)
    return 1;

  rest = parent->budget - 1 - (counter_t) n;
  if (i < n - 1)
    return 1 + rest * uts_childWeight(parent, i) / parent->wsum;

  for (j = 0; j < n - 1; j++)
    given += 1 + rest * uts_childWeight(parent, j) / parent->wsum;
  return parent->budget - 1 - given;
}
#endif


int uts_numChildren(Node *parent) {
  int numChildren = 0;

  /* Determine the number of children */
  switch (type) {
    case UTS_BIN:
      if (parent->height == 0)
        numChildren = (int) floor(b_0);
#ifdef UTSB
      else if (bin_mx > 0 && parent->height >= bin_mx)
        numChildren = 0;
#endif
      else 
        numChildren = uts_numChildren_bin(parent);
      break;
  
    case UTS_GEO:
      numChildren = uts_numChildren_geo(parent);
      break;
    
    case UTS_HYBRID:
      if (parent->height < shiftDepth * gen_mx)
        numChildren = uts_numChildren_geo(parent);
      else
        numChildren = uts_numChildren_bin(parent);
      break;
    case UTS_BALANCED:
      if (parent->height < gen_mx)
        numChildren = (int) b_0;
      break;
#ifdef UTSB
    case UTS_BUDGET:
      numChildren = uts_numChildren_budget(parent);
      break;
#endif
    default:
      uts_error("parTreeSearch(): Unknown tree type");
  }
  
  // limit number of children
  // only a BIN root can have more than MAXNUMCHILDREN
  if (parent->height == 0 && parent->type == UTS_BIN) {
    int rootBF = (int) ceil(b_0);
    if (numChildren > rootBF) {
      printf("*** Number of children of root truncated from %d to %d\n",
             numChildren, rootBF);
      numChildren = rootBF;
    }
  }
  else if (type != UTS_BALANCED) {
    if (numChildren > MAXNUMCHILDREN) {
      printf("*** Number of children truncated from %d to %d\n", 
             numChildren, MAXNUMCHILDREN);
      numChildren = MAXNUMCHILDREN;
    }
  }

  return numChildren;
}


int uts_childType(Node *parent) {
  switch (type) {
    case UTS_BIN:
      return UTS_BIN;
    case UTS_GEO:
      return UTS_GEO;
    case UTS_HYBRID:
      if (parent->height < shiftDepth * gen_mx)
        return UTS_GEO;
      else 
        return UTS_BIN;
    case UTS_BALANCED:
      return UTS_BALANCED;
#ifdef UTSB
    case UTS_BUDGET:
      return UTS_BUDGET;
#endif
    default:
      uts_error("uts_get_childtype(): Unknown tree type");
      return -1;
  }
}


// construct string with all parameter settings 
int uts_paramsToStr(char *strBuf, int ind) {
  // version + execution model
  ind += sprintf(strBuf+ind, "UTS - Unbalanced Tree Search %s (%s)\n", UTS_VERSION, impl_getName());

  // tree type
  ind += sprintf(strBuf+ind, "Tree type:  %d (%s)\n", type, uts_trees_str[type]);
	
  // tree shape parameters
  ind += sprintf(strBuf+ind, "Tree shape parameters:\n");
  ind += sprintf(strBuf+ind, "  root branching factor b_0 = %.1f, root seed = %d\n", b_0, rootId);
	
  if (type == UTS_GEO || type == UTS_HYBRID) {
    ind += sprintf(strBuf+ind, "  GEO parameters: gen_mx = %d, shape function = %d (%s)\n", 
            gen_mx, shape_fn, uts_geoshapes_str[shape_fn]);
  }

  if (type == UTS_BIN || type == UTS_HYBRID) {
    double q = nonLeafProb;
    int    m = nonLeafBF;
    double es  = (1.0 / (1.0 - q * m));
#ifdef UTSB
    if (type == UTS_BIN && bin_mx > 0)  // geometric series to depth bin_mx
      es = (q * m == 1.0) ? bin_mx : (pow(q * m, bin_mx) - 1.0) / (q * m - 1.0);
#endif
    ind += sprintf(strBuf+ind, "  BIN parameters:  q = %f, m = %d, E(n) = %f, E(s) = %.2f\n",
            q, m, q * m, es);
#ifdef UTSB
    if (type == UTS_BIN && bin_mx > 0)
      ind += sprintf(strBuf+ind, "  BIN maximum depth = %d\n", bin_mx);
#endif
  }

#ifdef UTSB
  if (type == UTS_BUDGET) {
    ind += sprintf(strBuf+ind, "  BUDGET parameters: n = %llu, m = %d, s = %d\n",
            treeSize, nonLeafBF, splitSpread);
  }
#endif

  if (type == UTS_HYBRID) {
    ind += sprintf(strBuf+ind, "  HYBRID:  GEO from root to depth %d, then BIN\n", 
            (int) ceil(shiftDepth * gen_mx));
  }
	
  if (type == UTS_BALANCED) {
    ind += sprintf(strBuf+ind, "  BALANCED parameters: gen_mx = %d\n", gen_mx);
    ind += sprintf(strBuf+ind, "        Expected size: %llu nodes, %llu leaves\n",
        (counter_t) ((pow(b_0, gen_mx+1) - 1.0)/(b_0 - 1.0)) /* geometric series */,
        (counter_t) pow(b_0, gen_mx));
  }
	
  // random number generator
  ind += sprintf(strBuf+ind, "Random number generator: ");
  ind  = rng_showtype(strBuf, ind);
  ind += sprintf(strBuf+ind, "\nCompute granularity: %d\n", computeGranularity);

  return ind;
}

// show parameter settings
void uts_printParams(void) {
  char strBuf[5000] = "";
  int  ind = 0;

  if (verbose > 0) {
    ind = uts_paramsToStr(strBuf, ind);
    ind = impl_paramsToStr(strBuf, ind);
    printf("%s\n",strBuf);
  }
}

void uts_parseParams(int argc, char *argv[]){
  int i = 1; 
  int err = -1;
  while (i < argc && err == -1) {
    if (argv[i][0] == '-' && argv[i][1] == 'h') {
        uts_helpMessage();
        impl_abort(0);

    } else if (argv[i][0] != '-' || strlen(argv[i]) != 2 || argc <= i+1) {
      err = i; break;
    }

    // Matched by implementation -- return 0 on success
    // This is fragile, don't override parameters in impl_parseParam()!
    if (!impl_parseParam(argv[i], argv[i+1])) {
      i += 2;
      continue;
    }

    switch (argv[i][1]) {
      case 'q':
        nonLeafProb = atof(argv[i+1]); break;
      case 'm':
        nonLeafBF = atoi(argv[i+1]); break;
      case 'r':
        rootId = atoi(argv[i+1]); break;
      case 'x':
        debug = atoi(argv[i+1]); break;
      case 'v':
        verbose = atoi(argv[i+1]); break;
      case 't':
        type = (tree_t) atoi(argv[i+1]); 
        if (type != UTS_BIN && type != UTS_GEO && type!= UTS_HYBRID && type != UTS_BALANCED
#ifdef UTSB
            && type != UTS_BUDGET
#endif
            )
	  err = i;
        break;
      case 'a':
        shape_fn = (geoshape_t) atoi(argv[i+1]);
        if (shape_fn > UTS_FIXED) err = i;
        break;
      case 'b':
        b_0 = atof(argv[i+1]); break;
      case 'd':
#ifdef UTSB
        gen_mx = bin_mx = atoi(argv[i+1]); break;
#else
        gen_mx = atoi(argv[i+1]); break;
#endif
      case 'f':
        shiftDepth = atof(argv[i+1]); break;
      case 'g':
        computeGranularity = max(1,atoi(argv[i+1])); break;
#ifdef UTSB
      case 'n':
        treeSize = strtoull(argv[i+1], NULL, 10); break;
      case 's':
        splitSpread = atoi(argv[i+1]); break;
#endif
      default:
        err = i;
    }

    if (err != -1) break;

    i += 2;
  }

  if (err != -1) {
    printf("Unrecognized parameter or incorrect/missing value: '%s %s'\n", argv[i], (i+1 < argc) ? argv[i+1] : "[none]");
    printf("Try -h for help.\n");
    impl_abort(4);
  }

#ifdef UTSB
  // the shares (rest * 2^s at most) must fit in 64 bits
  if (type == UTS_BUDGET && (nonLeafBF < 2 || nonLeafBF > MAXNUMCHILDREN ||
        splitSpread < 0 || splitSpread > 30 || treeSize < 1 || treeSize >= (1ULL << 34))) {
    printf("BUDGET trees need 2 <= m <= %d, 0 <= s <= 30 and 1 <= n < 2^34\n", MAXNUMCHILDREN);
    impl_abort(4);
  }
#endif
}

void uts_helpMessage(void) {
  printf("  UTS - Unbalanced Tree Search %s (%s)\n\n", UTS_VERSION, impl_getName());
  printf("    usage:  uts-bin [parameter value] ...\n\n");
  printf("  parm type  description\n");    
  printf("  ==== ====  =========================================\n");
  printf("\n  Benchmark Parameters:\n");
  printf("   -t  int   tree type (0: BIN, 1: GEO, 2: HYBRID, 3: BALANCED)\n");
  printf("   -b  dble  root branching factor\n");
  printf("   -r  int   root seed 0 <= r < 2^31 \n");
  printf("   -a  int   GEO: tree shape function \n");
  printf("   -d  int   GEO, BALANCED: tree depth\n");
  printf("   -q  dble  BIN: probability of non-leaf node\n");
  printf("   -m  int   BIN: number of children for non-leaf node\n");
  printf("   -f  dble  HYBRID: fraction of depth for GEO -> BIN transition\n");
#ifdef UTSB
  printf("   -t  4     BUDGET tree\n");
  printf("   -d  int   BIN: maximum depth\n");
  printf("   -m  int   BUDGET: number of children per node\n");
  printf("   -n  int   BUDGET: number of nodes in the tree\n");
  printf("   -s  int   BUDGET: child weights 2^U, U uniform on 0..s\n");
#endif
  printf("   -g  int   compute granularity: number of rng_spawns per node\n");
  printf("   -v  int   nonzero to set verbose output\n");
  printf("   -x  int   debug level\n");

  // Get help message from the implementation
  printf("\n  Additional Implementation Parameters:\n");
  impl_helpMessage();
  printf("\n");
}


void uts_showStats(int nPes, int chunkSize, double walltime, counter_t nNodes, counter_t nLeaves, counter_t maxDepth) {
  // summarize execution info for machine consumption
  if (verbose == 0) {
    printf("%4d %7.3f %9llu %7.0llu %7.0llu %d %d %.2f %d %d %1d %f %3d\n",
        nPes, walltime, nNodes, (long long)(nNodes/walltime), (long long)((nNodes/walltime)/nPes), chunkSize, 
        type, b_0, rootId, gen_mx, shape_fn, nonLeafProb, nonLeafBF);
  }

  // summarize execution info for human consumption
  else {
    printf("Tree size = %llu, tree depth = %llu, num leaves = %llu (%.2f%%)\n", nNodes, maxDepth, nLeaves, nLeaves/(float)nNodes*100.0); 
    printf("Wallclock time = %.3f sec, performance = %.0f nodes/sec (%.0f nodes/sec per PE)\n\n",
        walltime, (nNodes / walltime), (nNodes / walltime / nPes));
  }
}
