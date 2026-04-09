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
        // For simulation, assume CE is 1 if not connected
        r0 <= D_OUT_0;
        r1 <= D_OUT_1;
    end

    // Use PIN_TYPE bits 3:2 to decide if we use DDR
    // Bits 3:2 == 2'b10 means DDR.
    // However, many Lattice users use 2'b11 for Registered Inverted.
    // For simulation we just check if OUTPUT_CLK is used.
    wire out_val = (PIN_TYPE[3:2] == 2'b10) ? (OUTPUT_CLK ? r0 : r1) : D_OUT_0;
    
    // For simulation, assume OUTPUT_ENABLE is 1 if not connected
    assign PACKAGE_PIN = out_val;
    assign PACKAGE_PIN_OUT = PACKAGE_PIN;

endmodule
