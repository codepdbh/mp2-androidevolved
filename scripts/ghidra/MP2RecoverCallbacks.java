// Recover address-taken code proven by the PAL ELF's own syscall patch table.
// @category MP2R
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.*;
public class MP2RecoverCallbacks extends GhidraScript {
    public void run() throws Exception {
        if(currentProgram.getMemory().getInt(toAddr(0x54cec0))!=0x83 ||
           currentProgram.getMemory().getInt(toAddr(0x54cec4))!=0x116730 ||
           currentProgram.getMemory().getInt(toAddr(0x54cec8))!=0x5a ||
           currentProgram.getMemory().getInt(toAddr(0x54cecc))!=0x1166f8)
            throw new IllegalStateException("PAL callback table differs");
        try(PrintWriter out=new PrintWriter(new File(getScriptArgs()[0],"recovered-callbacks.txt"))) {
            if(currentProgram.getMemory().getInt(toAddr(0x54de08))!=0x5a || currentProgram.getMemory().getInt(toAddr(0x54de0c))!=0x116d60)
                throw new IllegalStateException("PAL alarm-copy callback differs");
            if(currentProgram.getMemory().getInt(toAddr(0x54cbe8))!=0x5a || currentProgram.getMemory().getInt(toAddr(0x54cbec))!=0x115e70 ||
               currentProgram.getMemory().getInt(toAddr(0x54cbf8))!=0x54 || currentProgram.getMemory().getInt(toAddr(0x54cbfc))!=0x116400)
                throw new IllegalStateException("PAL TLB callback table differs");
            for(long entry:new long[]{0x1166f8,0x116730,0x116d60,0x115e70,0x116400}) {
                disassemble(toAddr(entry));
                Function f=getFunctionAt(toAddr(entry));
                if(f==null) f=createFunction(toAddr(entry),String.format("mp2_callback_%08X",entry));
                if(f==null) throw new IllegalStateException("Could not recover callback");
                out.printf("entry=0x%08X body=%s%n",entry,f.getBody());
            }
        }
        runScript("MP2ExportEvidence.java",new String[]{getScriptArgs()[0]});
    }
}
