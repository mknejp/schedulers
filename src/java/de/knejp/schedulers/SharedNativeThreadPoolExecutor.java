// Copyright (c) 2016, Miroslav Knejp
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

package de.knejp.schedulers;

import java.lang.Runnable;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.Executor;

public class SharedNativeThreadPoolExecutor implements Executor
{
    private final long nativeRef;
    private final AtomicBoolean destroyed = new AtomicBoolean(false);

    private SharedNativeThreadPoolExecutor(long nativeRef)
    {
        if (nativeRef == 0) throw new RuntimeException("nativeRef is zero");
        this.nativeRef = nativeRef;
    }

    private native void nativeShutdown(long nativeRef);
    public void shutdown()
    {
        boolean destroyed = this.destroyed.getAndSet(true);
        if (!destroyed) nativeShutdown(this.nativeRef);
    }

    @Override
    protected void finalize() throws Throwable
    {
        shutdown();
        super.finalize();
    }

    @Override
    public void execute(Runnable r)
    {
        assert !this.destroyed.get() : "trying to use a destroyed object";
        native_execute(this.nativeRef, r);
    }
    private native void native_execute(long _nativeRef, Runnable r);
}
