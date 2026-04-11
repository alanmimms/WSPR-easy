`timescale 1ns / 100ps

module FreqCounter (
		    input logic clk40,
		    input logic reset,
		    input logic fpgaNCS,
		    input logic samplePPS,
		    output logic [25:0] ppsCount,
		    output logic [5:0] ppsGen
		    );

  // 4-stage pipelined 28-bit counter to comfortably hit 90MHz
  reg [6:0] c0 = 0, c1 = 0, c2 = 0, c3 = 0;
  reg w0, w1, w2;

  always_ff @(posedge clk40) begin
    c0 <= c0 + 7'd1;
    w0 <= (c0 == 7'h7E);
    
    if (w0) begin
      c1 <= c1 + 7'd1;
      w1 <= (c1 == 7'h7E);
    end else begin
      w1 <= 1'b0;
    end

    if (w1) begin
      c2 <= c2 + 7'd1;
      w2 <= (c2 == 7'h7E);
    end else begin
      w2 <= 1'b0;
    end

    if (w2) begin
      c3 <= c3 + 7'd1;
    end
  end

  wire [25:0] counter40MHz = {c3[4:0], c2, c1, c0};

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
