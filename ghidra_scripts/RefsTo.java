// Lists every instruction that uses a given constant (as an immediate, a displacement or
// a reference), e.g. a vtable address or a global, and decompiles the functions using it.
// Prefix a value with '-' to list sites only (no decompile).
// Usage (headless): -postScript RefsTo.java <output.txt> <value> [<value> ...]
//@category MoviesMod

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class RefsTo extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(args[0], StandardCharsets.UTF_8)) {
            for (int i = 1; i < args.length; i++) {
                boolean sitesOnly = args[i].startsWith("-");
                String a = sitesOnly ? args[i].substring(1) : args[i];
                long value = Long.decode(a);
                Map<Function, List<String>> funcs = new LinkedHashMap<>();
                List<String> orphans = new ArrayList<>();
                for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
                    boolean hit = false;
                    for (int op = 0; op < ins.getNumOperands() && !hit; op++) {
                        for (Object o : ins.getOpObjects(op)) {
                            if (o instanceof Scalar && (((Scalar) o).getUnsignedValue() == value)) hit = true;
                            if (o instanceof ghidra.program.model.address.Address
                                    && ((ghidra.program.model.address.Address) o).getOffset() == value) hit = true;
                        }
                    }
                    if (!hit)
                        for (Reference r : ins.getReferencesFrom())
                            if (r.getToAddress().getOffset() == value) hit = true;
                    if (!hit) continue;
                    Function f = getFunctionContaining(ins.getAddress());
                    String site = ins.getAddress() + "  " + ins;
                    if (f == null) orphans.add(site);
                    else funcs.computeIfAbsent(f, k -> new ArrayList<>()).add(site);
                }
                out.println("######## " + a + ": " + funcs.size() + " functions");
                for (String s : orphans) out.println("   (no function) " + s);
                for (Map.Entry<Function, List<String>> e : funcs.entrySet()) {
                    out.println(" " + e.getKey().getName() + " @ " + e.getKey().getEntryPoint());
                    for (String s : e.getValue()) out.println("     " + s);
                }
                if (sitesOnly) continue;
                int n = 0;
                for (Function f : funcs.keySet()) {
                    if (n++ >= 12) break;
                    out.println("// ==== " + f.getName() + " @ " + f.getEntryPoint());
                    DecompileResults res = decomp.decompileFunction(f, 120, monitor);
                    out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "// decompile failed");
                }
            }
        }
        decomp.dispose();
    }
}
