
======================
Thread Safety Analysis
======================

Introduction
============

Clang Thread Safety Analysis is a C++ language extension which warns about
potential race conditions in code.  The analysis is completely static (i.e.
compile-time); there is no run-time overhead.  The analysis is still
under active development, but it is mature enough to be deployed in an
industrial setting.  It being developed by Google, and is used extensively
on their internal code base.

Thread safety analysis works very much like a type system for multi-threaded
programs.  In addition to declaring the *type* of data (e.g. ``int``, ``float``,
etc.), the programmer can (optionally) declare how access to that data is
controlled in a multi-threaded environment.  For example, if ``foo`` is
*guarded by* the mutex ``mu``, then the analysis will issue a warning whenever
a piece of code reads or writes to ``foo`` without first locking ``mu``.
Similarly, if there are particular routines that should only be called by
the GUI thread, then the analysis will warn if other threads call those
routines. 

Getting Started
----------------

.. code-block:: c++

  #include "mutex.h"

  class BankAccount {
  private:
    Mutex mu;
    int   balance GUARDED_BY(mu);
  
    void depositImpl(int amount) {
      balance += amount;       // WARNING! Cannot write balance without locking mu.
    }
  
    void withdrawImpl(int amount) EXCLUSIVE_LOCKS_REQUIRED(mu) {
      balance -= amount;       // OK. Caller must have locked mu.
    }
  
  public:
    void withdraw(int amount) {
      mu.Lock();
      withdrawImpl(amount);    // OK.  We've locked mu.
    }                          // WARNING!  Failed to unlock mu.
  
    void transferFrom(BankAccount& b, int amount) {
      mu.Lock();
      b.withdrawImpl(amount);  // WARNING!  Calling withdrawImpl() requires locking b.mu.
      depositImpl(amount);     // OK.  depositImpl() has no requirements.
      mu.Unlock();
    }
  };

This example demonstrates the basic concepts behind the analysis.  The
``GUARDED_BY`` attribute declares that a thread must lock ``mu`` before it can
read or write to ``balance``, thus ensuring that the increment and decrement
operations are atomic.  Similarly, ``EXCLUSIVE_LOCKS_REQUIRED`` declares that
the calling thread must lock ``mu`` before calling ``withdrawImpl``.
Because the caller is assumed to have locked ``mu``, it is safe to modify
``balance`` within the body of the method.

The ``depositImpl()`` method does not have ``EXCLUSIVE_LOCKS_REQUIRED``, so the
analysis issues a warning.  Thread safety analysis is not inter-procedural, so
caller requirements must be explicitly declared.
There is also a warning in ``transferFrom()``, because although the method
locks ``this->mu``, it does not lock ``b.mu``.  The analysis understands
that these are two separate mutexes, in two different objects.  

Finally, there is a warning in the ``withdraw()`` method, because it fails to
unlock ``mu``.  Every lock must have a corresponding unlock, and the analysis
will detect both double locks, and double unlocks.  A function is allowed to
acquire a lock without releasing it, (or vice versa), but it must be annotated
as such (using ``LOCK``/``UNLOCK_FUNCTION``).


Running The Analysis
--------------------

To run the analysis, simply compile with the ``-Wthread-safety`` flag, e.g.

.. code-block:: bash

  clang -c -Wthread-safety example.cpp

Note that this example assumes the presence of a suitably annotated
:ref:`mutexheader` that declares which methods perform locking,
unlocking, and so on. 


Basic Concepts: Capabilities
============================

Thread safety analysis provides a way of protecting *resources* with
*capabilities*.  A resource is either a data member, or a function/method
that provides access to some underlying resource.  The analysis ensures that
the calling thread cannot access the *resource* (i.e. call the function, or
read/write the data) unless it has the *capability* to do so.

Capabilities are associated with named C++ objects which declare specific
methods to acquire and release the capability.  The name of the object serves
to identify the capability.  The most common example is a mutex.  For example,
if ``mu`` is a mutex, then calling ``mu.Lock()`` causes the calling thread
to acquire the capability to access data that is protected by ``mu``. Similarly, 
calling ``mu.Unlock()`` releases that capability.

