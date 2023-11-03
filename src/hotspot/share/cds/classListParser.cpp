/*
 * Copyright (c) 2015, 2023, Oracle and/or its affiliates. All rights reserved.
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

#include "precompiled.hpp"
#include "cds/archiveUtils.hpp"
#include "cds/classListParser.hpp"
#include "cds/classPrelinker.hpp"
#include "cds/lambdaFormInvokers.hpp"
#include "cds/metaspaceShared.hpp"
#include "cds/unregisteredClasses.hpp"
#include "classfile/classLoaderExt.hpp"
#include "classfile/javaClasses.inline.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/systemDictionaryShared.hpp"
#include "classfile/vmClasses.hpp"
#include "classfile/vmSymbols.hpp"
#include "interpreter/bytecode.hpp"
#include "interpreter/interpreterRuntime.hpp"
#include "interpreter/linkResolver.hpp"
#include "jimage.hpp"
#include "jvm.h"
#include "logging/log.hpp"
#include "logging/logTag.hpp"
#include "memory/resourceArea.hpp"
#include "oops/constantPool.inline.hpp"
#include "oops/cpCache.inline.hpp"
#include "runtime/atomic.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/java.hpp"
#include "runtime/javaCalls.hpp"
#include "utilities/defaultStream.hpp"
#include "utilities/macros.hpp"

const char* ClassListParser::LAMBDA_PROXY_TAG = "@lambda-proxy";
const char* ClassListParser::LAMBDA_FORM_TAG  = "@lambda-form-invoker";
const char* ClassListParser::CLASS_REFLECTION_DATA_TAG  = "@class-reflection-data";

const char* ClassListParser::CONSTANT_POOL_TAG  = "@cp";
volatile Thread* ClassListParser::_parsing_thread = nullptr;
ClassListParser* ClassListParser::_instance = nullptr;

ClassListParser::ClassListParser(const char* file, ParseMode parse_mode) : _id2klass_table(INITIAL_TABLE_SIZE, MAX_TABLE_SIZE) {
  log_info(cds)("Parsing %s%s", file,
                (parse_mode == _parse_lambda_forms_invokers_only) ? " (lambda form invokers only)" : "");
  _classlist_file = file;
  _file = nullptr;
  // Use os::open() because neither fopen() nor os::fopen()
  // can handle long path name on Windows.
  int fd = os::open(file, O_RDONLY, S_IREAD);
  if (fd != -1) {
    // Obtain a File* from the file descriptor so that fgets()
    // can be used in parse_one_line()
    _file = os::fdopen(fd, "r");
  }
  if (_file == nullptr) {
    char errmsg[JVM_MAXPATHLEN];
    os::lasterror(errmsg, JVM_MAXPATHLEN);
    vm_exit_during_initialization("Loading classlist failed", errmsg);
  }
  _line_no = 0;
  _token = _line;
  _interfaces = new (mtClass) GrowableArray<int>(10, mtClass);
  _indy_items = new (mtClass) GrowableArray<const char*>(9, mtClass);
  _parse_mode = parse_mode;

  // _instance should only be accessed by the thread that created _instance.
  assert(_instance == nullptr, "must be singleton");
  _instance = this;
  Atomic::store(&_parsing_thread, Thread::current());
}

bool ClassListParser::is_parsing_thread() {
  return Atomic::load(&_parsing_thread) == Thread::current();
}

ClassListParser::~ClassListParser() {
  if (_file != nullptr) {
    fclose(_file);
  }
  Atomic::store(&_parsing_thread, (Thread*)nullptr);
  delete _indy_items;
  delete _interfaces;
  _instance = nullptr;
}

int ClassListParser::parse(TRAPS) {
  int class_count = 0;

  while (parse_one_line()) {
    if (lambda_form_line()) {
      // The current line is "@lambda-form-invoker ...". It has been recorded in LambdaFormInvokers,
      // and will be processed later.
      continue;
    }
    if (_constant_pool_line) {
      continue;
    }
    if (_class_reflection_data_line) {
      continue;
    }
    if (_parse_mode == _parse_lambda_forms_invokers_only) {
      continue;
    }

    TempNewSymbol class_name_symbol = SymbolTable::new_symbol(_class_name);
    if (_indy_items->length() > 0) {
      // The current line is "@lambda-proxy class_name". Load the proxy class.
      resolve_indy(THREAD, class_name_symbol);
      class_count++;
      continue;
    }

    Klass* klass = load_current_class(class_name_symbol, THREAD);
    if (HAS_PENDING_EXCEPTION) {
      if (PENDING_EXCEPTION->is_a(vmClasses::OutOfMemoryError_klass())) {
        // If we have run out of memory, don't try to load the rest of the classes in
        // the classlist. Throw an exception, which will terminate the dumping process.
        return 0; // THROW
      }

      ResourceMark rm(THREAD);
      char* ex_msg = (char*)"";
      oop message = java_lang_Throwable::message(PENDING_EXCEPTION);
      if (message != nullptr) {
        ex_msg = java_lang_String::as_utf8_string(message);
      }
      log_warning(cds)("%s: %s", PENDING_EXCEPTION->klass()->external_name(), ex_msg);
      // We might have an invalid class name or an bad class. Warn about it
      // and keep going to the next line.
      CLEAR_PENDING_EXCEPTION;
      log_warning(cds)("Preload Warning: Cannot find %s", _class_name);
      continue;
    }

    assert(klass != nullptr, "sanity");
    if (log_is_enabled(Trace, cds)) {
      ResourceMark rm(THREAD);
      log_trace(cds)("Shared spaces preloaded: %s", klass->external_name());
    }

    if (klass->is_instance_klass()) {
      InstanceKlass* ik = InstanceKlass::cast(klass);

      // Link the class to cause the bytecodes to be rewritten and the
      // cpcache to be created. The linking is done as soon as classes
      // are loaded in order that the related data structures (klass and
      // cpCache) are located together.
      MetaspaceShared::try_link_class(THREAD, ik);
    }

    class_count++;
  }

  return class_count;
}

bool ClassListParser::parse_one_line() {
  for (;;) {
    if (fgets(_line, sizeof(_line), _file) == nullptr) {
      return false;
    }
    ++ _line_no;
    _line_len = (int)strlen(_line);
    if (_line_len > _max_allowed_line_len) {
      error("input line too long (must be no longer than %d chars)", _max_allowed_line_len);
    }
    if (*_line == '#') { // comment
      continue;
    }

    {
      int len = (int)strlen(_line);
      int i;
      // Replace \t\r\n\f with ' '
      for (i=0; i<len; i++) {
        if (_line[i] == '\t' || _line[i] == '\r' || _line[i] == '\n' || _line[i] == '\f') {
          _line[i] = ' ';
        }
      }

      // Remove trailing newline/space
      while (len > 0) {
        if (_line[len-1] == ' ') {
          _line[len-1] = '\0';
          len --;
        } else {
          break;
        }
      }
      _line_len = len;
    }

    // valid line
    break;
  }

  _class_name = _line;
  _id = _unspecified;
  _super = _unspecified;
  _interfaces->clear();
  _source = nullptr;
  _interfaces_specified = false;
  _indy_items->clear();
  _lambda_form_line = false;
  _constant_pool_line = false;
  _class_reflection_data_line = false;

  if (_line[0] == '@') {
    return parse_at_tags();
  }

  if ((_token = strchr(_line, ' ')) == nullptr) {
    // No optional arguments are specified.
    return true;
  }

  // Mark the end of the name, and go to the next input char
  *_token++ = '\0';

  while (*_token) {
    skip_whitespaces();

    if (parse_uint_option("id:", &_id)) {
      continue;
    } else if (parse_uint_option("super:", &_super)) {
      check_already_loaded("Super class", _super);
      continue;
    } else if (skip_token("interfaces:")) {
      int i;
      while (try_parse_uint(&i)) {
        check_already_loaded("Interface", i);
        _interfaces->append(i);
      }
    } else if (skip_token("source:")) {
      skip_whitespaces();
      _source = _token;
      char* s = strchr(_token, ' ');
      if (s == nullptr) {
        break; // end of input line
      } else {
        *s = '\0'; // mark the end of _source
        _token = s+1;
      }
    } else {
      error("Unknown input");
    }
  }

  // if src is specified
  //     id super interfaces must all be specified
  //     loader may be specified
  // else
  //     # the class is loaded from classpath
  //     id may be specified
  //     super, interfaces, loader must not be specified
  return true;
}

void ClassListParser::split_tokens_by_whitespace(int offset) {
  int start = offset;
  int end;
  bool done = false;
  while (!done) {
    while (_line[start] == ' ' || _line[start] == '\t') start++;
    end = start;
    while (_line[end] && _line[end] != ' ' && _line[end] != '\t') end++;
    if (_line[end] == '\0') {
      done = true;
    } else {
      _line[end] = '\0';
    }
    _indy_items->append(_line + start);
    start = ++end;
  }
}

int ClassListParser::split_at_tag_from_line() {
  _token = _line;
  char* ptr;
  if ((ptr = strchr(_line, ' ')) == nullptr) {
    error("Too few items following the @ tag \"%s\" line #%d", _line, _line_no);
    return 0;
  }
  *ptr++ = '\0';
  while (*ptr == ' ' || *ptr == '\t') ptr++;
  return (int)(ptr - _line);
}

bool ClassListParser::parse_at_tags() {
  assert(_line[0] == '@', "must be");
  int offset;
  if ((offset = split_at_tag_from_line()) == 0) {
    return false;
  }

  if (strcmp(_token, LAMBDA_PROXY_TAG) == 0) {
    split_tokens_by_whitespace(offset);
    if (_indy_items->length() < 2) {
      error("Line with @ tag has too few items \"%s\" line #%d", _token, _line_no);
      return false;
    }
    // set the class name
    _class_name = _indy_items->at(0);
    return true;
  } else if (strcmp(_token, LAMBDA_FORM_TAG) == 0) {
    LambdaFormInvokers::append(os::strdup((const char*)(_line + offset), mtInternal));
    _lambda_form_line = true;
    return true;
  } else if (strcmp(_token, CONSTANT_POOL_TAG) == 0) {
    _token = _line + offset;
    _constant_pool_line = true;
    parse_constant_pool_tag();
    return true;
  } else if (strcmp(_token, CLASS_REFLECTION_DATA_TAG) == 0) {
    _token = _line + offset;
    _class_reflection_data_line = true;
    parse_class_reflection_data_tag();
    return true;
  } else {
    error("Invalid @ tag at the beginning of line \"%s\" line #%d", _token, _line_no);
    return false;
  }
}

void ClassListParser::skip_whitespaces() {
  while (*_token == ' ' || *_token == '\t') {
    _token ++;
  }
}

void ClassListParser::skip_non_whitespaces() {
  while (*_token && *_token != ' ' && *_token != '\t') {
    _token ++;
  }
}

void ClassListParser::parse_int(int* value) {
  skip_whitespaces();
  if (sscanf(_token, "%i", value) == 1) {
    skip_non_whitespaces();
  } else {
    error("Error: expected integer");
  }
}

void ClassListParser::parse_uint(int* value) {
  parse_int(value);
  if (*value < 0) {
    error("Error: negative integers not allowed (%d)", *value);
  }
}

bool ClassListParser::try_parse_uint(int* value) {
  skip_whitespaces();
  if (sscanf(_token, "%i", value) == 1) {
    skip_non_whitespaces();
    return true;
  }
  return false;
}

bool ClassListParser::skip_token(const char* option_name) {
  size_t len = strlen(option_name);
  if (strncmp(_token, option_name, len) == 0) {
    _token += len;
    return true;
  } else {
    return false;
  }
}

bool ClassListParser::parse_int_option(const char* option_name, int* value) {
  if (skip_token(option_name)) {
    if (*value != _unspecified) {
      error("%s specified twice", option_name);
    } else {
      parse_int(value);
      return true;
    }
  }
  return false;
}

bool ClassListParser::parse_uint_option(const char* option_name, int* value) {
  if (skip_token(option_name)) {
    if (*value != _unspecified) {
      error("%s specified twice", option_name);
    } else {
      parse_uint(value);
      return true;
    }
  }
  return false;
}

void ClassListParser::print_specified_interfaces() {
  const int n = _interfaces->length();
  jio_fprintf(defaultStream::error_stream(), "Currently specified interfaces[%d] = {\n", n);
  for (int i=0; i<n; i++) {
    InstanceKlass* k = lookup_class_by_id(_interfaces->at(i));
    jio_fprintf(defaultStream::error_stream(), "  %4d = %s\n", _interfaces->at(i), k->name()->as_klass_external_name());
  }
  jio_fprintf(defaultStream::error_stream(), "}\n");
}

void ClassListParser::print_actual_interfaces(InstanceKlass* ik) {
  int n = ik->local_interfaces()->length();
  jio_fprintf(defaultStream::error_stream(), "Actual interfaces[%d] = {\n", n);
  for (int i = 0; i < n; i++) {
    InstanceKlass* e = ik->local_interfaces()->at(i);
    jio_fprintf(defaultStream::error_stream(), "  %s\n", e->name()->as_klass_external_name());
  }
  jio_fprintf(defaultStream::error_stream(), "}\n");
}

void ClassListParser::print_diagnostic_info(outputStream* st, const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  print_diagnostic_info(st, msg, ap);
  va_end(ap);
}

void ClassListParser::print_diagnostic_info(outputStream* st, const char* msg, va_list ap) {
  int error_index = pointer_delta_as_int(_token, _line);
  if (error_index >= _line_len) {
    error_index = _line_len - 1;
  }
  if (error_index < 0) {
    error_index = 0;
  }

  st->print("An error has occurred while processing class list file %s %d:%d.\n",
            _classlist_file, _line_no, (error_index + 1));
  st->vprint(msg, ap);

  if (_line_len <= 0) {
    st->print("\n");
  } else {
    st->print(":\n");
    for (int i=0; i<_line_len; i++) {
      char c = _line[i];
      if (c == '\0') {
        st->print("%s", " ");
      } else {
        st->print("%c", c);
      }
    }
    st->print("\n");
    for (int i=0; i<error_index; i++) {
      st->print("%s", " ");
    }
    st->print("^\n");
  }
}

void ClassListParser::error(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  LogTarget(Error, cds) lt;
  LogStream ls(lt);
  print_diagnostic_info(&ls, msg, ap);
  vm_exit_during_initialization("class list format error.", nullptr);
  va_end(ap);
}

void ClassListParser::constant_pool_resolution_warning(const char* msg, ...) {
  va_list ap;
  va_start(ap, msg);
  LogTarget(Warning, cds, resolve) lt;
  LogStream ls(lt);
  print_diagnostic_info(&ls, msg, ap);
  ls.print("Your classlist may be out of sync with the JDK or the application.");
  va_end(ap);
}

// This function is used for loading classes for customized class loaders
// during archive dumping.
InstanceKlass* ClassListParser::load_class_from_source(Symbol* class_name, TRAPS) {
#if !(defined(_LP64) && (defined(LINUX) || defined(__APPLE__) || defined(_WINDOWS)))
  // The only supported platforms are: (1) Linux/64-bit and (2) Solaris/64-bit and
  // (3) MacOSX/64-bit and (4) Windowss/64-bit
  // This #if condition should be in sync with the areCustomLoadersSupportedForCDS
  // method in test/lib/jdk/test/lib/Platform.java.
  error("AppCDS custom class loaders not supported on this platform");
#endif

  if (!is_super_specified()) {
    error("If source location is specified, super class must be also specified");
  }
  if (!is_id_specified()) {
    error("If source location is specified, id must be also specified");
  }
  if (strncmp(_class_name, "java/", 5) == 0) {
    log_info(cds)("Prohibited package for non-bootstrap classes: %s.class from %s",
          _class_name, _source);
    THROW_NULL(vmSymbols::java_lang_ClassNotFoundException());
  }

  InstanceKlass* k = UnregisteredClasses::load_class(class_name, _source, CHECK_NULL);
  if (k->local_interfaces()->length() != _interfaces->length()) {
    print_specified_interfaces();
    print_actual_interfaces(k);
    error("The number of interfaces (%d) specified in class list does not match the class file (%d)",
          _interfaces->length(), k->local_interfaces()->length());
  }

  assert(k->is_shared_unregistered_class(), "must be");

  bool added = SystemDictionaryShared::add_unregistered_class(THREAD, k);
  if (!added) {
    // We allow only a single unregistered class for each unique name.
    error("Duplicated class %s", _class_name);
  }

  return k;
}

void ClassListParser::populate_cds_indy_info(const constantPoolHandle &pool, int cp_index, CDSIndyInfo* cii, TRAPS) {
  // Caller needs to allocate ResourceMark.
  int type_index = pool->bootstrap_name_and_type_ref_index_at(cp_index);
  int name_index = pool->name_ref_index_at(type_index);
  cii->add_item(pool->symbol_at(name_index)->as_C_string());
  int sig_index = pool->signature_ref_index_at(type_index);
  cii->add_item(pool->symbol_at(sig_index)->as_C_string());
  int argc = pool->bootstrap_argument_count_at(cp_index);
  if (argc > 0) {
    for (int arg_i = 0; arg_i < argc; arg_i++) {
      int arg = pool->bootstrap_argument_index_at(cp_index, arg_i);
      jbyte tag = pool->tag_at(arg).value();
      if (tag == JVM_CONSTANT_MethodType) {
        cii->add_item(pool->method_type_signature_at(arg)->as_C_string());
      } else if (tag == JVM_CONSTANT_MethodHandle) {
        cii->add_ref_kind(pool->method_handle_ref_kind_at(arg));
        int callee_index = pool->method_handle_klass_index_at(arg);
        Klass* callee = pool->klass_at(callee_index, CHECK);
        cii->add_item(callee->name()->as_C_string());
        cii->add_item(pool->method_handle_name_ref_at(arg)->as_C_string());
        cii->add_item(pool->method_handle_signature_ref_at(arg)->as_C_string());
      } else {
        ShouldNotReachHere();
      }
    }
  }
}

bool ClassListParser::is_matching_cp_entry(const constantPoolHandle &pool, int cp_index, TRAPS) {
  ResourceMark rm(THREAD);
  CDSIndyInfo cii;
  populate_cds_indy_info(pool, cp_index, &cii, CHECK_0);
  GrowableArray<const char*>* items = cii.items();
  int indy_info_offset = 1;
  if (_indy_items->length() - indy_info_offset != items->length()) {
    return false;
  }
  for (int i = 0; i < items->length(); i++) {
    if (strcmp(_indy_items->at(i + indy_info_offset), items->at(i)) != 0) {
      return false;
    }
  }
  return true;
}

void ClassListParser::resolve_indy(JavaThread* current, Symbol* class_name_symbol) {
  ExceptionMark em(current);
  JavaThread* THREAD = current; // For exception macros.
  ClassListParser::resolve_indy_impl(class_name_symbol, THREAD);
  if (HAS_PENDING_EXCEPTION) {
    ResourceMark rm(current);
    char* ex_msg = (char*)"";
    oop message = java_lang_Throwable::message(PENDING_EXCEPTION);
    if (message != nullptr) {
      ex_msg = java_lang_String::as_utf8_string(message);
    }
    log_warning(cds)("resolve_indy for class %s has encountered exception: %s %s",
                     class_name_symbol->as_C_string(),
                     PENDING_EXCEPTION->klass()->external_name(),
                     ex_msg);
    CLEAR_PENDING_EXCEPTION;
  }
}

void ClassListParser::resolve_indy_impl(Symbol* class_name_symbol, TRAPS) {
  Handle class_loader(THREAD, SystemDictionary::java_system_loader());
  Handle protection_domain;
  Klass* klass = SystemDictionary::resolve_or_fail(class_name_symbol, class_loader, protection_domain, true, CHECK);
  if (klass->is_instance_klass()) {
    InstanceKlass* ik = InstanceKlass::cast(klass);
    MetaspaceShared::try_link_class(THREAD, ik);
    if (!ik->is_linked()) {
      // Verification of ik has failed
      return;
    }

    ConstantPool* cp = ik->constants();
    ConstantPoolCache* cpcache = cp->cache();
    bool found = false;
    for (int indy_index = 0; indy_index < cpcache->resolved_indy_entries_length(); indy_index++) {
      int pool_index = cpcache->resolved_indy_entry_at(indy_index)->constant_pool_index();
      constantPoolHandle pool(THREAD, cp);
      BootstrapInfo bootstrap_specifier(pool, pool_index, indy_index);
      Handle bsm = bootstrap_specifier.resolve_bsm(CHECK);
      if (!SystemDictionaryShared::is_supported_invokedynamic(&bootstrap_specifier)) {
        log_debug(cds, lambda)("is_supported_invokedynamic check failed for cp_index %d", pool_index);
        continue;
      }
      bool matched = is_matching_cp_entry(pool, pool_index, CHECK);
      if (matched) {
        found = true;
        CallInfo info;
        bool is_done = bootstrap_specifier.resolve_previously_linked_invokedynamic(info, CHECK);
        if (!is_done) {
          // resolve it
          Handle recv;
          LinkResolver::resolve_invoke(info,
                                       recv,
                                       pool,
                                       ConstantPool::encode_invokedynamic_index(indy_index),
                                       Bytecodes::_invokedynamic, CHECK);
          break;
        }
        cpcache->set_dynamic_call(info, indy_index);
      }
    }
    if (!found) {
      ResourceMark rm(THREAD);
      log_warning(cds)("No invoke dynamic constant pool entry can be found for class %s. The classlist is probably out-of-date.",
                     class_name_symbol->as_C_string());
    }
  }
}

Klass* ClassListParser::load_current_class(Symbol* class_name_symbol, TRAPS) {
  Klass* klass;
  if (!is_loading_from_source()) {
    // Load classes for the boot/platform/app loaders only.
    if (is_super_specified()) {
      error("If source location is not specified, super class must not be specified");
    }
    if (are_interfaces_specified()) {
      error("If source location is not specified, interface(s) must not be specified");
    }

    if (Signature::is_array(class_name_symbol)) {
      // array classes are not supported in class list.
      THROW_NULL(vmSymbols::java_lang_ClassNotFoundException());
    }

    JavaValue result(T_OBJECT);
    // Call java_system_loader().loadClass() directly, which will
    // delegate to the correct loader (boot, platform or app) depending on
    // the package name.

    // ClassLoader.loadClass() wants external class name format, i.e., convert '/' chars to '.'
    Handle ext_class_name = java_lang_String::externalize_classname(class_name_symbol, CHECK_NULL);
    Handle loader = Handle(THREAD, SystemDictionary::java_system_loader());

    JavaCalls::call_virtual(&result,
                            loader, //SystemDictionary::java_system_loader(),
                            vmClasses::ClassLoader_klass(),
                            vmSymbols::loadClass_name(),
                            vmSymbols::string_class_signature(),
                            ext_class_name,
                            CHECK_NULL);

    assert(result.get_type() == T_OBJECT, "just checking");
    oop obj = result.get_oop();
    assert(obj != nullptr, "jdk.internal.loader.BuiltinClassLoader::loadClass never returns null");
    klass = java_lang_Class::as_Klass(obj);
  } else {
    // If "source:" tag is specified, all super class and super interfaces must be specified in the
    // class list file.
    klass = load_class_from_source(class_name_symbol, CHECK_NULL);
  }

  assert(klass != nullptr, "exception should have been thrown");
  assert(klass->is_instance_klass(), "array classes should have been filtered out");

  if (is_id_specified()) {
    InstanceKlass* ik = InstanceKlass::cast(klass);
    int id = this->id();
    SystemDictionaryShared::update_shared_entry(ik, id);
    bool created;
    id2klass_table()->put_if_absent(id, ik, &created);
    if (!created) {
      error("Duplicated ID %d for class %s", id, _class_name);
    }
    if (id2klass_table()->maybe_grow()) {
      log_info(cds, hashtables)("Expanded id2klass_table() to %d", id2klass_table()->table_size());
    }
  }

  return klass;
}

bool ClassListParser::is_loading_from_source() {
  return (_source != nullptr);
}

InstanceKlass* ClassListParser::lookup_class_by_id(int id) {
  InstanceKlass** klass_ptr = id2klass_table()->get(id);
  if (klass_ptr == nullptr) {
    error("Class ID %d has not been defined", id);
  }
  assert(*klass_ptr != nullptr, "must be");
  return *klass_ptr;
}


InstanceKlass* ClassListParser::lookup_super_for_current_class(Symbol* super_name) {
  if (!is_loading_from_source()) {
    return nullptr;
  }

  InstanceKlass* k = lookup_class_by_id(super());
  if (super_name != k->name()) {
    error("The specified super class %s (id %d) does not match actual super class %s",
          k->name()->as_klass_external_name(), super(),
          super_name->as_klass_external_name());
  }
  return k;
}

InstanceKlass* ClassListParser::lookup_interface_for_current_class(Symbol* interface_name) {
  if (!is_loading_from_source()) {
    return nullptr;
  }

  const int n = _interfaces->length();
  if (n == 0) {
    error("Class %s implements the interface %s, but no interface has been specified in the input line",
          _class_name, interface_name->as_klass_external_name());
    ShouldNotReachHere();
  }

  int i;
  for (i=0; i<n; i++) {
    InstanceKlass* k = lookup_class_by_id(_interfaces->at(i));
    if (interface_name == k->name()) {
      return k;
    }
  }

  // interface_name is not specified by the "interfaces:" keyword.
  print_specified_interfaces();
  error("The interface %s implemented by class %s does not match any of the specified interface IDs",
        interface_name->as_klass_external_name(), _class_name);
  ShouldNotReachHere();
  return nullptr;
}

InstanceKlass* ClassListParser::find_builtin_class_helper(JavaThread* current, Symbol* class_name_symbol, oop class_loader_oop) {
  Handle class_loader(current, class_loader_oop);
  Handle protection_domain;
  return SystemDictionary::find_instance_klass(current, class_name_symbol, class_loader, protection_domain);
}

InstanceKlass* ClassListParser::find_builtin_class(JavaThread* current, const char* class_name) {
  TempNewSymbol class_name_symbol = SymbolTable::new_symbol(class_name);
  InstanceKlass* ik;

  if ( (ik = find_builtin_class_helper(current, class_name_symbol, nullptr)) != nullptr
    || (ik = find_builtin_class_helper(current, class_name_symbol, SystemDictionary::java_platform_loader())) != nullptr
    || (ik = find_builtin_class_helper(current, class_name_symbol, SystemDictionary::java_system_loader())) != nullptr) {
    return ik;
  } else {
    return nullptr;
  }
}

void ClassListParser::parse_constant_pool_tag() {
  if (_parse_mode == _parse_lambda_forms_invokers_only) {
    return;
  }

  JavaThread* THREAD = JavaThread::current();
  skip_whitespaces();
  char* class_name = _token;
  skip_non_whitespaces();
  *_token = '\0';
  _token ++;

  InstanceKlass* ik = find_builtin_class(THREAD, class_name);
  if (ik == nullptr) {
    _token = class_name;
    if (strstr(class_name, "/$Proxy") != nullptr ||
        strstr(class_name, "MethodHandle$Species_") != nullptr) {
      // ignore -- TODO: we should filter these out in classListWriter.cpp
    } else {
      constant_pool_resolution_warning("class %s is not (yet) loaded by one of the built-in loaders", class_name);
    }
    return;
  }

  ResourceMark rm(THREAD);
  constantPoolHandle cp(THREAD, ik->constants());
  GrowableArray<bool> preresolve_list(cp->length(), cp->length(), false);
  bool preresolve_class = false;
  bool preresolve_fmi = false;
  bool preresolve_indy = false;
  
  while (*_token) {
    int cp_index;
    skip_whitespaces();
    parse_uint(&cp_index);
    if (cp_index < 1 || cp_index >= cp->length()) {
      constant_pool_resolution_warning("Invalid constant pool index %d", cp_index);
      return;
    } else {
      preresolve_list.at_put(cp_index, true);
    }
    switch (cp->tag_at(cp_index).value()) {
    case JVM_CONSTANT_UnresolvedClass:
      preresolve_class = true;
      break;
    case JVM_CONSTANT_UnresolvedClassInError:
    case JVM_CONSTANT_Class:
      // ignore
    case JVM_CONSTANT_Fieldref:
    case JVM_CONSTANT_Methodref:
      preresolve_fmi = true;
      break;
    case JVM_CONSTANT_InvokeDynamic:
      preresolve_indy = true;
      break;
    default:
      constant_pool_resolution_warning("Unsupported constant pool index %d (type=%d)",
                                       cp_index, cp->tag_at(cp_index).value());
      return;
    }
  }

  if (preresolve_class) {
    ClassPrelinker::preresolve_class_cp_entries(THREAD, ik, &preresolve_list);
  }
  if (preresolve_fmi) {
    ClassPrelinker::preresolve_field_and_method_cp_entries(THREAD, ik, &preresolve_list);
  }
  if (preresolve_indy) {
    ClassPrelinker::preresolve_indy_cp_entries(THREAD, ik, &preresolve_list);
  }
}

void ClassListParser::parse_class_reflection_data_tag() {
  if (_parse_mode == _parse_lambda_forms_invokers_only) {
    return;
  }

  JavaThread* THREAD = JavaThread::current();
  skip_whitespaces();
  char* class_name = _token;
  skip_non_whitespaces();
  *_token = '\0';
  _token ++;

  InstanceKlass* ik = find_builtin_class(THREAD, class_name);
  if (ik == nullptr) {
    _token = class_name;
    if (strstr(class_name, "/$Proxy") != nullptr ||
        strstr(class_name, "MethodHandle$Species_") != nullptr) {
      // ignore -- TODO: we should filter these out in classListWriter.cpp
    } else {
      warning("@class-reflection-data: class not found: %s", class_name);
    }
    return;
  }

  ResourceMark rm(THREAD);

  int rd_flags = _unspecified;
  while (*_token) {
    skip_whitespaces();
    if (rd_flags != _unspecified) {
      error("rd_flags specified twice");
      return;
    }
    parse_uint(&rd_flags);
  }
  if (rd_flags == _unspecified) {
    error("no rd_flags specified");
    return;
  }

  if (ArchiveReflectionData) {
    log_info(cds)("Generate ReflectionData: %s (flags=" INT32_FORMAT_X ")", ik->external_name(), rd_flags);
    JavaCallArguments args(Handle(THREAD, ik->java_mirror()));
    args.push_int(rd_flags);
    JavaValue result(T_OBJECT);
    JavaCalls::call_special(&result,
                            vmClasses::Class_klass(),
                            vmSymbols::generateReflectionData_name(),
                            vmSymbols::int_void_signature(),
                            &args, THREAD);
    if (HAS_PENDING_EXCEPTION) {
      Handle exc_handle(THREAD, PENDING_EXCEPTION);
      CLEAR_PENDING_EXCEPTION;

      log_warning(cds)("Exception during Class::generateReflectionData() call for %s", ik->external_name());
      LogStreamHandle(Debug, cds) log;
      if (log.is_enabled()) {
        java_lang_Throwable::print_stack_trace(exc_handle, &log);
      }
    }
  }

}