// List the references to each address (from instruction, containing function) and write them to a file.
// Args: <outFile> <addr> [<addr> ...]
//@category PSUEx
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;

public class Xrefs extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		try( PrintWriter out = new PrintWriter( new FileWriter( args[ 0 ] ) ) ) {
			for( int i = 1; i < args.length; ++i ) {
				Address addr = toAddr( Long.decode( args[ i ] ) );
				out.println( "; ===== refs to " + addr );
				for( Reference ref : getReferencesTo( addr ) ) {
					Address from = ref.getFromAddress();
					Function f = getFunctionContaining( from );
					out.println( from + "  " + ref.getReferenceType() + "  in " + ( f == null ? "?" : f.getName() + " @ " + f.getEntryPoint() ) );
				}
			}
		}
	}
}