A thread may hold a capability either *exclusively* or *shared*.  An exclusive
capability can be held by only one thread at a time, while a shared capability
can be held by many threads at the same time.  This mechanism enforces a
multiple-reader, single-writer pattern.  Write operations to protected data
require exclusive access, while read operations require only shared access.  

At any given moment during program execution, a thread holds a specific set of
capabilities (e.g. the set of mutexes that it has locked.)  These act like keys
or tokens that allow the thread to access a given resource.  Just like physical
security keys, a thread cannot make copy of a capability, nor can it destroy
one.  A thread can only release a capability to another thread, or acquire one
from another thread.  The annotations are deliberately agnostic about the
exact mechanism used to acquire and release capabilities; it assumes that the 
underlying implementation (e.g. the Mutex implementation) does the handoff in
an appropriate manner.

The set of capabilities that are actually held by a given thread at a given
point in program execution is a run-time concept.  The static analysis works
by calculating an approximation of that set, called the *capability
environment*.  The capability environment is calculated for every program point,
and describes the set of capabilities that are statically known to be held, or
not held, at that particular point.  This environment is a conservative
approximation of the full set of capabilities that will actually held by a
thread at run-time.


Reference Guide
===============

The thread safety analysis uses attributes to declare threading constraints.
Attributes must be attached to named declarations, such as classes, methods,
and data members. Users are *strongly advised* to define macros for the various
attributes; example definitions can be found in :ref:`mutexheader`, below.
The following documentation assumes the use of macros.


GUARDED_BY(c) and PT_GUARDED_BY(c)
----------------------------------

``GUARDED_BY`` is an attribute on data members, which declares that the data
member is protected by the given capability.  Read operations on the data
require shared access, while write operations require exclusive access.

``PT_GUARDED_BY`` is similar, but is intended for use on pointers and smart
pointers. There is no constraint on the data member itself, but the *data that
it points to* is protected by the given capability.  

.. code-block:: c++

  Mutex mu;
  int *p1            GUARDED_BY(mu);
  int *p2            PT_GUARDED_BY(mu);
  unique_ptr<int> p3 PT_GUARDED_BY(mu);
  
  void test() {
    p1 = 0;             // Warning!
  
    p2 = new int;       // OK.
    *p2 = 42;           // Warning!
  
    p3.reset(new int);  // OK.
    *p3 = 42;           // Warning!
  }


EXCLUSIVE_LOCKS_REQUIRED(...), SHARED_LOCKS_REQUIRED(...)
---------------------------------------------------------

``EXCLUSIVE_LOCKS_REQUIRED`` is an attribute on functions or methods, which
declares that the calling thread must have exclusive access to the given
capabilities.  More than one capability may be specified.  The capabilities
must be held on entry to the function, *and must still be held on exit*.  

``SHARED_LOCKS_REQUIRED`` is similar, but requires only shared access.

.. code-block:: c++

  Mutex mu1, mu2;
  int a GUARDED_BY(mu1);
  int b GUARDED_BY(mu2);
  
  void foo() EXCLUSIVE_LOCKS_REQUIRED(mu1, mu2) {
    a = 0;
    b = 0;
  }
  
  void test() {
    mu1.Lock();
    foo();         // Warning!  Requires mu2.
    mu1.Unlock();
  }


EXCLUSIVE_LOCK_FUNCTION(...), SHARED_LOCK_FUNCTION(...), UNLOCK_FUNCTION(...)
-----------------------------------------------------------------------------

``EXCLUSIVE_LOCK_FUNCTION`` is an attribute on functions or methods, which
declares that the function acquires a capability, but does not release it.  The
caller must not hold the given capability on entry, and it will hold the
capability on exit.  ``SHARED_LOCK_FUNCTION`` is similar. 

