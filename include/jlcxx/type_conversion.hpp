﻿#ifndef JLCXX_TYPE_CONVERSION_HPP
#define JLCXX_TYPE_CONVERSION_HPP

#include <julia.h>
#if JULIA_VERSION_MAJOR == 0 && JULIA_VERSION_MINOR > 4 || JULIA_VERSION_MAJOR > 0
#include <julia_threads.h>
#endif

#include <complex>
#include <map>
#include <memory>
#include <stack>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <type_traits>
#include <iostream>

#include "jlcxx_config.hpp"

namespace jlcxx
{

namespace detail
{
  template<bool, typename T1, typename T2>
  struct StaticIf;

  // non-bits
  template<typename T1, typename T2>
  struct StaticIf<false, T1, T2>
  {
    typedef T2 type;
  };

  // bits type
  template<typename T1, typename T2>
  struct StaticIf<true, T1, T2>
  {
    typedef T1 type;
  };
}

JLCXX_API void protect_from_gc(jl_value_t* v);
JLCXX_API void unprotect_from_gc(jl_value_t* v);

template<typename T>
inline void protect_from_gc(T* x)
{
  protect_from_gc((jl_value_t*)x);
}

template<typename T>
inline void unprotect_from_gc(T* x)
{
  unprotect_from_gc((jl_value_t*)x);
}

/// Get the symbol name correctly depending on Julia version
inline std::string symbol_name(jl_sym_t* symbol)
{
#if JULIA_VERSION_MAJOR == 0 && JULIA_VERSION_MINOR < 5
  return std::string(symbol->name);
#else
  return std::string(jl_symbol_name(symbol));
#endif
}

inline std::string module_name(jl_module_t* mod)
{
  return symbol_name(mod->name);
}

/// Backwards-compatible apply_type
JLCXX_API jl_value_t* apply_type(jl_value_t* tc, jl_svec_t* params);

/// Get the type from a global symbol
JLCXX_API jl_value_t* julia_type(const std::string& name, const std::string& module_name = "");
JLCXX_API jl_value_t* julia_type(const std::string& name, jl_module_t* mod);

/// Backwards-compatible apply_array_type
template<typename T>
inline jl_value_t* apply_array_type(T* type, std::size_t dim)
{
#if JULIA_VERSION_MAJOR == 0 && JULIA_VERSION_MINOR < 6
  return jl_apply_array_type((jl_datatype_t*)type, dim);
#else
  return jl_apply_array_type((jl_value_t*)type, dim);
#endif
}

/// Check if we have a string
inline bool is_julia_string(jl_value_t* v)
{
#if JULIA_VERSION_MAJOR == 0 && JULIA_VERSION_MINOR < 5
  return jl_is_byte_string(v);
#else
  return jl_is_string(v);
#endif
}

inline const char* julia_string(jl_value_t* v)
{
  #if JULIA_VERSION_MAJOR == 0 && JULIA_VERSION_MINOR < 5
    return jl_bytestring_ptr(v);
  #else
    return jl_string_ptr(v);
  #endif
}

inline std::string julia_type_name(jl_datatype_t* dt)
{
  return jl_typename_str((jl_value_t*)dt);
}
inline std::string julia_type_name(jl_value_t* dt)
{
  return jl_typename_str(dt);
}

// Specialize to indicate direct Julia supertype in a smart-pointer compatible way i.e. using this to define supertypes
// will make conversion to a smart pointer of the base type work like in C++
template<typename T>
struct SuperType
{
  typedef T type;
};

template<typename T> using supertype = typename SuperType<T>::type;

/// Trait to determine if a type is to be treated as a Julia immutable type that has isbits == true
template<typename T> struct IsImmutable : std::false_type {};

/// Trait to determine if the given type is to be treated as a bits type
template<typename T> struct IsBits : std::false_type {};

/// Remove reference and const from a type
template<typename T> using remove_const_ref = typename std::remove_const<typename std::remove_reference<T>::type>::type;

template<typename T>
struct IsFundamental
{
  static constexpr bool value = std::is_fundamental<remove_const_ref<T>>::value;
};

template<typename T>
struct IsFundamental<T*>
{
  static constexpr bool value = IsFundamental<T>::value;
};

template<typename T>
struct IsFundamental<T**>
{
  static constexpr bool value = true;
};

template<>
struct IsFundamental<const char*>
{
  static constexpr bool value = false;
};

/// Indicate if a type is a smart pointer
template<typename T> struct IsSmartPointerType
{
  static constexpr bool value = false;
};

/// Trait to determine if the given type is to be treated as a value type, i.e. if the reference should be stripped when passed as argument
template<typename T> struct IsValueType
{
  static constexpr bool value = IsImmutable<T>::value || IsBits<T>::value || IsFundamental<T>::value || IsSmartPointerType<T>::value;
};

// Unbox boxed type
template<typename CppT>
inline CppT unbox(jl_value_t* v)
{
  return *reinterpret_cast<CppT*>(jl_data_ptr(v));
}

/// Equivalent of the basic C++ type layout in Julia
struct WrappedCppPtr {
  void* voidptr;
};

template<typename CppT>
inline CppT* extract_pointer(const WrappedCppPtr& p)
{
  return reinterpret_cast<CppT*>(p.voidptr);
}

template<typename T>
T* unbox_wrapped_ptr(jl_value_t* v)
{
  return reinterpret_cast<T*>(unbox<WrappedCppPtr>(v).voidptr);
}

namespace detail
{
  /// Finalizer function for type T
  template<typename T>
  void finalizer(jl_value_t* to_delete)
  {
    T* stored_obj = unbox_wrapped_ptr<T>(to_delete);
    if(stored_obj != nullptr)
    {
      delete stored_obj;
    }

    reinterpret_cast<WrappedCppPtr*>(to_delete)->voidptr = nullptr;
  }

