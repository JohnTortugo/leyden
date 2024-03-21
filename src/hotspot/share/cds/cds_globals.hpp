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

#ifndef SHARE_CDS_CDS_GLOBALS_HPP
#define SHARE_CDS_CDS_GLOBALS_HPP

#include "runtime/globals_shared.hpp"

//
// Defines all globals flags used by CDS.
//

#define CDS_FLAGS(develop,                                                  \
                  develop_pd,                                               \
                  product,                                                  \
                  product_pd,                                               \
                  notproduct,                                               \
                  range,                                                    \
                  constraint)                                               \
  /* Shared spaces */                                                       \
                                                                            \
  product(bool, VerifySharedSpaces, false,                                  \
          "Verify integrity of shared spaces")                              \
                                                                            \
  product(bool, RecordDynamicDumpInfo, false,                               \
          "Record class info for jcmd VM.cds dynamic_dump")                 \
                                                                            \
  product(bool, AutoCreateSharedArchive, false,                             \
          "Create shared archive at exit if cds mapping failed")            \
                                                                            \
  product(bool, PrintSharedArchiveAndExit, false,                           \
          "Print shared archive file contents")                             \
                                                                            \
  product(size_t, SharedBaseAddress, LP64_ONLY(32*G)                        \
          NOT_LP64(LINUX_ONLY(2*G) NOT_LINUX(0)),                           \
          "Address to allocate shared memory region for class data")        \
          range(0, SIZE_MAX)                                                \
                                                                            \
  product(ccstr, SharedArchiveConfigFile, nullptr,                          \
          "Data to add to the CDS archive file")                            \
                                                                            \
  product(uint, SharedSymbolTableBucketSize, 4,                             \
          "Average number of symbols per bucket in shared table")           \
          range(2, 246)                                                     \
                                                                            \
  product(bool, AllowArchivingWithJavaAgent, false, DIAGNOSTIC,             \
          "Allow Java agent to be run with CDS dumping")                    \
                                                                            \
  develop(ccstr, ArchiveHeapTestClass, nullptr,                             \
          "For JVM internal testing only. The static field named "          \
          "\"archivedObjects\" of the specified class is stored in the "    \
          "CDS archive heap")                                               \
                                                                            \
  product(ccstr, DumpLoadedClassList, nullptr,                              \
          "Dump the names all loaded classes, that could be stored into "   \
          "the CDS archive, in the specified file")                         \
                                                                            \
  product(ccstr, SharedClassListFile, nullptr,                              \
          "Override the default CDS class list")  \
                                                                            \
  product(ccstr, SharedArchiveFile, nullptr,                                \
          "Override the default location of the CDS archive file")          \
                                                                            \
  product(ccstr, ArchiveClassesAtExit, nullptr,                             \
          "The path and name of the dynamic archive file")                  \
                                                                            \
  product(ccstr, ExtraSharedClassListFile, nullptr,                         \
          "Extra classlist for building the CDS archive file")              \
                                                                            \
  /*FIXME - AOT code has direct pointers to metadata that's not relocated*/ \
  product(int, ArchiveRelocationMode, 0, DIAGNOSTIC,                        \
           "(0) first map at preferred address, and if "                    \
           "unsuccessful, map at alternative address; "                     \
           "(1) always map at alternative address (default); "              \
           "(2) always map at preferred address, and if unsuccessful, "     \
           "do not map the archive")                                        \
           range(0, 2)                                                      \
                                                                            \
  /*========== New options added by Leyden =============================*/  \
                                                                            \
  product(ccstr, CacheDataStore, nullptr,                                   \
          "If valid, use the specified file for SharedArchiveFile; "        \
          "otherwise the specified file is generated at program exit")      \
                                                                            \
  product(ccstr, CDSPreimage, nullptr,                                      \
          "(** internal use only **) -- used by a child JVM process to "    \
          "create the CacheDataStore final image")                          \
                                                                            \
  product(bool, CDSManualFinalImage, false, DIAGNOSTIC,                     \
          "(** internal use only **) -- if false, automatically launch a "  \
          "child process to create the final image.")                       \
                                                                            \
  /* To be renamed to CDSLoadedClasses */                                   \
  product(bool, PreloadSharedClasses, false,                                \
          "Load all shared classes for the boot/platform/app loaders "      \
          "immediately at VM start-up")                                     \
                                                                            \
  product(bool, PrelinkSharedClasses, true,                                 \
          "Link all shared classes for the boot/platform/app loaders "      \
          "immediately at VM start-up")                                     \
                                                                            \
  product(bool, ArchiveDynamicProxies, false,                               \
          "Archive classes generated for java/lang/reflect/Proxy")          \
                                                                            \
  product(bool, ArchiveFieldReferences, true,                               \
          "Archive resolved JVM_CONSTANT_Fieldref in ConstantPool")         \
                                                                            \
  product(bool, ArchiveInvokeDynamic, false,                                \
          "Archive resolved JVM_CONSTANT_InvokeDynamic in ConstantPool")    \
                                                                            \
  product(bool, ArchiveLoaderLookupCache, false,                            \
          "Archive app loader's positive and negative lookup cache")        \
                                                                            \
  product(bool, ArchiveMethodReferences, true,                              \
          "Archive resolved JVM_CONSTANT_Methodref and "                    \
          "JVM_CONSTANT_InterfaceMethodref in ConstantPool")                \
                                                                            \
  product(bool, ArchivePackages, false,                                     \
          "Archive the java.lang.ClassLoader::{packages,package2certs} "    \
          "tables")                                                         \
                                                                            \
  product(bool, ArchiveReflectionData, false,                               \
          "Archive Class::reflectionData field")                            \
                                                                            \
  product(bool, TempDisableAddJVMCIModule, false,                           \
          "Do not add jdk.internal.vm.ci module even for -XX:+EnableJVMCI") \
                                                                            \
  product(bool, UsePermanentHeapObjects, false, DIAGNOSTIC,                 \
          "Allow AOT code to access permanent archived heap objects")       \
                                                                            \
  product(bool, VerifyTrainingData, trueInDebug, DIAGNOSTIC,                \
          "Verify archived training data")                                  \

// end of CDS_FLAGS

DECLARE_FLAGS(CDS_FLAGS)

#endif // SHARE_CDS_CDS_GLOBALS_HPP
