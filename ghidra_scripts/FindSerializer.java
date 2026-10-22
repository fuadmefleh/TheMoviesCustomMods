// For each vtable, reports the slots whose function calls the named-field serializer
// (0x0098B490), i.e. the class's save/load function.
// Usage (headless): -postScript FindSerializer.java <output.txt> <slots> <vtable_addr> [...]
//@category MoviesMod

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;

public class FindSerializer extends GhidraScript {

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        int slots = Integer.parseInt(args[1]);
        try (PrintWriter out = new PrintWriter(args[0], StandardCharsets.UTF_8)) {
            for (int i = 2; i < args.length; i++) {
                Address vt = toAddr(args[i]);
                for (int s = 0; s < slots; s++) {
                    Address fn = toAddr(getInt(vt.add(s * 4L)) & 0xffffffffL);
                    if (getMemoryBlock(fn) == null || !getMemoryBlock(fn).isExecute())
                        break;
                    Function f = getFunctionAt(fn);
                    if (f == null) {
                        new CreateFunctionCmd(fn).applyTo(currentProgram, monitor);
                        f = getFunctionAt(fn);
                    }
                    if (f == null)
                        continue;
                    int calls = 0;
                    for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                        Address[] flows = ins.getFlows();
                        if (ins.getMnemonicString().equals("CALL") && flows.length == 1 && flows[0].getOffset() == 0x0098b490L)
                            calls++;
                    }
                    if (calls > 0)
                        out.println(vt + " slot " + s + " -> " + fn + " (" + calls + " fields)");
                }
            }
        }
    }
}
