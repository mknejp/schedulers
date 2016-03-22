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

package de.knejp.schedulerstest;

import java.lang.Thread;
import java.util.concurrent.CountDownLatch;
import de.knejp.schedulers.SharedNativeThreadPoolExecutor;

public class SharedNativeThreadPoolTest
{
    public static final SharedNativeThreadPoolExecutor executor = createSharedNativePool();

	public static void test() throws Throwable
	{
		mainThread = Thread.currentThread();
		classLoader = mainThread.getContextClassLoader();
		latch = new CountDownLatch(3);

		testExecuteJavaRunnable();
		testWorkerClassLoader();
		testWorkerIsOtherThread();

		if(!latch.await(10, java.util.concurrent.TimeUnit.SECONDS))
		{
			System.out.println("Tests timed out.");
			System.exit(1);
		}
		executor.shutdown();
	}

	private static ClassLoader classLoader;
	private static Thread mainThread;
	private static CountDownLatch latch;

	private static native SharedNativeThreadPoolExecutor createSharedNativePool();

	private static void testExecuteJavaRunnable()
	{
		executor.execute(new Runnable()
		{
			@Override
			public void run()
			{
				latch.countDown();
			}
		});
	}

	private static void testWorkerClassLoader()
	{
		executor.execute(new Runnable()
		{
			@Override
			public void run()
			{
				if(Thread.currentThread().getContextClassLoader() != classLoader)
				{
					System.out.println("Wrong class loader in worker thread.");
					System.exit(1);
				}
				latch.countDown();
			}
		});
	}

	private static void testWorkerIsOtherThread()
	{
		executor.execute(new Runnable()
		{
			@Override
			public void run()
			{
				if(Thread.currentThread() == mainThread)
				{
					System.out.println("Task in wrong thread.");
					System.exit(1);
				}
				latch.countDown();
			}
		});
	}
}
