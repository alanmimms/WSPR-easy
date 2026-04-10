`timescale 1ns / 100ps

module edgeDetector #(
    parameter int WIDTH = 1,
    parameter logic [WIDTH-1:0] RESET_VALUE = '0
) (
    input  logic clk,
    input  logic sigIn,
    output logic risingOut,
    output logic fallingOut,
    output logic anyOut
);

  logic sigDelay = 0;

  always_ff @(posedge clk) begin
    sigDelay <= sigIn;
  end

  assign risingOut  = sigIn & ~sigDelay;
  assign fallingOut = ~sigIn & sigDelay;
  assign anyOut     = sigIn ^ sigDelay;

endmodule // edgeDetector
