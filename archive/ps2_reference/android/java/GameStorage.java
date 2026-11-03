package com.mp2r.android;
import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.Settings;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.Locale;
import java.util.regex.*;

final class GameStorage {
    static final String FOLDER="mp2androidevolved";
    static File directory() { return new File(Environment.getExternalStorageDirectory(),FOLDER); }
    static boolean hasAccess(Activity activity) {
        if(Build.VERSION.SDK_INT>=30) return Environment.isExternalStorageManager();
        return activity.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE)==PackageManager.PERMISSION_GRANTED;
    }
    static void requestAccess(Activity activity) {
        if(Build.VERSION.SDK_INT>=30) {
            Intent settings=new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,Uri.parse("package:"+activity.getPackageName()));
            if(settings.resolveActivity(activity.getPackageManager())!=null) activity.startActivity(settings);
            else activity.startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
        } else activity.requestPermissions(new String[]{Manifest.permission.READ_EXTERNAL_STORAGE,Manifest.permission.WRITE_EXTERNAL_STORAGE},20);
    }
    /** Argument the PS2 loader passes as argv[0] for a disc boot: the full SYSTEM.CNF BOOT2 value. */
    static String bootPath() throws IOException {
        return "cdrom0:\\"+bootEntry().group(1).trim()+";1";
    }
    static File executableFile() throws IOException {
        File root=directory();
        File elf=new File(root,bootEntry().group(1).replace('\\','/').trim()).getCanonicalFile();
        if(!elf.toPath().startsWith(root.getCanonicalFile().toPath())) throw new IOException("Ruta BOOT2 fuera de la carpeta del juego");
        return elf;
    }
    private static Matcher bootEntry() throws IOException {
        File root=directory(),cnf=new File(root,"SYSTEM.CNF");
        if(!cnf.isFile()) throw new IOException("Falta SYSTEM.CNF en "+root);
        byte[] content;
        try(FileInputStream input=new FileInputStream(cnf); ByteArrayOutputStream output=new ByteArrayOutputStream()) {
            byte[] buffer=new byte[2048]; int size;
            while((size=input.read(buffer))!=-1) {
                if(output.size()+size>16384) throw new IOException("SYSTEM.CNF excede el tamaño permitido");
                output.write(buffer,0,size);
            }
            content=output.toByteArray();
        }
        Matcher boot=Pattern.compile("BOOT2\\s*=\\s*cdrom0:\\\\([^;\\r\\n]+)",Pattern.CASE_INSENSITIVE).matcher(new String(content,StandardCharsets.US_ASCII));
        if(!boot.find()) throw new IOException("SYSTEM.CNF no contiene BOOT2");
        return boot;
    }
    static byte[] loadVerifiedExecutable() throws Exception {
        File elf=executableFile();
        if(elf.length()!=4984624) throw new IOException("Tamaño de ELF distinto del PAL verificado");
        byte[] content=new byte[4984624];
        try(DataInputStream input=new DataInputStream(new FileInputStream(elf))) {
            input.readFully(content); if(input.read()!=-1) throw new IOException("El ELF cambió durante la lectura");
        }
        byte[] digest=MessageDigest.getInstance("SHA-256").digest(content);
        StringBuilder hash=new StringBuilder(); for(byte value:digest) hash.append(String.format(Locale.ROOT,"%02x",value&255));
        if(!hash.toString().equals("9dac1fec5eb742e123cee20bf141bf7c05e43d1fcf6de9b69750a4e7720bedba")) throw new IOException("Ejecutable distinto del PAL verificado");
        return content;
    }
    static String inspect(Activity activity) {
        File root=directory();
        NativeBridge.configureDataDirectory(root.getAbsolutePath());
        if(!hasAccess(activity)) return root.getAbsolutePath()+" · habilita acceso a los archivos";
        try {
            if(!root.isDirectory() && !root.mkdirs()) throw new IOException("No se pudo crear la carpeta");
            loadVerifiedExecutable();
            String relative=root.getCanonicalFile().toPath().relativize(executableFile().toPath()).toString();
            return root.getAbsolutePath()+" · ELF PAL verificado · "+NativeBridge.inspectExecutable(relative);
        } catch(Exception error) { return root.getAbsolutePath()+" · "+error.getMessage(); }
    }
}
