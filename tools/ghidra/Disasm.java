// Disassemble from an address (creating code if needed) and write the listing to a file.
// Args: <outFile> <count> <addr> [<addr> ...]
//@category PSUEx
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;

import java.io.FileWriter;
import java.io.PrintWriter;

public class Disasm extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		int count = Integer.parseInt( args[ 1 ] );
		try( PrintWriter out = new PrintWriter( new FileWriter( args[ 0 ] ) ) ) {
			for( int i = 2; i < args.length; ++i ) {
				Address addr = toAddr( Long.decode( args[ i ] ) );
				disassemble( addr );
				out.println( "; ===== " + addr );
				Instruction ins = getInstructionAt( addr );
				for( int n = 0; ins != null && n < count; ++n ) {
					out.println( ins.getAddress() + "  " + ins );
					ins = ins.getNext();
				}
			}
		}
	}
}
