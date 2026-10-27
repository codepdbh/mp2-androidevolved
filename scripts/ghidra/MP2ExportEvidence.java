// Preserve function-body ranges and startup instructions for review.
// @category MP2R
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.io.*;
public class MP2ExportEvidence extends GhidraScript {
    public void run() throws Exception {
        File directory=new File(getScriptArgs()[0]);
        int total=0,contiguous=0,fragmented=0;
        try(PrintWriter ranges=new PrintWriter(new File(directory,"body-ranges.csv"));
            PrintWriter strict=new PrintWriter(new File(directory,"mp2-functions-only.csv"));
            PrintWriter startup=new PrintWriter(new File(directory,"startup-disassembly.txt"))) {
            ranges.println("Name,Entry,RangeStart,RangeEndExclusive");
            strict.println("Name,Start,End,Size");
            FunctionIterator functions=currentProgram.getFunctionManager().getFunctions(true);
            while(functions.hasNext() && !monitor.isCancelled()) {
                Function f=functions.next();
                long entry=f.getEntryPoint().getOffset();
                if(entry<0x100000L || entry>=0x5c0d00L) continue;
                AddressSetView body=f.getBody();
                total++;
                AddressRangeIterator iterator=body.getAddressRanges();
                int count=0;
                while(iterator.hasNext()) {
                    AddressRange r=iterator.next(); count++;
                    ranges.printf("%s,0x%08X,0x%08X,0x%08X%n",f.getName(),entry,
                        r.getMinAddress().getOffset(),r.getMaxAddress().getOffset()+1);
                }
                if(count==1 && body.getMinAddress().equals(f.getEntryPoint())) {
                    contiguous++;
                    // Stable analysis name; this is an address, not an identified game API.
                    strict.printf("mp2_%08X,0x%08X,0x%08X,%d%n",entry,entry,
                        body.getMaxAddress().getOffset()+1,body.getNumAddresses());
                } else fragmented++;
            }
            AddressSet region=new AddressSet(toAddr(0x100000),toAddr(0x100250));
            InstructionIterator instructions=currentProgram.getListing().getInstructions(region,true);
            while(instructions.hasNext()) {
                Instruction ins=instructions.next();
                startup.printf("%s  %s%n",ins.getAddress(),ins);
            }
        }
        try(PrintWriter out=new PrintWriter(new File(directory,"evidence-summary.txt"))) {
            out.printf("function_count=%d%ncontiguous_count=%d%nfragmented_count=%d%n",total,contiguous,fragmented);
        }
        println("MP2R function bodies: total="+total+" contiguous="+contiguous+" fragmented="+fragmented);
    }
}