``UNLOCK_FUNCTION`` declares that the function releases the given capability.
The caller must hold the capability on entry, and will no longer hold it on
exit. It does not matter whether the given capability is shared or exclusive.

.. code-block:: c++

  Mutex mu;
  MyClass myObject GUARDED_BY(mu);
  
  void lockAndInit() EXCLUSIVE_LOCK_FUNCTION(mu) {
    mu.Lock();
    myObject.init();
  }
  
  void cleanupAndUnlock() UNLOCK_FUNCTION(mu) {
    myObject.cleanup();
  }  // Warning!  Need to unlock mu.
  
  void test() {
    lockAndInit();
    myObject.doSomething();
    cleanupAndUnlock();
    myObject.doSomething();  // Warning, mu is not locked.
  }

If no argument is passed to ``(UN)LOCK_FUNCTION``, then the argument is assumed
to be ``this``, and the analysis will not check the body of the function.  This
pattern is intended for use by classes which hide locking details behind an
abstract interface.  E.g.

.. code-block:: c++

  template <class T>
  class LOCKABLE Container {
  private:
    Mutex mu;
    T* data;
  
  public:
    // Hide mu from public interface.
    void Lock() EXCLUSIVE_LOCK_FUNCTION() { mu.Lock(); }
    void Unlock() UNLOCK_FUNCTION() { mu.Unlock(); }
  
    T& getElem(int i) { return data[i]; }
  };
  
  void test() {
    Container<int> c;
    c.Lock();
    int i = c.getElem(0);
    c.Unlock();
  }


LOCKS_EXCLUDED(...)
-------------------

``LOCKS_EXCLUDED`` is an attribute on functions or methods, which declares that
the caller must *not* hold the given capabilities.  This annotation is
used to prevent deadlock.  Many mutex implementations are not re-entrant, so
deadlock can occur if the function in question acquires the mutex a second time.

.. code-block:: c++

  Mutex mu;
  int a GUARDED_BY(mu);
  
  void clear() LOCKS_EXCLUDED(mu) {
    mu.Lock();
    a = 0;
    mu.Unlock();
  }
  
  void reset() {
    mu.Lock();
    clear();     // Warning!  Caller cannot hold 'mu'.
    mu.Unlock();
  }

Unlike ``LOCKS_REQUIRED``, ``LOCKS_EXCLUDED`` is optional.  The analysis will
not issue a warning if the attribute is missing.  See :ref:`limitations`.


NO_THREAD_SAFETY_ANALYSIS
-------------------------

``NO_THREAD_SAFETY_ANALYSIS`` is an attribute on functions or methods, which
turns off thread safety checking for that method.  It provides an escape hatch
for functions which are either (1) deliberately thread-unsafe, or (2) are
thread-safe, but too complicated for the analysis to understand.  Reasons for
(2) will be described in the :ref:`limitations`, below.

.. code-block:: c++

  class Counter {
    Mutex mu;
    int a GUARDED_BY(mu);
  
    void unsafeIncrement() NO_THREAD_SAFETY_ANALYSIS { a++; }
  };


LOCK_RETURNED(c)
----------------

``LOCK_RETURNED`` is an attribute on functions or methods, which declares that
the function returns a reference to the given capability.  It is used to
annotate getter methods that return mutexes.

.. code-block:: c++

  class MyClass {
  private:
    Mutex mu;
    int a GUARDED_BY(mu);
  
  public:
    Mutex* getMu() LOCK_RETURNED(mu) { return &mu; }
  
    // analysis knows that getMu() == mu
    void clear() EXCLUSIVE_LOCKS_REQUIRED(getMu()) { a = 0; }
  };


ACQUIRED_BEFORE(...), ACQUIRED_AFTER(...)
-----------------------------------------

``ACQUIRED_BEFORE`` and ``ACQUIRED_AFTER`` are attributes on member
declarations, specifically declarations of mutexes or other capabilities.
These declarations enforce a particular order in which the mutexes must be
acquired, in order to prevent deadlock.

