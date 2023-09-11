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
  RefCounted_list theAllTimeList;

#define opsSIZE 20

  RefCounted *opsPtr[ opsSIZE];
  int         opsRef[ opsSIZE];
  int         opsIdx = 0;

  void RefCounted_dump(void)
  {
    DMESG("\r\nRefCounted_dump");
    DMESG("ops");
    int idx = opsIdx;
    for ( int op = 0; op < opsSIZE; op++)
    {
      DMESG("%p,%d", opsPtr[idx], opsRef[idx]);
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
    DMESG("all time");
    for ( RefCounted_list::iterator it = theAllTimeList.begin(); it != theAllTimeList.end(); it++)
    {
      RefCounted *t = (RefCounted *) *it;
      DMESG("%p", t);
    }
  }

  void RefCounted_op( RefCounted *t, int ref)
  {
    opsPtr[opsIdx] = t;
    opsRef[opsIdx] = ref;
    opsIdx++;
    if (opsIdx >= opsSIZE)
      opsIdx = 0;
  }

  void RefCounted_add( RefCounted *t)
  {
    RefCounted_op( t, 30);
    theList.push_back(t);
    theAllTimeList.push_back(t);
  }

  void RefCounted_remove( RefCounted *t)
  {
    RefCounted_op( t, 31);
    theList.remove(t);
  }

  void RefCounted_alltimeunique()
  {
    RefCounted_op( 0, 32);
    theAllTimeList.sort();
    RefCounted_op( 0, 33);
    theAllTimeList.unique();
  }
}

using namespace codal;
// These two are placed in a separate file, so that they can be overriden by user code.

/**
  * Releases the current instance.
  */
void RefCounted::destroy()
{
    RefCounted_remove( this);
    free(this);
}

/**
  * Initializes for one outstanding reference.
  */
void RefCounted::init()
{
    // Initialize to one reference (lowest bit set to 1)
    refCount = 3;
    RefCounted_add( this);
}
