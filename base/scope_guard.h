//
// Author: Andrei Alexandrescu - andrei@metalanguage.com
//
// The code below is based on the following article published in
// C/C++ Users Journal by Andrei Alexandrescu:
//
// http://www.cuj.com/documents/s=8000/cujcexp1812alexandr/alexandr.htm
//
// ScopeGuard is useful when you need to perform automatic cleanup of resources.
// This idiom is important when you want to assemble an operation out of several
// atomic operations, each of which could fail.
//
// Usage
// ------
// Scope guard for objects:
// void f(T& t) {
//   std::vector<T> v;
//   v.push_back(t);
//   ScopeGuard guard = MakeObjGuard(v, &std::vector<T>v::pop_back);
//   if (!Commit()) {
//     return;
//   }
//   guard.Dismiss();     // removes the t from the vector in the case Commit fails
// }
//
// Scope guard for functions:
// void open();
// void close(int i);
// void g(int i) {
//   open();
//   ScopeGuard guard = MakeGuard(close, 0);
//   if (!read()) {
//       return;
//   }
//   if (!write()) {
//       return;
//   }
// }
//
// Using the macros:
// void g(int i) {
//   open();
//   ON_SCOPE_EXIT(close, 0);
//   if (!read()) {
//       return;
//   }
//   if (!write()) {
//       return;
//   }
// }

// TODO(omaha): provide support to run with or without exceptions enabled.
// For now it assumes that the code is not throwing exceptions.
#ifndef SCOPEGUARD_H_
#define SCOPEGUARD_H_

namespace omaha {

template <class T>
class RefHolder
{
  T& ref_;
public:
  RefHolder(T& ref) : ref_(ref) {}
  operator T& () const
  {
    return ref_;
  }
private:
    // Disable assignment - not implemented
    RefHolder& operator=(const RefHolder&);
};

template <class T>
inline RefHolder<T> ByRef(T& t)
{
  return RefHolder<T>(t);
}

class ScopeGuardImplBase
{
  ScopeGuardImplBase& operator =(const ScopeGuardImplBase&);
protected:
  ~ScopeGuardImplBase()
  {
  }
  ScopeGuardImplBase(const ScopeGuardImplBase& other) throw()
    : dismissed_(other.dismissed_)
  {
    other.Dismiss();
  }
  template <typename J>
  static void SafeExecute(J& j) throw()
  {
    if (!j.dismissed_)
    {
      // TODO(omaha): assume this does not throw
      j.Execute();
    }
  }