.. code-block:: c++

  Mutex m1;
  Mutex m2 ACQUIRED_AFTER(m1);
  
  // Alternative declaration
  // Mutex m2;
  // Mutex m1 ACQUIRED_BEFORE(m2);
  
  void foo() {
    m2.Lock();
    m1.Lock();  // Warning!  m2 must be acquired after m1.
    m1.Unlock();
    m2.Unlock();
  }


LOCKABLE
--------

``LOCKABLE`` is an attribute on classes, which specifies that objects of the
class can be used as a capability.  See the ``Container`` example given above,
or the ``Mutex`` class in :ref:`mutexheader`.


SCOPED_LOCKABLE
---------------

``SCOPED_LOCKABLE`` is an attribute on classes that implement RAII-style
locking, in which a capability is acquired in the constructor, and released in
the destructor.  Such classes require special handling because the constructor
and destructor refer to the capability via different names; see the
``MutexLocker`` class in :ref:`mutexheader`, below.


EXCLUSIVE_TRYLOCK_FUNCTION(<bool>, ...), SHARED_TRYLOCK_FUNCTION(<bool>, ...)
-----------------------------------------------------------------------------

These are attributes on a function or method that tries to acquire the given
capability, and returns a boolean value indicating success or failure.
The first argument must be ``true`` or ``false``, to specify which return value
indicates success, and the remaining arguments are interpreted in the same way
as ``(UN)LOCK_FUNCTION``.  See :ref:`mutexheader`, below, for example uses.


ASSERT_EXCLUSIVE_LOCK(...) and ASSERT_SHARED_LOCK(...)
------------------------------------------------------

These are attributes on a function or method that does a run-time test to see
whether the calling thread holds the given capability.  The function is assumed
to fail (no return) if the capability is not held.  See :ref:`mutexheader`,
below, for example uses.


GUARDED_VAR and PT_GUARDED_VAR
------------------------------

Use of these attributes has been deprecated.


Warning flags
-------------

* ``-Wthread-safety``:  Umbrella flag which turns on the following three:

  + ``-Wthread-safety-attributes``: Sanity checks on attribute syntax.
  + ``-Wthread-safety-analysis``: The core analysis.
  + ``-Wthread-safety-precise``: Requires that mutex expressions match precisely.
    This warning can be disabled for code which has a lot of aliases.

When new features and checks are added to the analysis, they can often introduce
additional warnings.  Those warnings are initially released as *beta* warnings
for a period of time, after which they are migrated to the standard analysis.  

* ``-Wthread-safety-beta``:  New features.  Off by default. 


.. _faq:

Frequently Asked Questions
==========================

(Q) Should I put attributes in the header file, or in the .cc/.cpp/.cxx file?

(A) Attributes should always go in the header.


(Q) "*Mutex is not locked on every path through here?*"  What does that mean?

(A) See :ref:`conditional_locks`, below.


.. _limitations:

Known Limitations 
=================

Lexical scope
-------------

Thread safety attributes contain ordinary C++ expressions, and thus follow
ordinary C++ scoping rules.  In particular, this means that mutexes and other
capabilities must be declared before they can be used in an attribute.
Use-before-declaration is okay within a single class, because attributes are
parsed at the same time as method bodies. (C++ delays parsing of method bodies
until the end of the class.)  However, use-before-declaration is not allowed
between classes, as illustrated below.  

.. code-block:: c++

  class Foo;

  class Bar {
    void bar(Foo* f) EXCLUSIVE_LOCKS_REQUIRED(f->mu);  // Error: mu undeclared.
  };

  class Foo {
    Mutex mu;
  };


Private Mutexes
---------------

Good software engineering practice dictates that mutexes should be private
members, because the locking mechanism used by a thread-safe class is part of
its internal implementation.  However, private mutexes can sometimes leak into
the public interface of a class.
Thread safety attributes follow normal C++ access restrictions, so if ``mu``
is a private member of ``c``, then it is an error to write ``c.mu`` in an
attribute.