  template<typename T>
  struct unused_type
  {
  };

  template<typename T1, typename T2>
  struct DefineIfDifferent
  {
    typedef T1 type;
  };

  template<typename T>
  struct DefineIfDifferent<T,T>
  {
    typedef unused_type<T> type;
  };

  template<typename T1, typename T2> using define_if_different = typename DefineIfDifferent<T1,T2>::type;
}

// By default, fundamental and "POD" types are mapped directly
template<typename T>
struct IsMirroredType : std::bool_constant<!std::is_class<T>::value || (std::is_standard_layout<T>::value && std::is_trivial<T>::value)>
{
};

struct NoMappingTrait {}; // no mapping, C++ type = Julia type by default
struct CxxWrappedTrait {}; // types added using add_type
struct WrappedPtrTrait {}; // By default pointers are wrapped
struct DirectPtrTrait {}; // Some pointers are returned directly, e.g. jl_value_t*

template<typename T, typename Enable=void>
struct MappingTrait
{
  using type = NoMappingTrait;
};

template<typename T>
struct MappingTrait<T&>
{
  using type = WrappedPtrTrait;
};

template<typename T>
struct MappingTrait<T*>
{
  using type = WrappedPtrTrait;
};

template<>
struct MappingTrait<jl_value_t*>
{
  using type = DirectPtrTrait;
};

template<typename T>
struct MappingTrait<T, typename std::enable_if<!IsMirroredType<T>::value>::type>
{
  using type = CxxWrappedTrait;
};

template<typename T> using mapping_trait = typename MappingTrait<T>::type;

/// Static mapping base template, just passing through
template<typename SourceT, typename TraitT=mapping_trait<SourceT>>
struct static_type_mapping
{
  using type = SourceT;
};

template<typename SourceT>
struct static_type_mapping<SourceT, CxxWrappedTrait>
{
  using type = WrappedCppPtr;
};

template<typename SourceT>
struct static_type_mapping<SourceT*, DirectPtrTrait>
{
  using type = SourceT*;
};

/// Pointers are a pointer to the equivalent C type
template<typename SourceT>
struct static_type_mapping<SourceT*, WrappedPtrTrait>
{
  using type = WrappedCppPtr;
};

/// References are pointers
template<typename SourceT>
struct static_type_mapping<SourceT&>
{
  using type = WrappedCppPtr;
};

template<typename T> using static_julia_type = typename static_type_mapping<T>::type;

// Store a data type pointer, ensuring GC safety
struct CachedDatatype
{
  explicit CachedDatatype() : m_dt(nullptr) {}
  explicit CachedDatatype(jl_datatype_t* dt)
  {
    set_dt(dt);
  }

  void set_dt(jl_datatype_t* dt)
  {
    m_dt = dt;
    if(m_dt != nullptr)
    {
      protect_from_gc(m_dt);
    }
  }

  jl_datatype_t* get_dt()
  {
    return m_dt;
  }
  
private:
  jl_datatype_t* m_dt = nullptr;
};

/// Store the Julia datatype linked to SourceT
template<typename SourceT>
class dynamic_type_mapping
{
public:

  static constexpr bool storing_dt = true;

  static inline jl_datatype_t* julia_type()
  {
    if(m_dt.get_dt() == nullptr)
    {
      throw std::runtime_error("Type " + std::string(typeid(SourceT).name()) + " has no Julia wrapper");
    }
    return m_dt.get_dt();
  }

  static inline void set_julia_type(jl_datatype_t* dt)
  {
    if(m_dt.get_dt() != nullptr)
    {
      throw std::runtime_error("Type " + std::string(typeid(SourceT).name()) + " already had a mapped type set");
    }
    m_dt.set_dt(dt);
  }

private:
  static inline CachedDatatype m_dt;
};

namespace detail
{
  // Gets the dynamic type to put inside a pointer, which is the normal dynamic type for normal types, or the base type for wrapped types
  template<typename T, typename TraitT=mapping_trait<T>>
  struct GetDynamicPtrT
  {
    static inline jl_datatype_t* type()
    {
      return dynamic_type_mapping<T>::julia_type();
    }
  };

