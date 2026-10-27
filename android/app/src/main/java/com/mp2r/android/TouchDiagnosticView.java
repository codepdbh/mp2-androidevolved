package com.mp2r.android;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.*;
import android.util.SparseIntArray;
import android.view.MotionEvent;
import android.view.View;
import java.util.Arrays;

final class TouchDiagnosticView extends View {
    private static final String[] NAMES={"MOVER","CÁMARA","DISPARO","APUNTAR","RECARGA","SALTO","ACCIÓN","BULLET TIME","SHOOTDODGE","ARMAS","ANALGÉSICO","PAUSA"};
    private final float[][] positions={{.14f,.66f},{.54f,.35f},{.89f,.46f},{.75f,.31f},{.89f,.74f},{.29f,.78f},{.70f,.67f},{.54f,.80f},{.73f,.9f},{.88f,.13f},{.29f,.36f},{.06f,.12f}};
    private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Bitmap[] icons=new Bitmap[12];
    private final Rect[] iconBounds=new Rect[12];
    private static final int[] ICONS={0,0,R.drawable.hud_fire,R.drawable.hud_aim,R.drawable.hud_reload,
        R.drawable.hud_jump,R.drawable.hud_interact,R.drawable.hud_bullet_time,R.drawable.hud_shootdodge,
        R.drawable.hud_weapons,R.drawable.hud_painkiller,R.drawable.hud_pause};
    private final SparseIntArray contacts=new SparseIntArray();
    private final SparseIntArray editContacts=new SparseIntArray();
    private final SharedPreferences prefs;
    private final float density;
    boolean editing;
    private float cameraOriginX,cameraOriginY;
    float[] state=new float[60];
    TouchDiagnosticView(Context c) {
        super(c); density=getResources().getDisplayMetrics().density;
        prefs=c.getSharedPreferences("mp2r_touch_layout_v1",Context.MODE_PRIVATE);
        for(int a=0;a<12;a++) for(int axis=0;axis<2;axis++)
            positions[a][axis]=prefs.getFloat(a+":"+axis,positions[a][axis]);
        BitmapFactory.Options options=new BitmapFactory.Options(); options.inScaled=false; options.inSampleSize=4;
        for(int a=2;a<12;a++) {
            icons[a]=BitmapFactory.decodeResource(getResources(),ICONS[a],options);
            Bitmap icon=icons[a]; int w=icon.getWidth(),h=icon.getHeight();
            int[] pixels=new int[w*h]; icon.getPixels(pixels,0,w,0,0,w,h);
            int left=w,top=h,right=0,bottom=0;
            for(int y=0;y<h;y++) for(int x=0;x<w;x++) if((pixels[y*w+x]>>>24)>16) {
                left=Math.min(left,x); top=Math.min(top,y); right=Math.max(right,x+1); bottom=Math.max(bottom,y+1);
            }
            iconBounds[a]=new Rect(left,top,right,bottom);
        }
        paint.setFilterBitmap(true);
        setContentDescription("Diagnóstico multitáctil MP2R");
    }
    float radius(int action) { return (action==0?39:action==2?31:26)*density; }
    float controlX(int a) { float r=radius(a); return Math.max(r,Math.min(getWidth()-r,positions[a][0]*getWidth())); }
    float controlY(int a) { float r=radius(a); return Math.max(r,Math.min(getHeight()-r,positions[a][1]*getHeight())); }
    void setEditing(boolean value) { cancelAll(); editing=value; if(!value) save(); invalidate(); }
    private void save() {
        SharedPreferences.Editor e=prefs.edit();
        for(int a=0;a<12;a++) for(int axis=0;axis<2;axis++) e.putFloat(a+":"+axis,positions[a][axis]);
        e.apply();
    }
    void cancelAll() { contacts.clear(); editContacts.clear(); NativeBridge.cancel(); state=NativeBridge.snapshot(); invalidate(); }
    @Override protected void onDetachedFromWindow() { cancelAll(); super.onDetachedFromWindow(); }
    @Override protected void onSizeChanged(int w,int h,int oldw,int oldh) { cancelAll(); }
    @Override protected void onDraw(Canvas canvas) {
        canvas.drawColor(0xff171a1d);
        paint.setTextAlign(Paint.Align.CENTER); paint.setTextSize(13*density); paint.setColor(0xff858888);
        canvas.drawText(editing?"ARRASTRA LOS CONTROLES · posiciones locales":"ENTRADA SEMÁNTICA · los botones aún no controlan el juego",getWidth()*.5f,18*density,paint);
        for(int a=0;a<12;a++) {
            if(a==1) continue;
            float x=controlX(a),y=controlY(a),r=radius(a);
            paint.setStyle(Paint.Style.FILL); paint.setColor(state[a*5+2]>0?0x998a6b3a:0x44252a2e);
            canvas.drawCircle(x,y,r,paint);
            paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(density); paint.setColor(0xffaaa18f); canvas.drawCircle(x,y,r,paint);
            paint.setStyle(Paint.Style.FILL);
            if(a==0) {
                paint.setColor(0xffc5c2b9);
                canvas.drawCircle(x+state[0]*r*.55f,y+state[1]*r*.55f,r*.36f,paint);
            } else {
                Rect source=iconBounds[a];
                float scale=1.42f*r/Math.max(source.width(),source.height());
                float hw=source.width()*scale*.5f,hh=source.height()*scale*.5f;
                paint.setAlpha(state[a*5+2]>0?255:210);
                canvas.drawBitmap(icons[a],source,new RectF(x-hw,y-hh,x+hw,y+hh),paint);
                paint.setAlpha(255);
            }
            if(editing) {
                paint.setColor(0xffe1ddd3); paint.setTextSize(8*density);
                canvas.drawText(NAMES[a],x,Math.min(getHeight()-3*density,y+r+10*density),paint);
            }
        }
        paint.setColor(0xffc7c0b2); paint.setTextSize(11*density);
        canvas.drawText("CÁMARA · desliza en la zona libre derecha",getWidth()*.56f,getHeight()*.40f,paint);
        canvas.drawText(String.format(java.util.Locale.ROOT,"MOVE %.2f %.2f  ·  LOOK %.2f %.2f",state[0],state[1],state[5],state[6]),getWidth()*.5f,getHeight()*.53f,paint);
    }
    private int hit(float x,float y) {
        for(int a=11;a>=0;a--) if(a!=1 && Math.hypot(x-controlX(a),y-controlY(a))<=radius(a)*1.1) return a;
        return -1;
    }
    @Override public boolean onTouchEvent(MotionEvent event) {
        int type=event.getActionMasked(),index=event.getActionIndex();
        if(type==MotionEvent.ACTION_CANCEL) { cancelAll(); return true; }
        if(editing) {
            if(type==MotionEvent.ACTION_DOWN || type==MotionEvent.ACTION_POINTER_DOWN) {
                int action=hit(event.getX(index),event.getY(index));
                if(action>=0) editContacts.put(event.getPointerId(index),action);
            } else if(type==MotionEvent.ACTION_MOVE) {
                for(int i=0;i<event.getPointerCount();i++) {
                    int action=editContacts.get(event.getPointerId(i),-1);
                    if(action>=0) { positions[action][0]=Math.max(0,Math.min(1,event.getX(i)/getWidth())); positions[action][1]=Math.max(0,Math.min(1,event.getY(i)/getHeight())); }
                }
            } else if(type==MotionEvent.ACTION_UP || type==MotionEvent.ACTION_POINTER_UP) { editContacts.delete(event.getPointerId(index)); save(); }
            invalidate(); return true;
        }
        int count=event.getPointerCount(),used=0;
        int[] ids=new int[count],types=new int[count],actions=new int[count];
        float[] xs=new float[count],ys=new float[count];
        for(int i=0;i<count;i++) {
            if(type!=MotionEvent.ACTION_MOVE && i!=index) continue;
            int id=event.getPointerId(i),action=contacts.get(id,-1),nativeType=1;
            if(type==MotionEvent.ACTION_DOWN || type==MotionEvent.ACTION_POINTER_DOWN) {
                action=hit(event.getX(i),event.getY(i));
                if(action<0 && event.getX(i)>getWidth()*.4f) action=1;
                if(action<0 || ((action==0 || action==1) && contacts.indexOfValue(action)>=0)) continue;
                contacts.put(id,action); nativeType=0;
                if(action==1) { cameraOriginX=event.getX(i); cameraOriginY=event.getY(i); }
            } else if(type==MotionEvent.ACTION_UP || type==MotionEvent.ACTION_POINTER_UP) { nativeType=2; contacts.delete(id); }
            if(action<0) continue;
            float x=0,y=0;
            if(action<=1) {
                float scale=action==0?radius(0):getHeight()*.32f;
                x=(event.getX(i)-(action==0?controlX(0):cameraOriginX))/scale;
                y=(event.getY(i)-(action==0?controlY(0):cameraOriginY))/scale;
                float length=(float)Math.hypot(x,y); if(length>1) { x/=length; y/=length; }
            }
            ids[used]=id; types[used]=nativeType; actions[used]=action; xs[used]=x; ys[used]=y; used++;
        }
        state=NativeBridge.touchBatch(Arrays.copyOf(ids,used),Arrays.copyOf(types,used),Arrays.copyOf(actions,used),Arrays.copyOf(xs,used),Arrays.copyOf(ys,used));
        invalidate(); return true;
    }
}