  mutable bool dismissed_;
public:
  ScopeGuardImplBase() throw() : dismissed_(false)
  {
  }
  void Dismiss() const throw()
  {
    dismissed_ = true;
  }
};

typedef const ScopeGuardImplBase& ScopeGuard;

template <typename F>
class ScopeGuardImpl0 : public ScopeGuardImplBase
{
public:
  static ScopeGuardImpl0<F> MakeGuard(F fun)
  {
    return ScopeGuardImpl0<F>(fun);
  }
  ~ScopeGuardImpl0() throw()
  {
    SafeExecute(*this);
  }
  void Execute()
  {
    fun_();
  }
protected:
  ScopeGuardImpl0(F fun) : fun_(fun)
  {
  }
  F fun_;
};

template <typename F>
inline ScopeGuardImpl0<F> MakeGuard(F fun)
{
  return ScopeGuardImpl0<F>::MakeGuard(fun);
}

template <typename F, typename P1>
class ScopeGuardImpl1 : public ScopeGuardImplBase
{
public:
  static ScopeGuardImpl1<F, P1> MakeGuard(F fun, P1 p1)
  {
    return ScopeGuardImpl1<F, P1>(fun, p1);
  }
  ~ScopeGuardImpl1() throw()
  {
    SafeExecute(*this);
  }
  void Execute()
  {
    fun_(p1_);
  }
protected:
  ScopeGuardImpl1(F fun, P1 p1) : fun_(fun), p1_(p1)
  {
  }
  F fun_;
  const P1 p1_;
};

template <typename F, typename P1>
inline ScopeGuardImpl1<F, P1> MakeGuard(F fun, P1 p1)
{
  return ScopeGuardImpl1<F, P1>::MakeGuard(fun, p1);
}

template <typename F, typename P1, typename P2>
class ScopeGuardImpl2: public ScopeGuardImplBase
{
public:
  static ScopeGuardImpl2<F, P1, P2> MakeGuard(F fun, P1 p1, P2 p2)
  {
    return ScopeGuardImpl2<F, P1, P2>(fun, p1, p2);
  }
  ~ScopeGuardImpl2() throw()
  {
    SafeExecute(*this);
  }
  void Execute()
  {
    fun_(p1_, p2_);
  }
protected:
  ScopeGuardImpl2(F fun, P1 p1, P2 p2) : fun_(fun), p1_(p1), p2_(p2)
  {
  }
  F fun_;
  const P1 p1_;
  const P2 p2_;
};

template <typename F, typename P1, typename P2>
inline ScopeGuardImpl2<F, P1, P2> MakeGuard(F fun, P1 p1, P2 p2)
{
  return ScopeGuardImpl2<F, P1, P2>::MakeGuard(fun, p1, p2);
}

template <typename F, typename P1, typename P2, typename P3>
class ScopeGuardImpl3 : public ScopeGuardImplBase
{
public:
  static ScopeGuardImpl3<F, P1, P2, P3> MakeGuard(F fun, P1 p1, P2 p2, P3 p3)
  {
    return ScopeGuardImpl3<F, P1, P2, P3>(fun, p1, p2, p3);
  }
  ~ScopeGuardImpl3() throw()
  {
    SafeExecute(*this);
  }
  void Execute()
  {
    fun_(p1_, p2_, p3_);
  }
protected:
  ScopeGuardImpl3(F fun, P1 p1, P2 p2, P3 p3) : fun_(fun), p1_(p1), p2_(p2), p3_(p3)
  {
  }
  F fun_;
  const P1 p1_;
  const P2 p2_;
  const P3 p3_;
};

template <typename F, typename P1, typename P2, typename P3>
inline ScopeGuardImpl3<F, P1, P2, P3> MakeGuard(F fun, P1 p1, P2 p2, P3 p3)
{
  return ScopeGuardImpl3<F, P1, P2, P3>::MakeGuard(fun, p1, p2, p3);
}


template <class Obj, typename MemFun>
class ObjScopeGuardImpl0 : public ScopeGuardImplBase
{
public:
  static ObjScopeGuardImpl0<Obj, MemFun> MakeObjGuard(Obj& obj, MemFun memFun)
  {
    return ObjScopeGuardImpl0<Obj, MemFun>(obj, memFun);
  }
  ~ObjScopeGuardImpl0() throw()
  {
    SafeExecute(*this);
  }
  void Execute()
  {
    (obj_.*memFun_)();
  }
protected:
  ObjScopeGuardImpl0(Obj& obj, MemFun memFun)
    : obj_(obj), memFun_(memFun) {}
  Obj& obj_;
  MemFun memFun_;
};

template <class Obj, typename MemFun>
inline ObjScopeGuardImpl0<Obj, MemFun> MakeObjGuard(Obj& obj, MemFun memFun)
{
  return ObjScopeGuardImpl0<Obj, MemFun>::MakeObjGuard(obj, memFun);
}

template <class Obj, typename MemFun, typename P1>
class ObjScopeGuardImpl1 : public ScopeGuardImplBase
{
public:
  static ObjScopeGuardImpl1<Obj, MemFun, P1> MakeObjGuard(Obj& obj, MemFun memFun, P1 p1)
  {
    return ObjScopeGuardImpl1<Obj, MemFun, P1>(obj, memFun, p1);
  }
  ~ObjScopeGuardImpl1() throw()
  {
    SafeExecute(*this);
  }
  void Execute()
  {
    (obj_.*memFun_)(p1_);
  }
protected:
  ObjScopeGuardImpl1(Obj& obj, MemFun memFun, P1 p1)
    : obj_(obj), memFun_(memFun), p1_(p1) {}
  Obj& obj_;
  MemFun memFun_;
  const P1 p1_;
};

template <class Obj, typename MemFun, typename P1>
inline ObjScopeGuardImpl1<Obj, MemFun, P1> MakeObjGuard(Obj& obj, MemFun memFun, P1 p1)
{
  return ObjScopeGuardImpl1<Obj, MemFun, P1>::MakeObjGuard(obj, memFun, p1);
}

template <class Obj, typename MemFun, typename P1, typename P2>
class ObjScopeGuardImpl2 : public ScopeGuardImplBase
{
public:
  static ObjScopeGuardImpl2<Obj, MemFun, P1, P2> MakeObjGuard(Obj& obj, MemFun memFun, P1 p1, P2 p2)
  {
    return ObjScopeGuardImpl2<Obj, MemFun, P1, P2>(obj, memFun, p1, p2);
  }
  ~ObjScopeGuardImpl2() throw()
  {
    SafeExecute(*this);
  }
  void Execute()
  {
    (obj_.*memFun_)(p1_, p2_);
  }
protected:
  ObjScopeGuardImpl2(Obj& obj, MemFun memFun, P1 p1, P2 p2)
    : obj_(obj), memFun_(memFun), p1_(p1), p2_(p2) {}
  Obj& obj_;
  MemFun memFun_;
  const P1 p1_;
  const P2 p2_;
};

template <class Obj, typename MemFun, typename P1, typename P2>
inline ObjScopeGuardImpl2<Obj, MemFun, P1, P2> MakeObjGuard(Obj& obj, MemFun memFun, P1 p1, P2 p2)
{
  return ObjScopeGuardImpl2<Obj, MemFun, P1, P2>::MakeObjGuard(obj, memFun, p1, p2);
}

template <class Obj, typename MemFun, typename P1, typename P2, typename P3>
class ObjScopeGuardImpl3 : public ScopeGuardImplBase
{
public:
  static ObjScopeGuardImpl3<Obj, MemFun, P1, P2, P3> MakeObjGuard(Obj& obj, MemFun memFun, P1 p1, P2 p2, P3 p3)
  {
    return ObjScopeGuardImpl3<Obj, MemFun, P1, P2, P3>(obj, memFun, p1, p2, p3);
  }
  ~ObjScopeGuardImpl3() throw()
  {
    SafeExecute(*this);
  }
  void Execute()
  {
    (obj_.*memFun_)(p1_, p2_, p3_);
  }
protected:
  ObjScopeGuardImpl3(Obj& obj, MemFun memFun, P1 p1, P2 p2, P3 p3)
    : obj_(obj), memFun_(memFun), p1_(p1), p2_(p2), p3_(p3) {}
  Obj& obj_;
  MemFun memFun_;
  const P1 p1_;
  const P2 p2_;
  const P3 p3_;
};

template <class Obj, typename MemFun, typename P1, typename P2, typename P3>
inline ObjScopeGuardImpl3<Obj, MemFun, P1, P2, P3> MakeObjGuard(Obj& obj, MemFun memFun, P1 p1, P2 p2, P3 p3)
{
  return ObjScopeGuardImpl3<Obj, MemFun, P1, P2, P3>::MakeObjGuard(obj, memFun, p1, p2, p3);
}

#define CONCATENATE_DIRECT(s1, s2) s1##s2
#define CONCATENATE(s1, s2) CONCATENATE_DIRECT(s1, s2)
#define ANONYMOUS_VARIABLE(str) CONCATENATE(str, __LINE__)

#define ON_SCOPE_EXIT ScopeGuard ANONYMOUS_VARIABLE(scopeGuard) = MakeGuard
#define ON_SCOPE_EXIT_OBJ ScopeGuard ANONYMOUS_VARIABLE(scopeGuard) = MakeObjGuard

}  // namespace omaha

#endif //SCOPEGUARD_H_
