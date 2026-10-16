// Create functions from root addresses (and their direct callees, to a depth) and write
// their decompiled C to a file. Args: <outFile> <depth> <addr> [<addr> ...]
//@category PSUEx
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.ArrayDeque;
import java.util.HashSet;
import java.util.Set;

public class DecompileTree extends GhidraScript {
	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		String outFile = args[ 0 ];
		int maxDepth = Integer.parseInt( args[ 1 ] );
		FunctionManager fm = currentProgram.getFunctionManager();
		DecompInterface decomp = new DecompInterface();
		decomp.openProgram( currentProgram );

		ArrayDeque< long[] > queue = new ArrayDeque<>();
		for( int i = 2; i < args.length; ++i ) {
			queue.add( new long[] { Long.decode( args[ i ] ), 0 } );
		}
		Set< Long > seen = new HashSet<>();
		try( PrintWriter out = new PrintWriter( new FileWriter( outFile ) ) ) {
			while( !queue.isEmpty() ) {
				long[] item = queue.poll();
				if( !seen.add( item[ 0 ] ) ) {
					continue;
				}
				Address addr = toAddr( item[ 0 ] );
				Function f = fm.getFunctionAt( addr );
				if( f == null ) {
					disassemble( addr );
					f = createFunction( addr, null );
				}
				if( f == null ) {
					out.println( "// FAILED to create function at " + addr );
					continue;
				}
				out.println( "// ===== " + f.getName() + " @ " + addr + " (depth " + item[ 1 ] + ")" );
				DecompileResults r = decomp.decompileFunction( f, 60, monitor );
				out.println( r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed" );
				if( item[ 1 ] >= maxDepth ) {
					continue;
				}
				InstructionIterator it = currentProgram.getListing().getInstructions( f.getBody(), true );
				while( it.hasNext() ) {
					Instruction ins = it.next();
					if( !ins.getFlowType().isCall() ) {
						continue;
					}
					for( Reference ref : ins.getReferencesFrom() ) {
						if( ref.getReferenceType().isCall() ) {
							long target = ref.getToAddress().getOffset();
							if( target >= 0x00400000L && target < 0x00900000L ) {
								queue.add( new long[] { target, item[ 1 ] + 1 } );
							}
						}
					}
				}
			}
		}
		println( "Wrote " + outFile + " (" + seen.size() + " functions)" );
	}
}
