`timescale 1ns / 1ps
// Simulation-only behavioral model for Lattice SB_IO
`default_nettype none
module SB_IO #(
    parameter PIN_TYPE = 6'b000000,
    parameter IO_STANDARD = "SB_LVCMOS"
) (
    inout  wire PACKAGE_PIN,
    input  wire OUTPUT_CLK,
    input  wire CLOCK_ENABLE,
    input  wire INPUT_CLK,
    input  wire OUTPUT_ENABLE,
    input  wire D_OUT_0,
    input  wire D_OUT_1,
    output wire D_IN_0,
    output wire D_IN_1
);

    // This model only implements PIN_TYPE = 6'b010000 (PIN_OUTPUT_DDR)

    assign PACKAGE_PIN = (CLOCK_ENABLE && OUTPUT_ENABLE) ?
                         (OUTPUT_CLK ? D_OUT_0 : D_OUT_1) :
                         1'bz; // High impedance when not enabled

    // Input path is not used in this design, so D_IN is not driven.
    assign D_IN_0 = 1'bz;
    assign D_IN_1 = 1'bz;

endmodule
