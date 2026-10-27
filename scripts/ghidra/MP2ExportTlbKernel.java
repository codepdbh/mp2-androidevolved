// @category MP2R
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class MP2ExportTlbKernel extends GhidraScript {
    public void run() throws Exception {
        for(int i=0;i<6;i++) {
            long address=0x80075300L+i*8;
            int number=currentProgram.getMemory().getInt(toAddr(address));
            long entry=Integer.toUnsignedLong(currentProgram.getMemory().getInt(toAddr(address+4)));
            if(entry==0) continue;
            if(number==3 && entry==0x80075330L) continue; // original pointer to table storage, not executable code
            if(entry<0x80075000L || entry>=0x80075300L) throw new IllegalStateException("TLB callback outside payload code");
            disassemble(toAddr(entry));
            if(getFunctionAt(toAddr(entry))==null && createFunction(toAddr(entry),String.format("mp2_tlb_%08X",entry))==null)
                throw new IllegalStateException("Could not recover TLB callback");
            println("MP2R TLB callback "+Long.toHexString(entry)+" body="+getFunctionAt(toAddr(entry)).getBody());
        }
        File directory=new File(getScriptArgs()[0]);
        try(PrintWriter out=new PrintWriter(new File(directory,"kernel-functions.csv"));
            PrintWriter listing=new PrintWriter(new File(directory,"kernel-listing.txt"))) {
            out.println("Name,Start,End,Size");
            FunctionIterator functions=currentProgram.getFunctionManager().getFunctions(true);
            int count=0;
            while(functions.hasNext()) {
                Function f=functions.next(); long start=f.getEntryPoint().getOffset();
                AddressSetView body=f.getBody();
                if(start<0x80075000L || start>=0x80075330L || body.getNumAddressRanges()!=1) continue;
                out.printf("mp2_%08X,0x%08X,0x%08X,%d%n",start,start,body.getMaxAddress().getOffset()+1,body.getNumAddresses()); count++;
            }
            InstructionIterator instructions=currentProgram.getListing().getInstructions(new AddressSet(toAddr(0x80075000L),toAddr(0x8007532fL)),true);
            while(instructions.hasNext()) { Instruction i=instructions.next(); listing.println(i.getAddress()+" "+i); }
            println("MP2R TLB kernel functions="+count);
        }
    }
}
