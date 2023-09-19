/*
The MIT License (MIT)

Copyright (c) 2017 Lancaster University.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

#include "CodalConfig.h"
#include "CodalDevice.h"
#include "RefCounted.h"
#include "CodalDMesg.h"
#include <list>

namespace codal
{
  typedef std::list<RefCounted *> RefCounted_list;
  RefCounted_list theList;
  //RefCounted_list theAllTimeList;

#define opsSIZE 60

  unsigned int opsUnq = 0;
  unsigned int opsSeq[ opsSIZE];

  RefCounted *opsPtr[ opsSIZE];
  int         opsCnt[ opsSIZE];
  void       *opsObj[ opsSIZE];
  char        opsRef[ opsSIZE][80];
  int         opsIdx = 0;

  void RefCounted_dump(void)
  {
    DMESG("\r\nRefCounted_dump");
    DMESG("ops: ptr, count, object, fn");
    int idx = opsIdx;
    for ( int op = 0; op < opsSIZE; op++)
    {
      DMESG("%u:%p,%d,%p::%s", opsSeq[idx], opsPtr[idx], (int) opsCnt[idx], opsObj[idx], opsRef[idx]);
      idx++;
      if (idx >= opsSIZE)
        idx = 0;
    }
    DMESG("current");
    for ( RefCounted_list::iterator it = theList.begin(); it != theList.end(); it++)
    {
      RefCounted *t = (RefCounted *) *it;
      DMESG("%p,%d", t, (int) t->refCount);
    }
    //DMESG("all time");
    //for ( RefCounted_list::iterator it = theAllTimeList.begin(); it != theAllTimeList.end(); it++)
    //{
    //  RefCounted *t = (RefCounted *) *it;
    //  DMESG("%p", t);
    //}
  }

  void RefCounted_op( RefCounted *t, void *obj, const char *ref)
  {
    target_disable_irq();
    opsSeq[opsIdx] = opsUnq;
    opsUnq++;
    opsPtr[opsIdx] = t;
    opsCnt[opsIdx] = t ? t->refCount : 0;
    opsObj[opsIdx] = obj;
    strcpy( opsRef[opsIdx], ref);
    opsIdx++;
    if (opsIdx >= opsSIZE)
      opsIdx = 0;
    target_enable_irq();
  }

  void RefCounted_add( RefCounted *t, void *obj)
  {
    RefCounted_op( t, obj, "init");
    theList.push_back(t);
    //theAllTimeList.push_back(t);
  }

  void RefCounted_remove( RefCounted *t, void *obj)
  {
    RefCounted_op( t, obj, "destroy");
    theList.remove(t);
  }

  //void RefCounted_alltimeunique()
  //{
  //  RefCounted_op( 0, "sort");
  //  theAllTimeList.sort();
  //  RefCounted_op( 0, "unique");
  //  theAllTimeList.unique();
  //}
}

using namespace codal;
// These two are placed in a separate file, so that they can be overriden by user code.

/**
  * Releases the current instance.
  */
void RefCounted::destroy( void *obj)
{
    RefCounted_remove( this, obj);
    free(this);
}

/**
  * Initializes for one outstanding reference.
  */
void RefCounted::init( void *obj)
{
    // Initialize to one reference (lowest bit set to 1)
    refCount = 3;
    RefCounted_add( this, obj);
}
