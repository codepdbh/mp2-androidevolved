// Configure only existing analysis options; dump them for reproducibility.
// @category MP2R
import ghidra.app.script.GhidraScript;
import java.io.File;
import java.io.PrintWriter;
import java.util.Map;
public class MP2AnalysisOptions extends GhidraScript {
    public void run() throws Exception {
        String[] args=getScriptArgs();
        if(args.length!=1) throw new IllegalArgumentException("Expected options output path");
        Map<String,String> options=getCurrentAnalysisOptionsAndValues(currentProgram);
        for(String name:options.keySet()) {
            if(name.equals("Decompiler Parameter ID") || name.equals("Stack"))
                setAnalysisOption(currentProgram,name,"false");
        }
        try(PrintWriter out=new PrintWriter(new File(args[0]))) {
            getCurrentAnalysisOptionsAndValues(currentProgram).entrySet().stream()
                .sorted(Map.Entry.comparingByKey()).forEach(e->out.println(e.getKey()+"="+e.getValue()));
        }
        println("MP2R configured bounded R5900 analysis");
    }
}
