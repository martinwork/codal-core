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

/**
  * Base class for payload for ref-counted objects. Used by ManagedString and DeviceImage.
  * There is no constructor, as this struct is typically malloc()ed.
  */
#include "CodalConfig.h"
#include "CodalDevice.h"
#include "RefCounted.h"
#include "CodalDMesg.h"
#include "CodalFiber.h"

using namespace codal;

/**
  * Checks if the object resides in flash memory.
  *
  * @param t the object to check.
  *
  * @return true if the object resides in flash memory, false otherwise.
  */
static inline bool isReadOnlyInline(RefCounted *t, void *obj, const char *ref)
{
    uint32_t refCount = t->refCount;

    if (refCount == 0xffff)
        return true; // object in flash

    // Do some sanity checking while we're here
    if (refCount == 1)        // object should have been deleted
    {
        DMESG("\r\n%p,%d,%p::%s refCount == 1", t, (int) t->refCount, obj, ref);
        RefCounted_dump();
        DMESGF("************************************************************************************************");
        target_panic(DEVICE_HEAP_ERROR+4);
    }

    if ((refCount & 1) == 0)    // refCount doesn't look right
    {
        DMESG("\r\n%p,%d,%p::%s (refCount & 1) == 0", t, (int) t->refCount, obj, ref);
        RefCounted_dump();
        DMESGF("************************************************************************************************");
        target_panic(DEVICE_HEAP_ERROR+5);
    }

    RefCounted_op( t, obj, ref);

    // Not read only
    return false;
}

/**
  * Checks if the object resides in flash memory.
  *
  * @return true if the object resides in flash memory, false otherwise.
  */
bool RefCounted::isReadOnly()
{
    return isReadOnlyInline(this,NULL,"isRO");
}

/**
  * Increment reference count.
  */
void RefCounted::incr( void *obj, const char *ref)
{
    if (!isReadOnlyInline(this,obj,ref))
      __sync_fetch_and_add(&refCount, 2);
}

/**
  * Decrement reference count.
  */
void RefCounted::decr( void *obj, const char *ref)
{
    if ( !isReadOnlyInline(this,obj,ref))
    {
        if (__sync_fetch_and_add(&refCount, -2) == 3 ) {
            destroy(obj);
        }
    }
}
