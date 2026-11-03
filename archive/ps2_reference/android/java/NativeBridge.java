package com.mp2r.android;
final class NativeBridge {
    static { System.loadLibrary("mp2r"); }
    static native String runProbe();
    static native String runInitialization(byte[] verifiedOriginalElf,String bootPath);
    static native String runGame(byte[] verifiedOriginalElf,String bootPath,String logPath,boolean profile);
    static native void setSurface(android.view.Surface surface);
    static native void setImageFlipped(boolean flipped);
    static native void setPaused(boolean paused);
    static native void stopGame();
    static native boolean prepareGame();
    static native void menuInput(int pressed);
    static native long frameCount();
    static native void cancel();
    static native void configureDataDirectory(String path);
    static native String inspectExecutable(String relativePath);
    static native float[] touchBatch(int[] ids,int[] types,int[] actions,float[] xs,float[] ys);
    static float[] snapshot() { return touchBatch(new int[0],new int[0],new int[0],new float[0],new float[0]); }
}
