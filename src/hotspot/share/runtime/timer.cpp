/*
 * Copyright (c) 1997, 2023, Oracle and/or its affiliates. All rights reserved.
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
#include "logging/log.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/os.hpp"
#include "runtime/thread.hpp"
#include "runtime/timer.hpp"
#include "utilities/ostream.hpp"

double TimeHelper::counter_to_seconds(jlong counter) {
  double freq  = (double) os::elapsed_frequency();
  return (double)counter / freq;
}

double TimeHelper::counter_to_millis(jlong counter) {
  return counter_to_seconds(counter) * 1000.0;
}

jlong TimeHelper::millis_to_counter(jlong millis) {
  jlong freq = os::elapsed_frequency() / MILLIUNITS;
  return millis * freq;
}

jlong TimeHelper::micros_to_counter(jlong micros) {
  jlong freq = os::elapsed_frequency() / MICROUNITS;
  return micros * freq;
}

void BaseTimer::add(BaseTimer* t) {
  _counter += t->_counter;
}

void BaseTimer::add_nanoseconds(jlong ns) {
  jlong freq = os::elapsed_frequency() / NANOUNITS;
  _counter += ns * freq;
}

void BaseTimer::start() {
  if (!_active) {
    _active = true;
    _start_counter = read_counter();
  }
}

void BaseTimer::stop() {
  if (_active) {
    _counter += read_counter() - _start_counter;
    _active = false;
  }
}

double BaseTimer::seconds() const {
 return TimeHelper::counter_to_seconds(_counter);
}

jlong BaseTimer::milliseconds() const {
  return (jlong)TimeHelper::counter_to_millis(_counter);
}

jlong BaseTimer::active_ticks() const {
  if (!_active) {
    return ticks();
  }
  jlong counter = _counter + read_counter() - _start_counter;
  return counter;
}

jlong elapsedTimer::read_counter() const {
  return os::elapsed_counter();
}

ThreadTimer::ThreadTimer() {
  _owner = Thread::current();
}

void ThreadTimer::start() {
  assert(_owner != nullptr, "timer must be bound to a thread");
  Thread* current = Thread::current();
  assert(current == _owner, "timer can only be started by the thread that owns it");
  BaseTimer::start();
}

void ThreadTimer::stop() {
  assert(_owner != nullptr, "sanity check");
  Thread* current = Thread::current();
  assert(current == _owner, "timer can only be stopped by the thread that owns it");
  if (_start_counter != -1) {
    BaseTimer::stop();
  }
}

jlong ThreadTimer::read_counter() const {
  assert(_owner != nullptr, "sanity check");
  return os::thread_cpu_time(_owner);
}

void TimeStamp::update_to(jlong ticks) {
  _counter = ticks;
  if (_counter == 0)  _counter = 1;
  assert(is_updated(), "must not look clear");
}

void TimeStamp::update() {
  update_to(os::elapsed_counter());
}

double TimeStamp::seconds() const {
  assert(is_updated(), "must not be clear");
  jlong new_count = os::elapsed_counter();
  return TimeHelper::counter_to_seconds(new_count - _counter);
}

jlong TimeStamp::milliseconds() const {
  assert(is_updated(), "must not be clear");
  jlong new_count = os::elapsed_counter();
  return (jlong)TimeHelper::counter_to_millis(new_count - _counter);
}

jlong TimeStamp::ticks_since_update() const {
  assert(is_updated(), "must not be clear");
  return os::elapsed_counter() - _counter;
}
