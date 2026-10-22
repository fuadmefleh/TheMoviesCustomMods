// For each function address, prints the stack bytes its RET instructions pop (RET n), which
// gives the real argument count of __thiscall/__stdcall functions (n / 4; ECX not counted).
// Usage (headless): -postScript RetSize.java <output.txt> <addr> [<addr> ...]
//@category MoviesMod

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.TreeSet;

public class RetSize extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(args[0], StandardCharsets.UTF_8)) {
            for (int i = 1; i < args.length; i++) {
                Address a = toAddr(args[i]);
                Function f = getFunctionAt(a);
                if (f == null)
                    f = getFunctionContaining(a);
                if (f == null) {
                    out.println(args[i] + ": no function");
                    continue;
                }
                TreeSet<String> rets = new TreeSet<>();
                InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
                while (it.hasNext()) {
                    Instruction ins = it.next();
                    if (ins.getMnemonicString().equalsIgnoreCase("RET"))
                        rets.add(ins.getNumOperands() > 0 ? ins.getDefaultOperandRepresentation(0) : "0");
                }
                out.println(args[i] + " " + f.getName() + " " + f.getSignature().getPrototypeString() + "  RET " + rets);
            }
        }
    }
}
