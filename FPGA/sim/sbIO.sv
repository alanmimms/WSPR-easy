`timescale 1ns / 100ps
// Behavioral model for Lattice SB_IO (Registered DDR Output)
module SB_IO #(
    parameter [5:0] PIN_TYPE = 6'b000000,
    parameter [0:0] PULLUP = 1'b0,
    parameter [0:0] NEG_TRIGGER = 1'b0,
    parameter IO_STANDARD = "SB_LVCMOS"
) (
    inout  wire  PACKAGE_PIN,
    output logic PACKAGE_PIN_OUT,
    input  logic OUTPUT_CLK,
    input  logic CLOCK_ENABLE,
    input  logic OUTPUT_ENABLE,
    input  logic D_OUT_0,
    input  logic D_OUT_1,
    output logic D_IN_0,
    output logic D_IN_1,
    input  logic INPUT_CLK,
    input  logic LATCH_INPUT_VALUE
);

    // Simple DDR registered output model
    logic r0, r1;
    always @(posedge OUTPUT_CLK) begin
        if (CLOCK_ENABLE) begin
            r0 <= D_OUT_0;
            r1 <= D_OUT_1;
        end
    end

    assign PACKAGE_PIN = OUTPUT_ENABLE ? (OUTPUT_CLK ? r0 : r1) : 1'bz;
    assign PACKAGE_PIN_OUT = PACKAGE_PIN;

endmodule