  template<typename T>
  struct GetDynamicPtrT<T,CxxWrappedTrait>
  {
    static inline jl_datatype_t* type()
    {
      return dynamic_type_mapping<T>::julia_type()->super;
    }
  };
}

// Mapping for const references
template<typename SourceT>
struct dynamic_type_mapping<const SourceT&>
{
  static inline jl_datatype_t* julia_type()
  {
    return (jl_datatype_t*)apply_type((jl_value_t*)jlcxx::julia_type("ConstCxxRef"), jl_svec1(detail::GetDynamicPtrT<SourceT>::type()));
  }
};

// Mapping for mutable references
template<typename SourceT>
struct dynamic_type_mapping<SourceT&>
{
  static inline jl_datatype_t* julia_type()
  {
    return (jl_datatype_t*)apply_type((jl_value_t*)jlcxx::julia_type("CxxRef"), jl_svec1(detail::GetDynamicPtrT<SourceT>::type()));
  }
};

// Mapping for const pointers
template<typename SourceT>
struct dynamic_type_mapping<const SourceT*>
{
  static inline jl_datatype_t* julia_type()
  {
    return (jl_datatype_t*)apply_type((jl_value_t*)jlcxx::julia_type("ConstCxxPtr"), jl_svec1(detail::GetDynamicPtrT<SourceT>::type()));
  }
};

// Mapping for mutable pointers
template<typename SourceT>
struct dynamic_type_mapping<SourceT*>
{
  static inline jl_datatype_t* julia_type()
  {
    return (jl_datatype_t*)apply_type((jl_value_t*)jlcxx::julia_type("CxxPtr"), jl_svec1(detail::GetDynamicPtrT<SourceT>::type()));
  }
};

template<>
struct dynamic_type_mapping<void*>
{
  static inline jl_datatype_t* julia_type()
  {
    return jl_voidpointer_type;
  }
};

template<>
struct dynamic_type_mapping<jl_datatype_t*>
{
  static inline jl_datatype_t* julia_type()
  {
    return jl_any_type;
  }
};

template<>
struct dynamic_type_mapping<jl_value_t*>
{
  static inline jl_datatype_t* julia_type()
  {
    return jl_any_type;
  }
};

template<typename T, typename Enable = void>
struct NeedsStorage
{
  static constexpr bool value = true;
};

template<typename T>
struct NeedsStorage<T, typename std::enable_if<dynamic_type_mapping<T>::storing_dt>::type>
{
  static constexpr bool value = !dynamic_type_mapping<T>::storing_dt;
};

/// Automatically cache the Julia pointer, if needed
template<typename T, bool needs_storage = NeedsStorage<T>::value>
struct CachedTypeMapping
{
  static jl_datatype_t* julia_type()
  {
    static CachedDatatype dt(dynamic_type_mapping<T>::julia_type());
    std::cout << "used a cached map for " << julia_type_name(dt.get_dt()) << std::endl;
    return dt.get_dt();
  }
};

// Case of the default dynamic_type_mapping, which already stores the datatype
template<typename T>
struct CachedTypeMapping<T, false>
{
  static jl_datatype_t* julia_type()
  {
    return dynamic_type_mapping<T>::julia_type();
  }
};

/// Convenience function to get the julia data type associated with T
template<typename T>
inline jl_datatype_t* julia_type()
{
  return CachedTypeMapping<T>::julia_type();
}

/// Base class to specialize for conversion to C++
template<typename CppT, typename TraitT=mapping_trait<CppT>>
struct ConvertToCpp
{
  template<typename JuliaT>
  CppT* operator()(JuliaT&&) const
  {
    static_assert(sizeof(CppT)==0, "No appropriate specialization for ConvertToCpp");
    return nullptr; // not reached
  }
};

/// Conversion to C++
template<typename CppT, typename JuliaT>
inline CppT convert_to_cpp(JuliaT julia_val)
{
  return ConvertToCpp<CppT>()(julia_val);
}

/// Automatically register pointer types
template<typename T>
void set_julia_type(jl_datatype_t* dt)
{
  dynamic_type_mapping<T>::set_julia_type(dt);
}

/// Helper for Singleton types (Type{T} in Julia)
template<typename T>
struct SingletonType
{
};

template<typename T> struct IsValueType<SingletonType<T>> : std::true_type {};

// template<typename T>
// struct static_type_mapping<SingletonType<T>>
// {
//   typedef jl_datatype_t* type;
//   static jl_datatype_t* julia_type() { return (jl_datatype_t*)apply_type((jl_value_t*)jl_type_type, jl_svec1(static_type_mapping<T>::julia_type())); }
// };

template<typename SourceT> using mapped_julia_type = typename static_type_mapping<SourceT>::type;

template<typename T, typename TraitT=mapping_trait<T>>
struct JuliaReturnType
{
  inline static jl_datatype_t* value()
  {
    return julia_type<T>();
  }
};

template<typename T>
struct JuliaReturnType<T, CxxWrappedTrait>
{
  inline static jl_datatype_t* value()
  {
    return jl_any_type;
  }
};

template<typename T>
inline jl_datatype_t* julia_return_type()
{
  return JuliaReturnType<T>::value();
}

/// Specializations

// Needed for Visual C++, static members are different in each DLL
extern "C" JLCXX_API jl_module_t* get_cxxwrap_module();
/*
template<>
struct static_type_mapping<void>
{
  typedef void type;
  static jl_datatype_t* julia_type() { return jl_void_type; }
};

template<>
struct static_type_mapping<bool>
{
  typedef bool type;
  static jl_datatype_t* julia_type() { return jl_bool_type; }
};

template<>
struct static_type_mapping<double>
{
  typedef double type;
  static jl_datatype_t* julia_type() { return jl_float64_type; }
};

template<>
struct static_type_mapping<float>
{
  typedef float type;
  static jl_datatype_t* julia_type() { return jl_float32_type; }
};

template<>
struct static_type_mapping<short>
{
  static_assert(sizeof(short) == 2, "short is expected to be 16 bits");
  typedef short type;
  static jl_datatype_t* julia_type() { return jl_int16_type; }
};

template<>
struct static_type_mapping<int>
{
  static_assert(sizeof(int) == 4, "int is expected to be 32 bits");
  typedef int type;
  static jl_datatype_t* julia_type() { return jl_int32_type; }
};

template<>
struct static_type_mapping<unsigned int>
{
  static_assert(sizeof(unsigned int) == 4, "unsigned int is expected to be 32 bits");
  typedef unsigned int type;
  static jl_datatype_t* julia_type() { return jl_uint32_type; }
};

template<>
struct static_type_mapping<unsigned char>
{
  typedef unsigned char type;
  static jl_datatype_t* julia_type() { return jl_uint8_type; }
};

template<>
struct static_type_mapping<int64_t>
{
  typedef int64_t type;
  static jl_datatype_t* julia_type() { return jl_int64_type; }
};

template<>
struct static_type_mapping<uint64_t>
{
  typedef uint64_t type;
  static jl_datatype_t* julia_type() { return jl_uint64_type; }
};

template<>
struct static_type_mapping<detail::define_if_different<long, int64_t>>
{
  static_assert(sizeof(long) == 8 || sizeof(long) == 4, "long is expected to be 64 bits or 32 bits");
  typedef long type;
  static jl_datatype_t* julia_type() { return sizeof(long) == 8 ? jl_int64_type : jl_int32_type; }
};

template<>
struct static_type_mapping<detail::define_if_different<long long, int64_t>>
{
  static_assert(sizeof(long long) == 8, " long long is expected to be 64 bits or 32 bits");
  typedef long long type;
  static jl_datatype_t* julia_type() { return jl_int64_type; }
};

template<>
struct static_type_mapping<detail::define_if_different<unsigned long, uint64_t>>
{
  static_assert(sizeof(unsigned long) == 8 || sizeof(unsigned long) == 4, "unsigned long is expected to be 64 bits or 32 bits");
  typedef unsigned long type;
  static jl_datatype_t* julia_type() { return sizeof(unsigned long) == 8 ? jl_uint64_type : jl_uint32_type; }
};

template<>
struct static_type_mapping<wchar_t>
{
  typedef wchar_t type;
  static jl_datatype_t* julia_type() { return (jl_datatype_t*)jl_get_global(jl_base_module, jl_symbol("Cwchar_t")); }
};

template<>
struct static_type_mapping<const wchar_t> : static_type_mapping<wchar_t>
{
};

template<>
struct IsValueType<std::string> : std::true_type
{
};

template<>
struct static_type_mapping<std::string>
{
  typedef jl_value_t* type;
  static jl_datatype_t* julia_type() { return (jl_datatype_t*)jl_get_global(jl_base_module, jl_symbol("AbstractString")); }
};

template<>
struct IsValueType<std::wstring> : std::true_type
{
};

template<>
struct static_type_mapping<std::wstring>
{
  typedef jl_value_t* type;
  static jl_datatype_t* julia_type() { return (jl_datatype_t*)jl_get_global(jl_base_module, jl_symbol("AbstractString")); }
};

template<typename T>
struct static_type_mapping<T*, typename std::enable_if<IsFundamental<T>::value && !std::is_const<T>::value>::type>
{
  typedef T* type;
  static jl_datatype_t* julia_type() { return (jl_datatype_t*)apply_type((jl_value_t*)jl_pointer_type, jl_svec1(static_type_mapping<T>::julia_type())); }
};

template<typename T>
struct static_type_mapping<T**, typename std::enable_if<!IsFundamental<T>::value>::type>
{
  typedef T** type;
  static jl_datatype_t* julia_type() { return (jl_datatype_t*)apply_type((jl_value_t*)jl_pointer_type, jl_svec1(static_type_mapping<T>::julia_allocated_type())); }
};

template<typename T>
struct static_type_mapping<const T*, typename std::enable_if<IsFundamental<T>::value>::type>
{
  typedef T* type;
  static jl_datatype_t* julia_type() { return (jl_datatype_t*)apply_type((jl_value_t*)jlcxx::julia_type("ConstCxxPtr"), jl_svec1(static_type_mapping<T>::julia_type())); }
};

template<>
struct static_type_mapping<const char*>
{
  typedef jl_value_t* type;
  static jl_datatype_t* julia_type() { return (jl_datatype_t*)jl_get_global(jl_base_module, jl_symbol("AbstractString")); }
};

template<> struct static_type_mapping<void*>
{
  typedef void* type;
  static jl_datatype_t* julia_type() { return jl_voidpointer_type; }
};

template<> struct static_type_mapping<jl_datatype_t*>
{
  typedef jl_datatype_t* type; // Debatable if this should be jl_value_t*
  static jl_datatype_t* julia_type() { return jl_any_type; }
};

template<> struct static_type_mapping<jl_value_t*>
{
  typedef jl_value_t* type;
  static jl_datatype_t* julia_type() { return jl_any_type; }
};

#if JULIA_VERSION_MAJOR == 0 && JULIA_VERSION_MINOR < 5
template<> struct static_type_mapping<jl_function_t*>
{
  typedef jl_function_t* type;
  static jl_datatype_t* julia_type() { return jl_any_type; }
};
#endif
*/
// Helper for ObjectIdDict
struct ObjectIdDict {};

template<> struct static_type_mapping<ObjectIdDict>
{
  typedef jl_value_t* type;
};

/// Wrap a C++ pointer in a Julia type that contains a single void pointer field, returning the result as an any
template<typename T>
jl_value_t* boxed_cpp_pointer(T* cpp_ptr, jl_datatype_t* dt, bool add_finalizer)
{
  assert(jl_datatype_nfields(dt) == 1);
  assert(jl_field_type(dt,0) == (jl_value_t*)jl_voidpointer_type);
  jl_value_t* void_ptr = nullptr;
  jl_value_t* result = nullptr;
  jl_value_t* finalizer = nullptr;
  JL_GC_PUSH3(&void_ptr, &result, &finalizer);
  void_ptr = jl_box_voidpointer((void*)cpp_ptr);
  result = jl_new_struct(dt, void_ptr);
  if(add_finalizer)
  {
    finalizer = jl_box_voidpointer((void*)detail::finalizer<T>);
#if JULIA_VERSION_MAJOR == 0 && JULIA_VERSION_MINOR < 5
    jl_gc_add_finalizer(result, (jl_function_t*)finalizer);
#else
    jl_gc_add_finalizer(result, finalizer);
#endif

  }
  JL_GC_POP();
  return result;
};

/// Transfer ownership of a regular pointer to Julia
template<typename T>
jl_value_t* julia_owned(T* cpp_ptr)
{
  static_assert(!std::is_fundamental<T>::value, "Ownership can't be transferred for fundamental types");
  const bool finalize = true;
  return boxed_cpp_pointer(cpp_ptr, julia_type<T>(), finalize);
}

/// Base class to specialize for conversion to Julia
// C++ wrapped types are in fact always returned as a pointer wrapped in a struct, so to avoid memory management issues with the wrapper itself
// we always return the wrapping struct by value
//template<typename T, typename TraitT=mapping_trait<typename std::remove_pointer<typename std::remove_reference<T>::type>::type>>
template<typename T, typename TraitT=mapping_trait<T>>
struct ConvertToJulia
{
  template<typename CppT>
  jl_value_t* operator()(CppT&& cpp_val) const
  {
    static_assert(std::is_same<mapped_julia_type<T>, WrappedCppPtr>::value, "No appropriate specialization for ConvertToJulia");
    static_assert(std::is_class<T>::value, "Need class type for conversion");
    T* cpp_obj = new T(cpp_val);
    return julia_owned(cpp_obj);
  }
};

template<typename T>
struct ConvertToJulia<T&, WrappedPtrTrait>
{
  WrappedCppPtr operator()(T& cpp_val) const
  {
    return {reinterpret_cast<void*>(const_cast<typename std::remove_const<T>::type*>(&cpp_val))};
  }
};

template<typename T>
struct ConvertToJulia<T*, WrappedPtrTrait>
{
  WrappedCppPtr operator()(T* cpp_val) const
  {
    return {reinterpret_cast<void*>(const_cast<typename std::remove_const<T>::type*>(cpp_val))};
  }
};

template<typename T>
struct ConvertToJulia<T*, DirectPtrTrait>
{
  T* operator()(T* cpp_val) const
  {
    return cpp_val;
  }
};

// Fundamental type
template<typename T>
struct ConvertToJulia<T, NoMappingTrait>
{
  T operator()(const T& cpp_val) const
  {
    return cpp_val;
  }
};

/// Conversion to the statically mapped target type.
template<typename T>
inline auto convert_to_julia(T&& cpp_val) -> decltype(ConvertToJulia<T>()(std::forward<T>(cpp_val)))
{
  return ConvertToJulia<T>()(std::forward<T>(cpp_val));
}

template<typename CppT>
inline typename std::enable_if<!std::is_same<jl_value_t*, mapped_julia_type<CppT>>::value && !std::is_same<WrappedCppPtr, mapped_julia_type<CppT>>::value, jl_value_t*>::type box(const CppT&)
{
  static_assert(sizeof(CppT*) == 0, "Unimplemented box in jlcxx");
  return nullptr;
}

// Box an automatically converted value
template<typename CppT>
inline typename std::enable_if<std::is_same<WrappedCppPtr, mapped_julia_type<CppT>>::value && !std::is_pointer<CppT>::value, jl_value_t*>::type box(const CppT& cpp_val)
{
  return boxed_cpp_pointer(&cpp_val, julia_type<CppT>(), false);
}

template<typename CppT>
inline typename std::enable_if<std::is_same<WrappedCppPtr, mapped_julia_type<CppT>>::value && std::is_pointer<CppT>::value, jl_value_t*>::type box(const CppT& cpp_val)
{
  return boxed_cpp_pointer(cpp_val, julia_type<CppT>(), false);
}

// Pass-through for already boxed types
template<typename CppT>
inline typename std::enable_if<std::is_same<jl_value_t*, mapped_julia_type<CppT>>::value, jl_value_t*>::type box(CppT&& cpp_val)
{
  return (jl_value_t*)convert_to_julia(std::forward<CppT>(cpp_val));
}

// Generic bits type conversion
template<typename CppT>
inline typename std::enable_if<IsBits<remove_const_ref<CppT>>::value, jl_value_t*>::type box(CppT&& cpp_val)
{
  return jl_new_bits((jl_value_t*)static_type_mapping<remove_const_ref<CppT>>::julia_type(), &cpp_val);
}

template<>
inline jl_value_t* box(const bool& b)
{
  return jl_box_bool(b);
}

template<>
inline jl_value_t* box(const int32_t& i)
{
  return jl_box_int32(i);
}

template<>
inline jl_value_t* box(const int64_t& i)
{
  return jl_box_int64(i);
}

template<>
inline jl_value_t* box(const uint32_t& i)
{
  return jl_box_uint32(i);
}

template<>
inline jl_value_t* box(const uint64_t& i)
{
  return jl_box_uint64(i);
}

template<>
inline jl_value_t* box(const float& x)
{
  return jl_box_float32(x);
}

template<>
inline jl_value_t* box(const double& x)
{
  return jl_box_float64(x);
}

template<>
inline jl_value_t* box(jl_datatype_t* const& x)
{
  return (jl_value_t*)x;
}

template<>
inline jl_value_t* box(void* const& x)
{
  return jl_box_voidpointer(x);
}

template<typename T>
inline typename std::enable_if<IsFundamental<remove_const_ref<T>>::value, jl_value_t*>::type box(T* const& x)
{
  return jl_new_bits((jl_value_t*)dynamic_type_mapping<T*>::julia_type(), (void*)&x);
}

namespace detail
{
  inline jl_value_t* box_long(long x)
  {
    return jl_box_long(x);
  }

