/*
 * Copyright (c) 2022, 2024, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

#ifndef SHARE_CDS_AOTCONSTANTPOOLRESOLVER_HPP
#define SHARE_CDS_AOTCONSTANTPOOLRESOLVER_HPP

#include "interpreter/bytecodes.hpp"
#include "oops/oopsHierarchy.hpp"
#include "memory/allStatic.hpp"
#include "memory/allocation.hpp"
#include "runtime/handles.hpp"
#include "utilities/exceptions.hpp"
#include "utilities/macros.hpp"
#include "utilities/resourceHash.hpp"

class ConstantPool;
class constantPoolHandle;
class InstanceKlass;
class Klass;

template <typename T> class GrowableArray;

// AOTConstantPoolResolver is used to perform ahead-of-time linking of ConstantPool entries
// for archived InstanceKlasses.
//
// At run time, Java classes are loaded dynamically and may be replaced with JVMTI.
// Therefore, we take care to prelink only the ConstantPool entries that are
// guatanteed to resolve to the same results at both dump time and run time.
//
// For example, a JVM_CONSTANT_Class reference to a supertype can be safely resolved
// at dump time, because at run time we will load a class from the CDS archive only
// if all of its supertypes are loaded from the CDS archive.
class AOTConstantPoolResolver :  AllStatic {
  using ClassesTable = ResourceHashtable<InstanceKlass*, bool, 15889, AnyObj::C_HEAP, mtClassShared> ;
  static ClassesTable* _processed_classes;

#ifdef ASSERT
  template <typename T> static bool is_in_archivebuilder_buffer(T p) {
    return is_in_archivebuilder_buffer((address)(p));
  }
  static bool is_in_archivebuilder_buffer(address p);
#endif

  static void resolve_string(constantPoolHandle cp, int cp_index, TRAPS) NOT_CDS_JAVA_HEAP_RETURN;
  static bool is_class_resolution_deterministic(InstanceKlass* cp_holder, Klass* resolved_class);
  static bool is_indy_resolution_deterministic(ConstantPool* cp, int cp_index);

  // helper
  static Klass* resolve_boot_class_or_fail(const char* class_name, TRAPS);

  // java/lang/reflect/Proxy caching
  static void init_dynamic_proxy_cache(TRAPS);

  static Klass* find_loaded_class(Thread* current, oop class_loader, Symbol* name);
  static Klass* find_loaded_class(Thread* current, ConstantPool* cp, int class_cp_index);

  // fmi = FieldRef/MethodRef/InterfaceMethodRef
  static void maybe_resolve_fmi_ref(InstanceKlass* ik, Method* m, Bytecodes::Code bc, int raw_index,
                                    GrowableArray<bool>* resolve_fmi_list, TRAPS);
public:
  static void initialize();
  static void dispose();

  static void preresolve_class_cp_entries(JavaThread* current, InstanceKlass* ik, GrowableArray<bool>* preresolve_list);
  static void preresolve_field_and_method_cp_entries(JavaThread* current, InstanceKlass* ik, GrowableArray<bool>* preresolve_list);
  static void preresolve_indy_cp_entries(JavaThread* current, InstanceKlass* ik, GrowableArray<bool>* preresolve_list);

  // java/lang/Class$ReflectionData caching
  static int class_reflection_data_flags(InstanceKlass* ik, TRAPS);
  static void generate_reflection_data(JavaThread* current, InstanceKlass* ik, int rd_flags);

  // java/lang/reflect/Proxy caching
  static void trace_dynamic_proxy_class(oop loader, const char* proxy_name, objArrayOop interfaces, int access_flags);
  static void define_dynamic_proxy_class(Handle loader, Handle proxy_name, Handle interfaces, int access_flags, TRAPS);

  // Resolve all constant pool entries that are safe to be stored in the
  // CDS archive.
  static void dumptime_resolve_constants(InstanceKlass* ik, TRAPS);

  static bool is_resolution_deterministic(ConstantPool* cp, int cp_index);
};

#endif // SHARE_CDS_AOTCONSTANTPOOLRESOLVER_HPP
