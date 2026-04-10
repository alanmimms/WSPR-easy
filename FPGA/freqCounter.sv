module FreqCounter (
		    input logic clk40,
		    input logic reset,
		    input logic fpgaNCS,
		    input logic samplePPS,
		    output logic [25:0] ppsCount,
		    output logic [5:0] ppsGen
		    );

  // 2-stage pipelined 26-bit counter to hit 90MHz
  logic [12:0] cLow = 0;
  logic [12:0] cHigh = 0;
  logic lowWrap = 0;

  always_ff @(posedge clk40) begin
    cLow <= cLow + 13'd1;
    // Signal wrap one cycle early to allow registration
    lowWrap <= (cLow == 13'h1FFE);
    
    if (lowWrap) begin
      cHigh <= cHigh + 13'd1;
    end
  end

  // Re-combine for output. Note: cHigh is effectively 2 cycles behind cLow
  // relative to the start of the count, but for frequency measurement
  // it just creates a fixed offset which cancels out.
  wire [25:0] counter40MHz = {cHigh, cLow};

  logic syncNCS;
  logic fallingNCS;
  Synchronizer ncsSyncronizer (.clk(clk40), .dIn(fpgaNCS), .dOut(syncNCS));
  edgeDetector ncsEdgeDetector (.clk(clk40), .sigIn(syncNCS), .fallingOut(fallingNCS));

  logic syncPPS;
  logic risingPPS;
  Synchronizer ppsSyncronizer (.clk(clk40), .dIn(samplePPS), .dOut(syncPPS));
  edgeDetector ppsDetector (.clk(clk40), .sigIn(syncPPS), .risingOut(risingPPS));

  // Initialize outputs
  initial begin
    ppsGen = 0;
    ppsCount = 0;
  end

  always_ff @(posedge clk40) begin
    if (risingPPS) begin
      ppsGen <= ppsGen + 6'd1;
      ppsCount <= counter40MHz;
    end
  end

endmodule // FreqCounter