  inline jl_value_t* box_long(unused_type<long>)
  {
    // never called
    return nullptr;
  }

  inline jl_value_t* box_long_long(long long x)
  {
    return jl_box_int64(x);
  }

  inline jl_value_t* box_long_long(unused_type<long long>)
  {
    // never called
    return nullptr;
  }

  inline jl_value_t* box_us_long(unsigned long x)
  {
    if(sizeof(unsigned long) == 8)
    {
      return jl_box_uint64(x);
    }
    return jl_box_uint32(x);
  }

  inline jl_value_t* box_us_long(unused_type<unsigned long>)
  {
    // never called
    return nullptr;
  }
}

template<>
inline jl_value_t* box(const detail::define_if_different<long, int64_t>& x)
{
  return detail::box_long(x);
}

template<>
inline jl_value_t* box(const detail::define_if_different<unsigned long, uint64_t>& x)
{
  return detail::box_us_long(x);
}

template<>
inline jl_value_t* box(const detail::define_if_different<long long, int64_t>& x)
{
  return detail::box_long_long(x);
}

template<>
inline bool unbox(jl_value_t* v)
{
  return jl_unbox_bool(v);
}

template<>
inline float unbox(jl_value_t* v)
{
  return jl_unbox_float32(v);
}

template<>
inline double unbox(jl_value_t* v)
{
  return jl_unbox_float64(v);
}

template<>
inline int32_t unbox(jl_value_t* v)
{
  return jl_unbox_int32(v);
}

template<>
inline int64_t unbox(jl_value_t* v)
{
  return jl_unbox_int64(v);
}

template<>
inline uint32_t unbox(jl_value_t* v)
{
  return jl_unbox_uint32(v);
}

template<>
inline uint64_t unbox(jl_value_t* v)
{
  return jl_unbox_uint64(v);
}

template<>
inline void* unbox(jl_value_t* v)
{
  return jl_unbox_voidpointer(v);
}

// Fundamental type conversion
template<typename CppT>
struct ConvertToCpp<CppT, NoMappingTrait>
{
  inline CppT operator()(CppT julia_val) const
  {
    return julia_val;
  }

