package com.mp2r.android;
import android.app.*;
import android.content.Intent;
import android.os.*;
import android.view.MotionEvent;

public final class TouchInstrumentation extends Instrumentation {
    private boolean gameBenchmark;
    private static void check(boolean ok,String message) { if(!ok) throw new AssertionError(message); }
    private void event(TouchDiagnosticView view,int action,int[] ids,float[] x,float[] y) {
        MotionEvent.PointerProperties[] props=new MotionEvent.PointerProperties[ids.length];
        MotionEvent.PointerCoords[] coords=new MotionEvent.PointerCoords[ids.length];
        for(int i=0;i<ids.length;i++) {
            props[i]=new MotionEvent.PointerProperties(); props[i].id=ids[i]; props[i].toolType=MotionEvent.TOOL_TYPE_FINGER;
            coords[i]=new MotionEvent.PointerCoords(); coords[i].x=x[i]; coords[i].y=y[i]; coords[i].pressure=1; coords[i].size=1;
        }
        long now=SystemClock.uptimeMillis();
        MotionEvent e=MotionEvent.obtain(now,now,action,ids.length,props,coords,0,0,1,1,0,0,0,0);
        view.onTouchEvent(e); e.recycle();
    }
    @Override public void onCreate(Bundle args) { super.onCreate(args); gameBenchmark=args!=null && "game".equals(args.getString("mode")); start(); }
    @Override public void onStart() {
        if(gameBenchmark) { benchmarkGame(); return; }
        Bundle result=new Bundle(); DiagnosticActivity activity=null;
        try {
            Intent intent=new Intent(getTargetContext(),DiagnosticActivity.class); intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            activity=(DiagnosticActivity)startActivitySync(intent); final DiagnosticActivity current=activity;
            waitForIdleSync();
            String dataStatus=GameStorage.inspect(current);
            check(dataStatus.contains("ELF PAL verificado") && dataStatus.contains("lectura nativa OK"),"Root game-data Java/JNI read failed: "+dataStatus);
            check(GameStorage.bootPath().equals("cdrom0:\\SLES_522.56;1"),"Unexpected BOOT2 argument");
            String init=NativeBridge.runInitialization(GameStorage.loadVerifiedExecutable(),GameStorage.bootPath());
            check(init.startsWith("PASS:") && init.contains("semaphores=3") && init.contains("alarm_patch=1") && init.contains("pc=0x42f7e0") &&
                init.contains("tlb_services=1") && init.contains("main_entry=1") &&
                init.contains("threads=2") && init.contains("switches=2") && init.contains("waits=1"),"Original InitSys did not reach expected boundary: "+init);
            check(NativeBridge.runInitialization(new byte[52],GameStorage.bootPath()).startsWith("FAIL:"),"Truncated ELF accepted");
            runOnMainSync(()->{
                TouchDiagnosticView v=current.controls;
                check(GameStorage.directory().getName().equals("mp2androidevolved"),"Wrong game data folder");
                check(v.getWidth()>0 && v.getHeight()>0,"No diagnostic surface");
                float mx=v.controlX(0),my=v.controlY(0),fx=v.controlX(2),fy=v.controlY(2);
                event(v,MotionEvent.ACTION_DOWN,new int[]{7},new float[]{mx},new float[]{my});
                event(v,MotionEvent.ACTION_POINTER_DOWN|(1<<8),new int[]{7,21},new float[]{mx,fx},new float[]{my,fy});
                float[] s=NativeBridge.snapshot(); check(s[2]==1 && s[12]==1,"Move+fire not held together");
                event(v,MotionEvent.ACTION_MOVE,new int[]{7,21},new float[]{mx+v.radius(0)*.5f,fx},new float[]{my,fy});
                s=NativeBridge.snapshot(); check(s[0]>.45f && s[12]==1,"Move update released fire");
                event(v,MotionEvent.ACTION_POINTER_UP|(1<<8),new int[]{7,21},new float[]{mx,fx},new float[]{my,fy});
                s=NativeBridge.snapshot(); check(s[2]==1 && s[12]==0,"Pointer UP released wrong contact");
                event(v,MotionEvent.ACTION_CANCEL,new int[]{7},new float[]{mx},new float[]{my});
                s=NativeBridge.snapshot(); for(int a=0;a<12;a++) check(s[a*5+2]==0,"Cancel left held input");
                float cx=v.getWidth()*.45f,cy=v.getHeight()*.2f;
                event(v,MotionEvent.ACTION_DOWN,new int[]{3},new float[]{cx},new float[]{cy});
                s=NativeBridge.snapshot(); check(s[7]==1 && s[5]==0 && s[6]==0,"Camera jumps on touchdown");
                event(v,MotionEvent.ACTION_MOVE,new int[]{3},new float[]{cx+30},new float[]{cy});
                s=NativeBridge.snapshot(); check(s[5]>0,"Camera motion lost"); v.cancelAll();
                float oldX=v.controlX(4),oldY=v.controlY(4),newX=oldX-v.radius(4)*.5f;
                v.setEditing(true);
                event(v,MotionEvent.ACTION_DOWN,new int[]{9},new float[]{oldX},new float[]{oldY});
                event(v,MotionEvent.ACTION_MOVE,new int[]{9},new float[]{newX},new float[]{oldY});
                event(v,MotionEvent.ACTION_UP,new int[]{9},new float[]{newX},new float[]{oldY}); v.setEditing(false);
                TouchDiagnosticView restored=new TouchDiagnosticView(current); restored.layout(0,0,v.getWidth(),v.getHeight());
                check(Math.abs(restored.controlX(4)-newX)<2,"Edited layout did not persist");
                restored.layout(0,0,v.getHeight(),v.getWidth());
                check(restored.controlX(4)>=0 && restored.controlX(4)<=restored.getWidth(),"Layout outside new aspect");
                v.setEditing(true);
                event(v,MotionEvent.ACTION_DOWN,new int[]{9},new float[]{newX},new float[]{oldY});
                event(v,MotionEvent.ACTION_MOVE,new int[]{9},new float[]{oldX},new float[]{oldY});
                event(v,MotionEvent.ACTION_UP,new int[]{9},new float[]{oldX},new float[]{oldY}); v.setEditing(false);
                v.cancelAll();
            });
            result.putString("result","PASS: verified PAL InitSys loads from mp2androidevolved, InitSys completes (helper thread, OSD config, TLB services) and crt0 reaches main() with BOOT2 argv, truncated ELF refused; MotionEvent multitouch/editor/aspect regression passed");
            result.putBoolean("passed",true); finish(Activity.RESULT_OK,result);
        } catch(Throwable error) {
            result.putString("result","FAIL: "+error); result.putBoolean("passed",false); finish(Activity.RESULT_CANCELED,result);
        } finally { if(activity!=null) { DiagnosticActivity target=activity; runOnMainSync(target::finish); } }
    }
    private void benchmarkGame() {
        Bundle result=new Bundle(); Activity activity=null;
        int code=Activity.RESULT_CANCELED;
        try {
            Intent intent=new Intent(getTargetContext(),GameActivity.class); intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK); intent.putExtra("profile",true);
            activity=startActivitySync(intent); waitForIdleSync();
            SystemClock.sleep(5000);
            long start=NativeBridge.frameCount(),time=SystemClock.elapsedRealtime();
            SystemClock.sleep(15000);
            long elapsed=SystemClock.elapsedRealtime()-time,frames=NativeBridge.frameCount()-start;
            check(frames>0,"No presented frames during benchmark");
            NativeBridge.setPaused(true); SystemClock.sleep(300);
            long paused=NativeBridge.frameCount(); SystemClock.sleep(700);
            check(NativeBridge.frameCount()==paused,"Paused runtime continues to present frames");
            NativeBridge.setPaused(false); SystemClock.sleep(1500);
            check(NativeBridge.frameCount()>paused,"Runtime did not resume presentation");
            result.putString("result","PASS: frames="+frames+" elapsed_ms="+elapsed+" presented_fps="+(frames*1000.0/elapsed)+"; pause/resume passed");
            result.putBoolean("passed",true); code=Activity.RESULT_OK;
        } catch(Throwable error) {
            result.putString("result","FAIL: "+error); result.putBoolean("passed",false);
        } finally {
            NativeBridge.stopGame();
            if(activity!=null) { Activity target=activity; runOnMainSync(target::finish); }
            SystemClock.sleep(1500);
        }
        finish(code,result);
    }
}