One workround is to (ab)use the ``LOCK_RETURNED`` attribute to provide a public
*name* for a private mutex, without actually exposing the underlying mutex.
For example:

.. code-block:: c++

  class MyClass {
  private:
    Mutex mu;

  public:
    // For thread safety analysis only.  Does not actually return mu.
    Mutex* getMu() LOCK_RETURNED(mu) { return 0; }

    void doSomething() EXCLUSIVE_LOCKS_REQUIRED(mu); 
  };

  void doSomethingTwice(MyClass& c) EXCLUSIVE_LOCKS_REQUIRED(c.getMu()) {
    // The analysis thinks that c.getMu() == c.mu
    c.doSomething();
    c.doSomething();
  }

In the above example, ``doSomethingTwice()`` is an external routine that
requires ``c.mu`` to be locked, which cannot be declared directly because ``mu``
is private.  This pattern is discouraged because it
violates encapsulation, but it is sometimes necessary, especially when adding
annotations to an existing code base.  The workaround is to define ``getMu()``
as a fake getter method, which is provided only for the benefit of thread
safety analysis.


False negatives on pass by reference.
-------------------------------------

The current version of the analysis only checks operations which refer to
guarded data members directly by name.  If the data members are accessed
indirectly, via a pointer or reference, then no warning is generated.  Thus,
no warnings will be generated for the following code:

.. code-block:: c++

  Mutex mu;
  int a GUARDED_BY(mu);

  void clear(int& ra) { ra = 0; }

  void test() {
    int *p = &a;
    *p = 0;       // No warning.  *p is an alias to a.  
       
    clear(a);     // No warning.  'a' is passed by reference.
  }

This issue is by far the biggest source of false negatives in the current
version of the analysis.  At a fundamental level, the
false negatives are caused by the fact that annotations are attached to data
members, rather than types.  The type of ``&a`` should really be
``int GUARDED_BY(mu)*``, rather than ``int*``, and the statement ``p = &a``
should thus generate a type error.  However, attaching attributes to types
would be an invasive change to the C++ type system, with potential
ramifications with respect to template instantation, function overloading,
and so on.  Thus, a complete solution to this issue is simply not feasible.

Future versions of the analysis will include better support for pointer
alias analysis, along with limited checking of guarded types, in order to
reduce the number of false negatives.


.. _conditional_locks:

No conditionally held locks.
----------------------------

The analysis must be able to determine whether a lock is held, or not held, at
every program point.  Thus, sections of code where a lock *might be held* will
generate spurious warnings (false positives).  For example:

.. code-block:: c++

  void foo() {
    bool b = needsToLock();
    if (b) mu.Lock();
    ...  // Warning!  Mutex 'mu' is not held on every path through here. 
    if (b) mu.Unlock();
  }


No checking inside constructors and destructors.
------------------------------------------------

The analysis currently does not do any checking inside constructors or
destructors.  In other words, every constructor and destructor is treated as
if it was annotated with ``NO_THREAD_SAFETY_ANALYSIS``.  
The reason for this is that during initialization, only one thread typically
has access to the object which is being initialized, and it is thus safe (and
common practice) to initialize guarded members without acquiring any locks.
The same is true of destructors.

Ideally, the analysis would allow initialization of guarded members inside the
object being initialized or destroyed, while still enforcing the usual access
restrictions on everything else.  However, this is difficult to enforce in
practice, because in complex pointer-based data structures, it is hard to
determine what data is "owned by" the enclosing object.

No inlining.
------------

Thread safety analysis is strictly intra-procedural, just like ordinary type
checking.  It relies only on the declared attributes of a function, and will
not attempt to "step inside", or inline any method calls.  As a result, code
such as the following will not work:

