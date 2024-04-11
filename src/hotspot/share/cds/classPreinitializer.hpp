/*
 * Copyright (c) 2024, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_CDS_CLASSPREINITIALIZER_HPP
#define SHARE_CDS_CLASSPREINITIALIZER_HPP

#include "memory/allStatic.hpp"
#include "utilities/exceptions.hpp"

class InstanceKlass;

class ClassPreinitializer : AllStatic {
  static bool has_non_default_static_fields(InstanceKlass* ik);
  static bool is_forced_preinit_class(InstanceKlass* ik);

  static bool check_can_be_preinited(InstanceKlass* ik);
  static bool can_be_preinited(InstanceKlass* ik);
  static bool can_be_preinited_locked(InstanceKlass* ik);

public:
  // Called by metaspaceShared.cpp to speculatively initialize a class (if the results
  // of initialization can be safely archived.
  static void maybe_preinit_class(InstanceKlass* ik, TRAPS);

  static void reset_preinit_check();

  // Called by heapShared.cpp to see if src_ik->java_mirror() can be archived in
  // the initialized state.
  static bool can_archive_preinitialized_mirror(InstanceKlass* src_ik);
};

#endif // SHARE_CDS_CLASSPREINITIALIZER_HPP