  CppT operator()(jl_value_t* julia_val) const
  {
    return unbox<CppT>(julia_val);
  }
};

/// Conversion of pointer types
template<typename CppT>
struct ConvertToCpp<CppT*, WrappedPtrTrait>
{
  inline CppT* operator()(WrappedCppPtr julia_val) const
  {
    return extract_pointer<CppT>(julia_val);
  }
};

template<typename CppT>
struct ConvertToCpp<CppT*, DirectPtrTrait>
{
  inline CppT* operator()(CppT* julia_val) const
  {
    return julia_val;
  }
};

/// Conversion of reference types
template<typename CppT>
struct ConvertToCpp<CppT&, WrappedPtrTrait>
{
  inline CppT& operator()(WrappedCppPtr julia_val) const
  {
    return *extract_pointer<CppT>(julia_val);
  }
};

template<typename CppT>
struct ConvertToCpp<CppT, CxxWrappedTrait>
{
  inline CppT operator()(WrappedCppPtr julia_val) const
  {
    return *extract_pointer<CppT>(julia_val);
  }
};

namespace detail
{

// Unpack based on reference or pointer target type
// Try to dereference by default
template<typename T>
struct DoUnpack
{
  template<typename CppT>
  T operator()(CppT* ptr)
  {
    if(ptr == nullptr)
    {
      throw std::runtime_error("C++ object was deleted");
    }

    return *ptr;
  }
};

// Return the pointer if a pointer was passed
template<typename T>
struct DoUnpack<T*>
{
  template<typename CppT>
  T* operator()(CppT* ptr)
  {
    return ptr;
  }
};

template<typename T>
struct DoUnpack<T*&&>
{
  template<typename CppT>
  T* operator()(CppT* ptr)
  {
    return ptr;
  }
};

/// Helper class to unpack a julia type
template<typename CppT>
struct JuliaUnpacker
{
  // The C++ type stripped of all pointer, reference, const
  typedef typename std::remove_const<typename std::remove_pointer<remove_const_ref<CppT>>::type>::type stripped_cpp_t;