.. code-block:: c++

  template<class T>
  class AutoCleanup {
    T* object;
    void (T::*mp)();
    
  public:
    AutoCleanup(T* obj, void (T::*imp)()) : object(obj), mp(imp) { }
    ~AutoCleanup() { (object->*mp)(); }
  };

  Mutex mu;
  void foo() {
    mu.Lock();
    AutoCleanup<Mutex>(&mu, &Mutex::Unlock); 
    ...
  }  // Warning, mu is not unlocked.

In this case, the destructor of ``Autocleanup`` calls ``mu.Unlock()``, so
the warning is bogus.  However,
thread safety analysis cannot see the unlock, because it does not attempt to
inline the destructor.  Moreover, there is no way to annotate the destructor,
because the destructor is calling a function that is not statically known.
This pattern is simply not supported. 


LOCKS_EXCLUDED is not transitive.
---------------------------------

A function which calls a method marked with LOCKS_EXCLUDED is not required to
put LOCKS_EXCLUDED in its own interface.  LOCKS_EXCLUDED behaves differently
from LOCKS_REQUIRED in this respect, and it can result in false negatives:

.. code-block:: c++

  class Foo {
    Mutex mu;
    
    void foo() {
      mu.Lock();
      bar();                // No warning
      mu.Unlock();
    }
    
    void bar() { baz(); }   // No warning.  (Should have LOCKS_EXCLUDED(mu).)
    
    void baz() LOCKS_EXCLUDED(mu);
  };

The lack of transitivity is due to the fact that LOCKS_EXCLUDED can easily
break encapsulation; it would be a bad idea to require functions to list the
names private locks which happen to be acquired internally.  


No alias analysis.
------------------

The analysis currently does not track pointer aliases.  Thus, there can be
false positives if two pointers both point to the same mutex.  


.. code-block:: c++

  class MutexUnlocker {
    Mutex* mu;

  public:
    MutexUnlocker(Mutex* m) UNLOCK_FUNCTION(m) : mu(m)  { mu->Unlock(); }
    ~MutexUnlocker() EXCLUSIVE_LOCK_FUNCTION(mu) { mu->Lock(); }
  };

  Mutex mutex;
  void test() EXCLUSIVE_LOCKS_REQUIRED(mutex) {
    { 
      MutexUnlocker munl(&mutex);  // unlocks mutex
      doSomeIO();
    }                              // Warning: locks munl.mu
  }

The MutexUnlocker class is intended to be the dual of the MutexLocker class,
defined in :ref:`mutexheader`.  However, it doesn't work because the analysis
doesn't know that munl.mu == mutex.  The SCOPED_LOCKABLE attribute handles
aliasing 


ACQUIRED_BEFORE(...) and ACQUIRED_AFTER(...) are currently unimplemented.
-------------------------------------------------------------------------

To be fixed in a future update. 


.. _mutexheader:

mutex.h
=======

Thread safety analysis can be used with any threading library, but it does
require that the threading API be wrapped in classes and methods which have the
appropriate annotations.  The following code provides ``mutex.h`` as an example;
these methods should be filled in to call the appropriate underlying
implementation. 


