// dumps evry function of the loaded program as decompiled c into one file per address range
// plus a strings file with who references each string. run it thru analyzeHeadless (see tools/decompile.sh)

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.File;
import java.io.PrintWriter;

public class DumpEverything extends GhidraScript {
    private static final int FUNCTIONS_PER_FILE = 2000;

    @Override
    protected void run() throws Exception {
        String outputDirectoryPath = getScriptArgs().length > 0 ? getScriptArgs()[0] : "decomp";
        File outputDirectory = new File(outputDirectoryPath);
        outputDirectory.mkdirs();

        dumpStrings(new File(outputDirectory, "strings_with_xrefs.txt"));

        DecompInterface decompiler = new DecompInterface();
        decompiler.setOptions(new DecompileOptions());
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        decompiler.openProgram(currentProgram);

        FunctionIterator allFunctions = currentProgram.getFunctionManager().getFunctions(true);
        int functionCount = 0;
        int fileNumber = 0;
        PrintWriter currentFile = null;
        while (allFunctions.hasNext() && !monitor.isCancelled()) {
            Function function = allFunctions.next();
            if (functionCount % FUNCTIONS_PER_FILE == 0) {
                if (currentFile != null) {
                    currentFile.close();
                }
                currentFile = new PrintWriter(new File(outputDirectory, String.format("functions_%03d.c", fileNumber++)));
            }
            DecompileResults results = decompiler.decompileFunction(function, 90, monitor);
            currentFile.printf("//==== %s @ %s%n", function.getName(), function.getEntryPoint());
            if (results != null && results.decompileCompleted()) {
                currentFile.println(results.getDecompiledFunction().getC());
            } else {
                currentFile.println("// decompile failed");
            }
            functionCount++;
            if (functionCount % 500 == 0) {
                println("decompiled " + functionCount);
            }
        }
        if (currentFile != null) {
            currentFile.close();
        }
        decompiler.dispose();
        println("done, " + functionCount + " functions");
    }

    private void dumpStrings(File outputFile) throws Exception {
        try (PrintWriter writer = new PrintWriter(outputFile)) {
            DataIterator allData = currentProgram.getListing().getDefinedData(true);
            while (allData.hasNext()) {
                Data data = allData.next();
                if (!data.hasStringValue()) {
                    continue;
                }
                String value = StringDataInstance.getStringDataInstance(data).getStringValue();
                if (value == null || value.length() < 4) {
                    continue;
                }
                StringBuilder referencedBy = new StringBuilder();
                for (Reference reference : getReferencesTo(data.getAddress())) {
                    Function caller = getFunctionContaining(reference.getFromAddress());
                    referencedBy.append(caller != null ? caller.getName() : reference.getFromAddress().toString()).append(' ');
                }
                writer.printf("%s\t%s\t%s%n", data.getAddress(), referencedBy.toString().trim(), value.replace("\n", "\\n"));
            }
        }
    }
}