  CppT operator()(const WrappedCppPtr& julia_value)
  {
    return DoUnpack<CppT>()(reinterpret_cast<stripped_cpp_t*>(julia_value.voidptr));
  }
};

} // namespace detail

// // Generic conversion for C++ classes wrapped in a composite type
// template<typename CppT>
// struct ConvertToCpp<CppT, false, false, false, typename std::enable_if<static_type_mapping<CppT>::is_dynamic>::type>
// {
//   CppT operator()(const WrappedCppPtr& julia_value) const
//   {
//     return detail::JuliaUnpacker<CppT>()(julia_value);
//   }

//   // pass-through for Julia pointers
//   template<typename JuliaPtrT>
//   JuliaPtrT* operator()(JuliaPtrT* julia_value) const
//   {
//     return julia_value;
//   }
// };

// strings
// template<>
// struct ConvertToCpp<const char*, false, false, false>
// {
//   const char* operator()(jl_value_t* jstr) const
//   {
//     if(jstr == nullptr || !is_julia_string(jstr))
//     {
//       throw std::runtime_error("Any type to convert to string is not a string but a " + julia_type_name((jl_datatype_t*)jl_typeof(jstr)));
//     }
//     return julia_string(jstr);
//   }
// };

// template<>
// struct ConvertToCpp<std::string, false, false, false>
// {
//   std::string operator()(jl_value_t* jstr) const
//   {
//     return std::string(ConvertToCpp<const char*, false, false, false>()(jstr));
//   }
// };

// template<>
// struct JLCXX_API ConvertToCpp<std::wstring, false, false, false>
// {
//   std::wstring operator()(jl_value_t* jstr) const;
// };

// template<typename T>
// struct ConvertToCpp<SingletonType<T>, false, false, false>
// {
//   SingletonType<T> operator()(jl_datatype_t*) const
//   {
//     return SingletonType<T>();
//   }
// };

// Used for deepcopy_internal overloading
// template<>
// struct ConvertToCpp<ObjectIdDict, false, false, false>
// {
//   ObjectIdDict operator()(jl_value_t*) const
//   {
//     return ObjectIdDict();
//   }
// };

/// Represent a Julia TypeVar in the template parameter list
template<int I>
struct TypeVar
{
  static constexpr int value = I;