.. code-block:: c++

  #ifndef THREAD_SAFETY_ANALYSIS_MUTEX_H
  #define THREAD_SAFETY_ANALYSIS_MUTEX_H
  
  // Enable thread safety attributes only with clang.
  // The attributes can be safely erased when compiling with other compilers.
  #if defined(__clang__) && (!defined(SWIG))
  #define THREAD_ANNOTATION_ATTRIBUTE__(x)   __attribute__((x))
  #else
  #define THREAD_ANNOTATION_ATTRIBUTE__(x)   // no-op
  #endif
  
  #define THREAD_ANNOTATION_ATTRIBUTE__(x)   __attribute__((x))
  
  #define GUARDED_BY(x) \
    THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))
  
  #define GUARDED_VAR \
    THREAD_ANNOTATION_ATTRIBUTE__(guarded)
  
  #define PT_GUARDED_BY(x) \
    THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded_by(x))
  
  #define PT_GUARDED_VAR \
    THREAD_ANNOTATION_ATTRIBUTE__(pt_guarded)
  
  #define ACQUIRED_AFTER(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))
  
  #define ACQUIRED_BEFORE(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))
  
  #define EXCLUSIVE_LOCKS_REQUIRED(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(exclusive_locks_required(__VA_ARGS__))
  
  #define SHARED_LOCKS_REQUIRED(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(shared_locks_required(__VA_ARGS__))
  
  #define LOCKS_EXCLUDED(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(locks_excluded(__VA_ARGS__))
  
  #define LOCK_RETURNED(x) \
    THREAD_ANNOTATION_ATTRIBUTE__(lock_returned(x))
  
  #define LOCKABLE \
    THREAD_ANNOTATION_ATTRIBUTE__(lockable)
  
  #define SCOPED_LOCKABLE \
    THREAD_ANNOTATION_ATTRIBUTE__(scoped_lockable)
  
  #define EXCLUSIVE_LOCK_FUNCTION(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(exclusive_lock_function(__VA_ARGS__))
  
  #define SHARED_LOCK_FUNCTION(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(shared_lock_function(__VA_ARGS__))
  
  #define ASSERT_EXCLUSIVE_LOCK(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(assert_exclusive_lock(__VA_ARGS__))
  
  #define ASSERT_SHARED_LOCK(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(assert_shared_lock(__VA_ARGS__))
  
  #define EXCLUSIVE_TRYLOCK_FUNCTION(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(exclusive_trylock_function(__VA_ARGS__))
  
  #define SHARED_TRYLOCK_FUNCTION(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(shared_trylock_function(__VA_ARGS__))
  
  #define UNLOCK_FUNCTION(...) \
    THREAD_ANNOTATION_ATTRIBUTE__(unlock_function(__VA_ARGS__))
  
  #define NO_THREAD_SAFETY_ANALYSIS \
    THREAD_ANNOTATION_ATTRIBUTE__(no_thread_safety_analysis)
  
  
  // Defines an annotated interface for mutexes.
  // These methods can be implemented to use any internal mutex implementation.
  class LOCKABLE Mutex {
  public:
    // Acquire/lock this mutex exclusively.  Only one thread can have exclusive
    // access at any one time.  Write operations to guarded data require an
    // exclusive lock.
    void Lock() EXCLUSIVE_LOCK_FUNCTION();
  
    // Acquire/lock this mutex for read operations, which require only a shared
    // lock.  This assumes a multiple-reader, single writer semantics.  Multiple
    // threads may acquire the mutex simultaneously as readers, but a writer must
    // wait for all of them to release the mutex before it can acquire it
    // exclusively.  
    void ReaderLock() SHARED_LOCK_FUNCTION();
  
    // Release/unlock the mutex, regardless of whether it is exclusive or shared.
    void Unlock() UNLOCK_FUNCTION();
  
    // Try to acquire the mutex.  Returns true on success, and false on failure.
    bool TryLock() EXCLUSIVE_TRYLOCK_FUNCTION(true);
  
    // Try to acquire the mutex for read operations.
    bool ReaderTryLock() SHARED_TRYLOCK_FUNCTION(true);
  
    // Assert that this mutex is currently held by the calling thread.
    void AssertHeld() ASSERT_EXCLUSIVE_LOCK();
  
    // Assert that is mutex is currently held for read operations. 
    void AssertReaderHeld() ASSERT_SHARED_LOCK();
  };
  
  
  // MutexLocker is an RAII class that acquires a mutex in its constructor, and
  // releases it in its destructor.  
  class SCOPED_LOCKABLE MutexLocker {
  private:
    Mutex* mut;
  
  public:
    MutexLocker(Mutex *mu) EXCLUSIVE_LOCK_FUNCTION(mu) : mut(mu) {
      mu->Lock();
    }  
    ~MutexLocker() UNLOCK_FUNCTION() {
      mut->Unlock();
    }
  };
  
  #endif  // THREAD_SAFETY_ANALYSIS_MUTEX_H
