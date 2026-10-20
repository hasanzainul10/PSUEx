// Headless pre-script: the unpacked PSUC.exe marks its code section (SECTION.0) rw- without
// execute, so auto-analysis only follows code reached from the entry point (~2,000 functions).
// Marks that block executable and sets the analyzers for a full pass.
//   analyzeHeadless D:\PSUExTools\ghidra-projects PSUClementine -process PSUC_Uncompressed.exe
//     -scriptPath tools\ghidra -preScript PrepareFullAnalysis.java
//@category PSUEx

import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.MemoryBlock;

public class PrepareFullAnalysis extends GhidraScript {
	@Override
	protected void run() throws Exception {
		MemoryBlock code = currentProgram.getMemory().getBlock( toAddr( 0x00401000L ) );
		if( code == null ) {
			printerr( "No block at 0x00401000" );
			return;
		}
		code.setExecute( true );
		println( "Executable: " + code.getName() + " " + code.getStart() + "-" + code.getEnd() );
		setAnalysisOption( currentProgram, "PDB Universal", "false" );
		setAnalysisOption( currentProgram, "PDB MSDIA", "false" );
		setAnalysisOption( currentProgram, "Aggressive Instruction Finder", "true" );
		println( "Functions before: " + currentProgram.getFunctionManager().getFunctionCount() );
	}
}