  static jl_tvar_t* tvar()
  {
    static jl_tvar_t* this_tvar = build_tvar();
    return this_tvar;
  }

  static jl_tvar_t* build_tvar()
  {
    jl_tvar_t* result = jl_new_typevar(jl_symbol((std::string("T") + std::to_string(I)).c_str()), (jl_value_t*)jl_bottom_type, (jl_value_t*)jl_any_type);
    protect_from_gc(result);
    return result;
  }
};

template<int I> struct IsValueType<TypeVar<I>> : std::true_type {};

// template<int I>
// struct static_type_mapping<TypeVar<I>>
// {
//   typedef jl_tvar_t* type;
//   static jl_tvar_t* julia_type() { return TypeVar<I>::tvar(); }
// };

/// Helper to encapsulate a strictly typed number type. Numbers typed like this will not be involved in the convenience-overloads that allow passing e.g. an Int to a Float64 argument
template<typename NumberT>
struct StrictlyTypedNumber
{
  NumberT value;
};

template<typename NumberT> struct IsBits<StrictlyTypedNumber<NumberT>> : std::true_type {};
template<typename NumberT> struct IsImmutable<StrictlyTypedNumber<NumberT>> : std::true_type {};

// template<typename NumberT> struct static_type_mapping<StrictlyTypedNumber<NumberT>>
// {
//   typedef StrictlyTypedNumber<NumberT> type;
//   static jl_datatype_t* julia_type()
//   {
//     static jl_datatype_t* dt = nullptr;
//     if(dt == nullptr)
//     {
//       dt = (jl_datatype_t*)apply_type((jl_value_t*)::jlcxx::julia_type("StrictlyTypedNumber"), jl_svec1(static_type_mapping<NumberT>::julia_type()));
//       protect_from_gc(dt);
//     }
//     return dt;
//   }
// };

// // Match references to fundamental types (e.g. double&)
// template<typename T> struct static_type_mapping<T&, typename std::enable_if<IsFundamental<T>::value>::type>
// {
//   typedef T* type;
//   static jl_datatype_t* julia_type() { return (jl_datatype_t*)apply_type((jl_value_t*)::jlcxx::julia_type("Ref"), jl_svec1(static_type_mapping<T>::julia_type())); }
// };

// // References to pointers
// template<typename T> struct static_type_mapping<T*&, typename std::enable_if<!IsFundamental<T>::value>::type>
// {
//   typedef T** type;
//   static jl_datatype_t* julia_type() { return (jl_datatype_t*)apply_type((jl_value_t*)::jlcxx::julia_type("Ref"), jl_svec1(static_type_mapping<T>::julia_allocated_type())); }
// };

namespace detail
{

template<typename T>
struct JuliaComplex
{
  T real;
  T imag;
};

}

// Complex numbers
// template<typename NumberT> struct IsFundamental<std::complex<NumberT>> : std::true_type {};

// template<typename NumberT> struct static_type_mapping<std::complex<NumberT>>
// {
//   typedef std::complex<NumberT> type;
//   static jl_datatype_t* julia_type()
//   {
//     static jl_datatype_t* dt = nullptr;
//     if(dt == nullptr)
//     {
//       dt = (jl_datatype_t*)apply_type(jlcxx::julia_type("Complex"), jl_svec1(static_type_mapping<NumberT>::julia_type()));
//       protect_from_gc(dt);
//     }
//     return dt;
//   }
// };

// template<typename NumberT>
// struct ConvertToCpp<std::complex<NumberT>, true, false, false>
// {
//   std::complex<NumberT> operator()(std::complex<NumberT> julia_value) const
//   {
//     return julia_value;
//   }
// };

// template<typename NumberT>
// struct ConvertToJulia<std::complex<NumberT>, true, false, false>
// {
//   detail::JuliaComplex<NumberT> operator()(std::complex<NumberT> cpp_value) const
//   {
//     return {cpp_value.real(), cpp_value.imag()};
//   }
// };

}

#endif
