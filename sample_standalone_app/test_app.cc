/**
 * Non-metric Space Library
 *
 * Authors: Bilegsaikhan Naidan (https://github.com/bileg), Leonid Boytsov (http://boytsov.info).
 * With contributions from Lawrence Cayton (http://lcayton.com/) and others.
 *
 * For the complete list of contributors and further details see:
 * https://github.com/searchivarius/NonMetricSpaceLib 
 * 
 * Copyright (c) 2014
 *
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 */
#include <iostream>
#include <cassert>
#include <cstdlib>
#include <sstream>

#include "data.h"

#include "custom_space.h"
#include "init.h"
#include "index.h"
#include "params.h"
#include "rangequery.h"
#include "knnquery.h"
#include "knnqueue.h"
#include "methodfactory.h"

#include "ztimer.h"

#define REP_QTY 1000

#ifdef _MSC_VER
#include <windows.h>
#endif

using namespace std;
using namespace similarity;

void Usage(const char *pProg, const char* pErr) {
  cerr << pErr << endl;
  cerr << "Usage: " << pProg << " <test file> " << endl;
  cerr << "Usage: " << pProg << " <test file> <max #of vectors> " << endl;
  cerr << "Usage: " << pProg << endl;
};

/*
 * Define an implementation of the distance function.
 */
struct DistL2 {
  /*
   * Important: the function is const and arguments are const as well!!!
   */
  float operator()(const float* x, const float* y, size_t qty) const {
    float res = 0;
    for (size_t i = 0; i < qty; ++i) res+=(x[i]-y[i])*(x[i]-y[i]);
    return sqrt(res);
  }
};

void printResults(KNNQuery<float>* qobj) {
  KNNQueue<float>* res = qobj->Result()->Clone();

  while (!res->Empty()) {
    cout << res->TopObject()->id() << " : " << res->TopDistance() << endl;
    res->Pop();
  }
}

void printResults(RangeQuery<float>* qobj) {
  const ObjectVector&    objs = *qobj->Result();
  const vector<float>&    dists = *qobj->ResultDists();

  for (size_t i = 0; i < objs.size(); ++i) {
    cout << objs[i]->id() << " : " << dists[i] << endl;
  }
}

template <class QueryType>
void doSearch(Index<float>* index, QueryType* qobj, int repQty) {
  WallClockTimer timer;

  /*
   * In this example we repeat the search many times,
   * but only because we need to measure the result
   * properly.
   */
  for (int i = 0; i < repQty; ++i) {
     index->Search(qobj);
	 if (i + 1 < repQty) qobj->Reset(); // This is needed b/c we reuse the same query many times
  }

  timer.split();

  cout << "Search " << qobj->Type() << " using index: " << index->ToString() 
	   << " repeated: " << repQty << " times " << endl;
  cout << "Avg time:  " << timer.elapsed()/1000.0/repQty << " ms" << endl;
  cout << "# of results: " << qobj->ResultSize() << endl;

  printResults(qobj);
}


int main(int argc, char* argv[]) {
  ObjectVector    dataSet; 
  const Object*   queryObj = NULL;

  // Create an instance of our custom space that uses L2-distance
  VectorSpaceGen<float, DistL2>   customSpace;

  const char* fileName = NULL;
  if (argc == 2 || argc == 3) {
    fileName = argv[1];
    // Here we will read data from a file
    int MaxNumObjects = 100; // read at most 100 objects, if 0 all objects are read
    if (argc == 3) MaxNumObjects = atol(argv[2]);
    customSpace.ReadDataset(dataSet,
                      NULL, // we don't need config here
                      fileName,
                      MaxNumObjects);
    if (dataSet.size() < 2) {
      cerr << "Too few data elements in " << fileName << endl; 
      return 1;
    }
  } else if (argc == 1) {
    // If the file is not specified, create the data set from a vector of vectors
    customSpace.CreateDataset(dataSet, rawData); 
  } else {
    Usage(argv[1], "Wrong # of arguments");
    return 1;
  } 

  assert(dataSet.size() > 1);

  queryObj = dataSet[0];
  dataSet.erase(dataSet.begin());

  cout << "Using the first object as the query vector" << endl;
  cout << "The number of remaining objects is: " << dataSet.size() << " "; 

  if (fileName == NULL) cout << " created from vector<vector<...>> "; 
  else cout << "read from file: " << fileName << endl;

  /* 
   * Clearing memory: we would rather use some smart pointer here.
   *                  can't use standard shared_ptr, b/c they have
   *                  performance issues.
   * see, e.g.: http://nerds-central.blogspot.com/2012/03/sharedptr-performance-issues-and.html 
   */

  cout << "We have the space and the query, let's create some search index." << endl;

  /* 
   * Init library, specify a log file
   * If the logfile is NULL,  we print to STDERR.
   */
  initLibrary("logfile.txt"); 

  Index<float>*   indexSmallWorld =  
                        MethodFactoryRegistry<float>::Instance().
                                CreateMethod(true /* print progress */,
                                        "small_world_rand",
                                        "custom", &customSpace,
                                        dataSet, 
                                        AnyParams(
                                                  {
                                                  "NN=11",
                                                  "initIndexAttempts=3",
                                                  "initSearchAttempts=3",
                                                  "indexThreadQty=4", /* 4 indexing threads */
                                                  }
                                                  )
                                        );

  cout << "Small-world index is created!" << endl;

  Index<float>*   indexVPTree = 
                        MethodFactoryRegistry<float>::Instance().
                                CreateMethod(false /* don't print progress */,
                                        "vptree",
                                        "custom", &customSpace,
                                        dataSet, 
                                        AnyParams(
                                                  {
                                                  "alphaLeft=1.0",
                                                  "alphaRight=1.0",
                                                  }
                                                  )
                                        );

  cout << "VP-tree index is created!" << endl;

  Index<float>*   indexPerm = 
                           MethodFactoryRegistry<float>::Instance().
                                CreateMethod(false /* don't print progress */,
                                        "perm_incsort",
                                        "custom", &customSpace,
                                        dataSet, 
                                        AnyParams(
                                                  {
                                                  "dbScanFrac=0.2", // A fraction of the data set to scan
                                                  "numPivot=16",   // Number of pivots (should be < the # of objects)
                                                  }
                                                  )
                                        );

  cout << "Permutation index is created!" << endl;

  /* Now let's try some searches */
  float radius = 0.12;
  RangeQuery<float>   rangeQ(&customSpace, queryObj, radius);

  //doSearch(indexSmallWorld, &rangeQ); not supported for small world method
  doSearch(indexVPTree, &rangeQ, REP_QTY);
  doSearch(indexPerm, &rangeQ, REP_QTY);

  unsigned K = 5; // 10-NN query
  KNNQuery<float>   knnQ(&customSpace, queryObj, K);

  doSearch(indexSmallWorld, &knnQ, REP_QTY);
  doSearch(indexVPTree, &knnQ, REP_QTY);

  cout << "Setting one value of a query-time param" << endl;
  indexPerm->SetQueryTimeParams(AnyParams( { "dbScanFrac=0.05" }));
  doSearch(indexPerm, &knnQ, REP_QTY);
  cout << "Setting another value of a query-time param" << endl;
  indexPerm->SetQueryTimeParams(AnyParams( { "dbScanFrac=0.5" }));
  doSearch(indexPerm, &knnQ, REP_QTY);

  cout << "Deleting objects..." << endl;

  delete indexSmallWorld;
  delete indexVPTree;
  delete indexPerm;

  delete queryObj;

  for (const Object* obj: dataSet) delete obj;

  return 0;
};

