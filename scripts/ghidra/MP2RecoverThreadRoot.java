// Address passed to SetupThread by the verified PAL entry sequence.
// @category MP2R
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
public class MP2RecoverThreadRoot extends GhidraScript {
    public void run() throws Exception {
        var entry=toAddr(0x100220);
        if(currentProgram.getMemory().getInt(entry)!=0x24030023 || currentProgram.getMemory().getInt(entry.add(4))!=0x0000000c)
            throw new IllegalStateException("PAL ExitThread root differs");
        disassemble(entry);
        Function f=getFunctionAt(entry);
        if(f==null) f=currentProgram.getFunctionManager().createFunction("mp2_thread_root",entry,
            new AddressSet(entry,entry.add(7)),SourceType.USER_DEFINED);
        f.setNoReturn(true);
        println("MP2R original thread root: "+f.getBody());
        runScript("MP2ExportEvidence.java",new String[]{getScriptArgs()[0]});
    }
}
