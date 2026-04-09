module FreqCounter (
		    input logic clk40,
		    input logic reset,
		    input logic fpgaNCS,
		    input logic samplePPS,
		    output logic [25:0] ppsCount,
		    output logic [5:0] ppsGen
		    );

  logic [25:0] counter40MHz = 0;
  always_ff @(posedge clk40) counter40MHz <= counter40MHz + 1;

  logic syncNCS;
  logic fallingNCS;
  Synchronizer ncsSyncronizer (.clk(clk40), .dIn(fpgaNCS), .dOut(syncNCS));
  edgeDetector ncsEdgeDetector (.clk(clk40), .sigIn(syncNCS), .fallingOut(fallingNCS));

  logic syncPPS;
  logic risingPPS;
  Synchronizer ppsSyncronizer (.clk(clk40), .dIn(samplePPS), .dOut(syncPPS));
  edgeDetector ppsDetector (.clk(clk40), .sigIn(syncPPS), .risingOut(risingPPS));

  // Initialize outputs to avoid "constant bits" warnings
  initial begin
    ppsGen = 0;
    ppsCount = 0;
  end

  always_ff @(posedge clk40) begin
    if (risingPPS) begin
      ppsGen <= ppsGen + 1;
      ppsCount <= counter40MHz;
    end else if (fallingNCS) begin
      // Latches current time relative to PPS if needed, 
      // but only one driver is allowed.
      // We prioritize PPS rising edge as the time-base.
    end
  end

endmodule // FreqCounter
