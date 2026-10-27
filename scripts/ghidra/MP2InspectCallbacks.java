// @category MP2R
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
public class MP2InspectCallbacks extends GhidraScript {
    public void run() throws Exception {
        File directory=new File(getScriptArgs()[0]);
        try(PrintWriter out=new PrintWriter(new File(directory,"kernel-callbacks.txt"))) {
            for(long address=0x1166d8;address<0x116780;address+=4) {
                Instruction ins=currentProgram.getListing().getInstructionAt(toAddr(address));
                Data data=currentProgram.getListing().getDataContaining(toAddr(address));
                out.printf("%08X %08X %s %s%n",address,currentProgram.getMemory().getInt(toAddr(address)),
                    ins==null?"<no instruction>":ins.toString(),data==null?"":data.getDataType().getName());
            }
        }
    }
}
