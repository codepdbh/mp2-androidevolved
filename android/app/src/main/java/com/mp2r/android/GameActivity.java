package com.mp2r.android;

import android.app.Activity;
import android.os.Bundle;
import android.view.*;
import android.widget.*;
import java.io.File;

public final class GameActivity extends Activity implements SurfaceHolder.Callback {
    private TouchDiagnosticView controls;
    private TextView status;
    private Thread game;
    private boolean resumed,ready;
    private int buttons;
    private View menuTouch;
    private boolean hudVisible,menuMoved,menuHeld,menuMulti,menuLong,flipImage;
    private float menuX,menuY;
    private final android.os.Handler ui=new android.os.Handler(android.os.Looper.getMainLooper());
    private int pulseGeneration;
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        FrameLayout root=new FrameLayout(this); root.setBackgroundColor(0xff000000);
        SurfaceView surface=new SurfaceView(this); surface.getHolder().addCallback(this);
        FrameLayout image=new FrameLayout(this);
        FrameLayout.LayoutParams aspect=new FrameLayout.LayoutParams(-1,-1,Gravity.CENTER);
        root.addView(image,aspect); image.addView(surface,new FrameLayout.LayoutParams(-1,-1));
        root.addOnLayoutChangeListener((v,l,t,r,b,ol,ot,or,ob)->{
            int width=r-l,height=b-t,w=Math.min(width,height*4/3),h=w*3/4;
            if(image.getLayoutParams().width!=w || image.getLayoutParams().height!=h) {
                FrameLayout.LayoutParams p=new FrameLayout.LayoutParams(w,h,Gravity.CENTER); image.setLayoutParams(p);
            }
        });
        menuTouch=new View(this); menuTouch.setOnTouchListener((v,e)->menuTouch(e));
        root.addView(menuTouch,new FrameLayout.LayoutParams(-1,-1));
        controls=new TouchDiagnosticView(this); controls.gameMode=true; controls.setVisibility(View.GONE);
        root.addView(controls,new FrameLayout.LayoutParams(-1,-1));
        Button options=new Button(this); options.setText("⋮"); options.setTextSize(21);
        int size=(int)(48*getResources().getDisplayMetrics().density);
        root.addView(options,new FrameLayout.LayoutParams(size,size,Gravity.TOP|Gravity.RIGHT));
        options.setOnClickListener(v->{
            PopupMenu popup=new PopupMenu(this,options);
            popup.getMenu().add(hudVisible?"Ocultar controles":"Controles de juego").setOnMenuItemClickListener(item->{ showControls(!hudVisible); return true; });
            popup.getMenu().add("Invertir imagen verticalmente").setOnMenuItemClickListener(item->{
                flipImage=!flipImage;
                NativeBridge.setImageFlipped(flipImage);
                return true;
            });
            if(hudVisible) popup.getMenu().add(controls.editing?"Guardar posiciones":"Editar posiciones").setOnMenuItemClickListener(item->{ controls.setEditing(!controls.editing); return true; });
            popup.getMenu().add("Pausa / continuar").setOnMenuItemClickListener(item->{ showControls(false); pulse(8); return true; });
            popup.getMenu().add("Salir").setOnMenuItemClickListener(item->{ finish(); return true; });
            popup.show();
        });
        status=new TextView(this); status.setTextColor(0xffdddddd); status.setTextSize(12);
        status.setBackgroundColor(0xaa000000); status.setText("Preparando…");
        root.addView(status,new FrameLayout.LayoutParams(-2,-2,Gravity.BOTTOM|Gravity.CENTER_HORIZONTAL));
        setContentView(root);
        Toast.makeText(this,"Menús: toca para confirmar, desliza para elegir; dos dedos para volver.",Toast.LENGTH_LONG).show();
    }
    private void showControls(boolean visible) {
        controls.cancelAll(); buttons=0; NativeBridge.menuInput(0); hudVisible=visible;
        controls.setVisibility(visible?View.VISIBLE:View.GONE); menuTouch.setVisibility(visible?View.GONE:View.VISIBLE);
    }
    private void pulse(int bit) {
        int generation=++pulseGeneration; NativeBridge.menuInput(bit);
        ui.postDelayed(()->{ if(generation==pulseGeneration) NativeBridge.menuInput(0); },220);
    }
    private boolean menuTouch(MotionEvent e) {
        int action=e.getActionMasked();
        if(action==MotionEvent.ACTION_DOWN) {
            menuX=e.getX(); menuY=e.getY(); menuMoved=menuMulti=menuLong=false; menuHeld=true;
            ui.postDelayed(()->{ if(menuHeld && !menuMoved && !menuMulti) { menuLong=true; pulse(8); } },600);
        } else if(action==MotionEvent.ACTION_POINTER_DOWN) { menuMulti=true; pulse(0x2000); }
        else if(action==MotionEvent.ACTION_MOVE && !menuMoved && !menuMulti) {
            float x=e.getX()-menuX,y=e.getY()-menuY,threshold=48*getResources().getDisplayMetrics().density;
            if(Math.hypot(x,y)>threshold) { menuMoved=true; pulse(Math.abs(x)>Math.abs(y)?(x>0?0x20:0x80):(y>0?0x40:0x10)); }
        } else if(action==MotionEvent.ACTION_UP) {
            menuHeld=false; if(!menuMoved && !menuMulti && !menuLong) pulse(0x4000);
        } else if(action==MotionEvent.ACTION_CANCEL) { menuHeld=false; NativeBridge.menuInput(0); }
        return true;
    }
    private void startGame() {
        if(!resumed || !ready || game!=null) return;
        if(!GameStorage.hasAccess(this)) { status.setText("Habilita el acceso a mp2androidevolved"); GameStorage.requestAccess(this); return; }
        if(!NativeBridge.prepareGame()) { status.postDelayed(this::startGame,250); return; }
        game=new Thread(()->{
            String result;
            try {
                NativeBridge.configureDataDirectory(GameStorage.directory().getAbsolutePath());
                result=NativeBridge.runGame(GameStorage.loadVerifiedExecutable(),GameStorage.bootPath(),new File(getFilesDir(),"game.log").getAbsolutePath(),getIntent().getBooleanExtra("profile",false));
            } catch(Exception e) { result=e.getMessage(); }
            final String message=result;
            runOnUiThread(()->{ if(!isDestroyed()) { status.setVisibility(View.VISIBLE); status.setText("Ejecución detenida: "+message); } });
        },"MP2R-Game");
        status.setText("Cargando Max Payne 2…"); game.start();
        status.postDelayed(new Runnable(){ public void run(){
            if(isDestroyed() || game==null || !game.isAlive()) return;
            long frames=NativeBridge.frameCount();
            if(frames>0) status.setVisibility(View.GONE);
            status.postDelayed(this,1000);
        }},1000);
    }
    @Override protected void onResume(){ super.onResume(); resumed=true; NativeBridge.setPaused(false); startGame(); }
    @Override protected void onPause(){ resumed=false; menuHeld=false; ++pulseGeneration; buttons=0; NativeBridge.menuInput(0); controls.cancelAll(); NativeBridge.setPaused(true); super.onPause(); }
    @Override protected void onDestroy(){ NativeBridge.stopGame(); super.onDestroy(); }
    @Override public void onWindowFocusChanged(boolean focus){
        super.onWindowFocusChanged(focus);
        if(!focus && controls!=null) { menuHeld=false; ++pulseGeneration; buttons=0; NativeBridge.menuInput(0); controls.cancelAll(); }
        if(focus) {
            if(android.os.Build.VERSION.SDK_INT>=30) {
                getWindow().getInsetsController().hide(WindowInsets.Type.systemBars());
                getWindow().getInsetsController().setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            } else getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN|View.SYSTEM_UI_FLAG_HIDE_NAVIGATION|View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }
    @Override public void onBackPressed(){ if(hudVisible) { showControls(false); pulse(8); } else pulse(0x2000); }
    @Override public boolean onKeyDown(int key,KeyEvent event){
        if(key==KeyEvent.KEYCODE_ESCAPE) { finish(); return true; }
        return super.onKeyDown(key,event);
    }
    @Override public void surfaceCreated(SurfaceHolder holder){ NativeBridge.setSurface(holder.getSurface()); ready=true; startGame(); }
    @Override public void surfaceChanged(SurfaceHolder holder,int format,int width,int height){}
    @Override public void surfaceDestroyed(SurfaceHolder holder){ ready=false; NativeBridge.setSurface(null); }
}
