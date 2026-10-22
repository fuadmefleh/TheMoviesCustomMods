// Lists the named fields a save/load function serializes, with the operands of the
// serializer call that follows each name (which give the field offset).
// The game's serializer: bool FieldName(const char*) @ 0x0098B490, followed by a
// typed read/write call (FUN_0098xxxx) on the field address.
// Usage (headless): -postScript SaveFields.java <output.txt> <func_addr> [...]
//@category MoviesMod

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;

import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Deque;

public class SaveFields extends GhidraScript {

    private static final long FIELD_NAME_FN = 0x0098b490L;

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        try (PrintWriter out = new PrintWriter(args[0], StandardCharsets.UTF_8)) {
            for (int i = 1; i < args.length; i++) {
                Function f = getFunctionContaining(toAddr(args[i]));
                out.println("######## " + f.getName() + " @ " + f.getEntryPoint());
                Deque<Instruction> recent = new ArrayDeque<>();
                String pending = null; String before = "";
                Deque<String> since = new ArrayDeque<>();
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                    if (isCallTo(ins, FIELD_NAME_FN)) {
                        pending = findPushedString(recent); before = describe(recent);
                        since.clear();
                    } else if (pending != null && ins.getMnemonicString().equals("CALL")) {
                        out.println(String.format("%-28s %s  args: %s  || before: %s", pending, ins, String.join(" | ", since), before));
                        pending = null;
                    } else if (pending != null) {
                        String s = ins.toString();
                        if (s.startsWith("PUSH") || s.startsWith("LEA") || s.startsWith("ADD E") || (s.startsWith("MOV E") && s.contains("+")))
                            since.add(s);
                    }
                    recent.addLast(ins);
                    if (recent.size() > 10)
                        recent.removeFirst();
                }
            }
        }
    }

    private String describe(Deque<Instruction> recent) {
        StringBuilder sb = new StringBuilder();
        for (Instruction i : recent) {
            String s = i.toString();
            if (s.contains("+ 0x") || s.startsWith("ADD") || s.startsWith("LEA")) sb.append(s).append(" ; ");
        }
        return sb.toString();
    }

    private boolean isCallTo(Instruction ins, long target) {
        if (!ins.getMnemonicString().equals("CALL"))
            return false;
        Address[] flows = ins.getFlows();
        return flows.length == 1 && flows[0].getOffset() == target;
    }

    private String findPushedString(Deque<Instruction> recent) throws Exception {
        Instruction[] arr = recent.toArray(new Instruction[0]);
        for (int i = arr.length - 1; i >= 0; i--) {
            Instruction ins = arr[i];
            if (!ins.getMnemonicString().equals("PUSH"))
                continue;
            Object[] ops = ins.getOpObjects(0);
            if (ops.length == 1 && ops[0] instanceof Scalar) {
                Address a = toAddr(((Scalar) ops[0]).getUnsignedValue());
                StringBuilder sb = new StringBuilder();
                for (int k = 0; k < 64; k++) {
                    byte b = getByte(a.add(k));
                    if (b == 0)
                        break;
                    sb.append((char) b);
                }
                return sb.toString();
            }
        }
        return "?";
    }
}
