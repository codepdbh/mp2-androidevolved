package com.mp2r.android;
import android.app.Activity;
import android.os.Bundle;
import android.widget.*;
import java.io.*;

public final class DiagnosticActivity extends Activity {
    TouchDiagnosticView controls;
    private Thread probe;
    private TextView storageStatus;
    private TextView resultStatus;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        LinearLayout root=new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff111315); root.setFitsSystemWindows(true);
        TextView heading=new TextView(this);
        heading.setText("MP2R · DIAGNÓSTICO — SIN GAMEPLAY"); heading.setTextColor(0xfff1eee6);
        heading.setTextSize(17); heading.setPadding(16,8,16,4); root.addView(heading);
        TextView result=new TextView(this); resultStatus=result; result.setText("Comprobando código original ARM64…");
        result.setTextColor(0xffb9b5ae); result.setPadding(16,0,16,0); root.addView(result);
        storageStatus=new TextView(this); storageStatus.setTextColor(0xffb9b5ae); storageStatus.setTextSize(11); storageStatus.setPadding(16,0,16,0); root.addView(storageStatus);
        LinearLayout tools=new LinearLayout(this); root.addView(tools);
        Button edit=new Button(this); edit.setText("Editar posiciones"); tools.addView(edit);
        Button access=new Button(this); access.setText("Acceso a datos"); tools.addView(access);
        access.setOnClickListener(v->GameStorage.requestAccess(this));
        controls=new TouchDiagnosticView(this); root.addView(controls,new LinearLayout.LayoutParams(-1,0,1));
        edit.setOnClickListener(v->{ controls.setEditing(!controls.editing); edit.setText(controls.editing?"Guardar posiciones":"Editar posiciones"); });
        setContentView(root);
    }
    private void startProbe() {
        if(probe!=null && probe.isAlive()) return;
        if(!GameStorage.hasAccess(this)) { resultStatus.setText("Habilita acceso a datos para probar la inicialización original"); return; }
        resultStatus.setText("Ejecutando inicialización original ARM64…");
        probe=new Thread(()->{
            String output;
            try { output=NativeBridge.runInitialization(GameStorage.loadVerifiedExecutable()); }
            catch(Exception error) { output="Datos no disponibles: "+error.getMessage(); }
            try(FileOutputStream file=openFileOutput("diagnostic.log",MODE_PRIVATE)) { file.write(output.getBytes(java.nio.charset.StandardCharsets.UTF_8)); }
            catch(IOException e) { output += "\nLog: "+e.getMessage(); }
            final String text=output.startsWith("PASS:")?"Hilo auxiliar ejecutado · Configuración de sistema pendiente":output;
            runOnUiThread(()->{ if(!isDestroyed()) resultStatus.setText(text); });
        },"MP2R-Bootstrap"); probe.start();
    }
    @Override protected void onResume() {
        super.onResume();
        startProbe();
        new Thread(()->{
            String status=GameStorage.inspect(this);
            runOnUiThread(()->{ if(!isDestroyed()) storageStatus.setText(status); });
        },"MP2R-DataCheck").start();
    }
    @Override public void onRequestPermissionsResult(int request,String[] permissions,int[] grants) {
        super.onRequestPermissionsResult(request,permissions,grants);
        if(request==20) startProbe();
        if(request==20) { new Thread(()->{ String status=GameStorage.inspect(this); runOnUiThread(()->{ if(!isDestroyed()) storageStatus.setText(status); }); }).start(); }
    }
    @Override protected void onPause() { if(controls!=null) controls.cancelAll(); super.onPause(); }
    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus); if(!focus && controls!=null) controls.cancelAll();
    }
}
