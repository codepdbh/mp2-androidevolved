// @category MP2R
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class MP2ExportAlarmKernel extends GhidraScript {
    public void run() throws Exception {
        // Address-taken return trampoline proven by the original patch table.
        if(currentProgram.getMemory().getInt(toAddr(0x8007673cL))!=0x800766c0)
            throw new IllegalStateException("Alarm return trampoline differs");
        disassemble(toAddr(0x800766c0L));
        if(getFunctionAt(toAddr(0x800766c0L))==null && createFunction(toAddr(0x800766c0L),"mp2_alarm_return")==null)
            throw new IllegalStateException("Cannot recover alarm return trampoline");
        File directory=new File(getScriptArgs()[0]);
        try(PrintWriter out=new PrintWriter(new File(directory,"kernel-functions.csv"));
            PrintWriter listing=new PrintWriter(new File(directory,"kernel-listing.txt"))) {
            out.println("Name,Start,End,Size");
            FunctionIterator functions=currentProgram.getFunctionManager().getFunctions(true);
            int count=0;
            while(functions.hasNext()) {
                Function f=functions.next(); long start=f.getEntryPoint().getOffset();
                AddressSetView body=f.getBody();
                if(start<0x80076000L || start>=0x80076740L || body.getNumAddressRanges()!=1) continue;
                out.printf("mp2_%08X,0x%08X,0x%08X,%d%n",start,start,body.getMaxAddress().getOffset()+1,body.getNumAddresses()); count++;
            }
            InstructionIterator instructions=currentProgram.getListing().getInstructions(new AddressSet(toAddr(0x80076000L),toAddr(0x8007673fL)),true);
            while(instructions.hasNext()) { Instruction i=instructions.next(); listing.println(i.getAddress()+" "+i); }
            println("MP2R alarm kernel functions="+count);
        }
    }
}
