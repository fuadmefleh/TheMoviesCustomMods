// Finds functions that reference the stress / boredom / addiction tuning strings
// in MoviesSE.exe and writes their decompiled C to an output file.
// Usage (headless): -postScript DumpTuningRefs.java <output.txt>
//@category MoviesMod

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class DumpTuningRefs extends GhidraScript {

    private static final String[] TARGETS = {
        "WORKINGONAMOVIESTRESS", "WORKINGONAMOVIEBOREDOM", "REHEARSINGSTRESS",
        "IDLINGSTRESS", "DRINKINGSTRESS", "DETOXADDICTIONS", "ADDICTEDWORKDECAYPERTICK",
        "MAXIMUMBOREDOMONPROJECT", "StressThreshold", "BoredomThreshold",
        "ai_drunk_thresh", "ai_vdrunk_thresh", "ai_stressed", "ai_boredom",
        "drunkennessadder", "desiregetdrunk", "desireoverdrink", "boredomhalflife",
        "boredomboost", "TOOSTRESSED", "THEYAREDRUNK",
    };

    @Override
    protected void run() throws Exception {
        String outPath = getScriptArgs().length > 0 ? getScriptArgs()[0] : "tuning_refs.txt";
        Memory mem = currentProgram.getMemory();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        Map<Function, Set<String>> funcs = new LinkedHashMap<>();
        try (PrintWriter out = new PrintWriter(outPath, StandardCharsets.UTF_8)) {
            out.println("=== string references ===");
            for (String target : TARGETS) {
                byte[] needle = (target + "\0").getBytes(StandardCharsets.US_ASCII);
                Address addr = mem.findBytes(currentProgram.getMinAddress(), needle, null, true, monitor);
                if (addr == null) {
                    out.println(target + ": not found");
                    continue;
                }
                List<String> sites = new ArrayList<>();
                for (Reference ref : getReferencesTo(addr)) {
                    Address from = ref.getFromAddress();
                    Function f = getFunctionContaining(from);
                    sites.add(from + (f != null ? " in " + f.getName() + "@" + f.getEntryPoint() : " (no function)"));
                    if (f != null)
                        funcs.computeIfAbsent(f, k -> new TreeSet<>()).add(target);
                }
                out.println(target + " @ " + addr + " <- " + (sites.isEmpty() ? "no refs" : String.join("; ", sites)));
            }

            out.println();
            out.println("=== decompiled functions ===");
            for (Map.Entry<Function, Set<String>> e : funcs.entrySet()) {
                Function f = e.getKey();
                out.println();
                out.println("// ---- " + f.getName() + " @ " + f.getEntryPoint() + " uses " + e.getValue());
                DecompileResults res = decomp.decompileFunction(f, 120, monitor);
                out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed: " + res.getErrorMessage());
            }
        }
        decomp.dispose();
        println("wrote " + outPath + " (" + funcs.size() + " functions)");
    }
}
