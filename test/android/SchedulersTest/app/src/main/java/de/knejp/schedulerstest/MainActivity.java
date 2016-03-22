package de.knejp.schedulerstest;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;

public class MainActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        SharedNativeThreadPoolTest.executor.execute(new Runnable() {
            @Override
            public void run() {
                Log.d("SCHEDULERS", "some text");
            }
        });

        try {
            SharedNativeThreadPoolTest.test();
        }
        catch(Throwable tr) {
            Log.e("x", "error", tr);
        }

        final Thread mainThread = Thread.currentThread();

        createMainLooperScheduler();
        Runnable r = new Runnable() {
            @Override
            public void run() {
                if(Thread.currentThread() != mainThread)
                {
                    Log.e("SCHEDULERS", "not on main thread");
                }
                else
                {
                    Log.i("SCHEDULERS", "on main thread");
                }
            }
        };
        executeOnMainLooperScheduler(r);
        executeOnMainLooperScheduler(r);
        executeOnMainLooperScheduler(r);
        executeOnMainLooperScheduler(r);
    }

    static {
        System.loadLibrary("schedulers-android-test");
    }

    private native void createMainLooperScheduler();
//    private native void destroyMainLooperScheduler();
    private native void executeOnMainLooperScheduler(Runnable r);
}
