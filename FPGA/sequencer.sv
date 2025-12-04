`timescale 1ns / 1ps
/**
 * file: sequencer.sv
 * module: Sequencer121
 * description: Combinatorial decoder for the WSPR-ease 6-step harmonic cancellation waveform.
 * Maps a step index (0-5) to the One-Hot MOSFET drive signals.
 * Source:
 */

module Sequencer121 (
    input  logic [2:0] stepIndex,
    output logic       pushBase,
    output logic       pushPeak,
    output logic       pullBase,
    output logic       pullPeak
);

    always_comb begin
        // Default safe state (All OFF)
        pushBase = 1'b0;
        pushPeak = 1'b0;
        pullBase = 1'b0;
        pullPeak = 1'b0;

        case (stepIndex)
            3'd0: pushBase = 1'b1; // Phase 0-60:   +1
            3'd1: pushPeak = 1'b1; // Phase 60-120: +2
            3'd2: pushBase = 1'b1; // Phase 120-180: +1
            3'd3: pullBase = 1'b1; // Phase 180-240: -1
            3'd4: pullPeak = 1'b1; // Phase 240-300: -2
            3'd5: pullBase = 1'b1; // Phase 300-360: -1
            default: begin
                pushBase = 1'b0;
            end
        endcase
    end

endmodule // Sequencer121
