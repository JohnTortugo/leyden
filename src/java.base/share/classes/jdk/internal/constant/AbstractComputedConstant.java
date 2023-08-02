/*
 * Copyright (c) 2023, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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
 */

package jdk.internal.constant;

import jdk.internal.misc.Unsafe;
import jdk.internal.vm.annotation.ForceInline;
import jdk.internal.vm.annotation.Stable;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.util.NoSuchElementException;
import java.util.Objects;
import java.util.function.Supplier;

public abstract sealed class AbstractComputedConstant<V, P>
        implements ComputedConstant<V>
        permits ListElementComputedConstant,
        MethodHandleComputedConstant,
        StandardComputedConstant {

    // `Unsafe` is used rather than the supported API of `VarHandle` to allow
    // the use of `ComputedConstant` constructs early in the boot process.
    private static final long VALUE_OFFSET =
            Unsafe.getUnsafe().objectFieldOffset(AbstractComputedConstant.class, "value");

    private static final long AUX_OFFSET =
            Unsafe.getUnsafe().objectFieldOffset(AbstractComputedConstant.class, "auxiliary");

    /**
     * This field holds a bound lazy value.
     * If != null, a value is bound, otherwise the auxiliary field needs to be consulted.
     */
    @Stable
    private V value;

    /**
     * This non-final auxiliary field is used for:
     *   0) Holding the initial provider
     *   1) Flagging if the value is being bound (constructing)
     *   2) Flagging if the value was actually evaluated to null
     *   3) Flagging if the initial supplier threw an exception
     *   4) Flagging if a value is bound
     */
    private Object auxiliary;

    public AbstractComputedConstant(P provider) {
        this.auxiliary = provider;
    }

    @ForceInline
    @Override
    public final boolean isUnbound() {
        return providerType().isInstance(auxiliaryVolatile());
    }

    @ForceInline
    @Override
    public final boolean isBound() {
        // Try normal memory semantics first
        return value != null || auxiliaryVolatile() instanceof ConstantUtil.Bound;
    }

    @ForceInline
    @Override
    public final boolean isError() {
        return auxiliaryVolatile() instanceof ConstantUtil.BindError;
    }

    @ForceInline
    @Override
    public final V get() {
        // Try normal memory semantics first
        V v = value;
        if (v != null) {
            return v;
        }
        if (auxiliary instanceof ConstantUtil.Null) {
            return null;
        }
        @SuppressWarnings("unchecked")
        P provider = (P) auxiliary;
        return slowPath(provider, null, true);
    }

    @ForceInline
    @Override
    public final V orElse(V other) {
        // Try normal memory semantics first
        V v = value;
        if (v != null) {
            return v;
        }
        if (auxiliary instanceof ConstantUtil.Null) {
            return null;
        }
        @SuppressWarnings("unchecked")
        P provider = (P) auxiliary;
        return slowPath(provider, other, false);
    }

    @ForceInline
    @Override
    public final <X extends Throwable> V orElseThrow(Supplier<? extends X> exceptionSupplier) throws X {
        V v = orElse(null);
        if (v == null) {
            throw exceptionSupplier.get();
        }
        return v;
    }

    private synchronized V slowPath(P provider,
                                    V other,
                                    boolean rethrow) {

        // Under synchronization, visibility and atomicy is guaranteed for both
        // the fields "value" and "auxiliary" as they are only changed within this block.
        V v = value;
        if (v != null) {
            return v;
        }
        return switch (auxiliary) {
            case ConstantUtil.Null __ -> null;
            case ConstantUtil.Binding __ ->
                    throw new StackOverflowError("Circular provider detected: " + toStringDescription());
            case ConstantUtil.BindError __ ->
                    throw new NoSuchElementException("A previous provider threw an exception: "+toStringDescription());
            default -> bindValue(rethrow, other, provider);
        };
    }

    @SuppressWarnings("unchecked")
    private V bindValue(boolean rethrow, V other, P provider) {
        setAuxiliaryVolatile(ConstantUtil.BINDING_SENTINEL);
        try {
            final V v;
            if (provider instanceof Supplier<?> supplier) {
                // Regardless of ComputedConstant implementation variant, computeIfUnbound might be called.
                v = (V) supplier.get();
            } else {
                v = evaluate(provider);
            }
            if (v == null) {
                setAuxiliaryVolatile(ConstantUtil.NULL_SENTINEL);
            } else {
                casValue(v);
                setAuxiliaryVolatile(ConstantUtil.NON_NULL_SENTINEL);
            }
            return v;
        } catch (Throwable e) {
            setAuxiliaryVolatile(ConstantUtil.BIND_ERROR_SENTINEL);
            if (e instanceof Error err) {
                // Always rethrow errors
                throw err;
            }
            if (rethrow) {
                throw new NoSuchElementException(e);
            }
            return other;
        }
    }

    abstract V evaluate(P provider);

    abstract Class<?> providerType();

    abstract String toStringDescription();

    @Override
    public final String toString() {
        var a = auxiliaryVolatile();
        String v = switch (a) {
            case ConstantUtil.Binding __ -> ".binding";
            case ConstantUtil.Null __ -> "null";
            case ConstantUtil.NonNull __ -> "[" + valueVolatile().toString() + "]";
            case ConstantUtil.BindError __ -> ".error";
            default -> {
                if (providerType().isInstance(a)) {
                    yield ".unbound";
                }
                yield ".INTERNAL_ERROR";
            }
        };
        return toStringDescription() + v;
    }

    @SuppressWarnings("unchecked")
    private V valueVolatile() {
        return (V) Unsafe.getUnsafe().getReferenceVolatile(this, VALUE_OFFSET);
    }

    private Object auxiliaryVolatile() {
        return Unsafe.getUnsafe().getReferenceVolatile(this, AUX_OFFSET);
    }

    private void casValue(Object o) {
        if (!Unsafe.getUnsafe().compareAndSetReference(this, VALUE_OFFSET, null, o)) {
            throw new InternalError();
        }
    }

    private void setAuxiliaryVolatile(Object o) {
        Unsafe.getUnsafe().putReferenceVolatile(this, AUX_OFFSET, o);
        //AUX_HANDLE.setVolatile(this, o);
    }

}
