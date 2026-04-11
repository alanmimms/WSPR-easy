`timescale 1ns / 100ps
`default_nettype none

module WSPRExciter (
    input  wire        clk90,
    input  wire        reset,
    input  wire [31:0] tuningWord,
    input  wire [7:0]  powerThreshold,
    input  wire        txEnable,

    output wire        rfPushBase,
    output wire        rfPushPeak,
    output wire        rfPullBase,
    output wire        rfPullPeak
    );

  // No registration of tuning word - keep it simple
  reg [31:0] acc;
  always_ff @(posedge clk90) begin
    acc <= acc + tuningWord;
  end

  // State is just top 3 bits (0-7)
  // Mapping to 6 states by simple wrapping
  reg [2:0] state;
  always_ff @(posedge clk90) begin
    if (acc[31:29] >= 3'd6) state <= acc[31:29] - 3'd6;
    else                    state <= acc[31:29];
  end

  reg [3:0] out;
  always_ff @(posedge clk90) begin
    case (state)
      3'd0: out <= 4'b0001; 3'd1: out <= 4'b0011; 3'd2: out <= 4'b0001;
      3'd3: out <= 4'b0100; 3'd4: out <= 4'b1100; 3'd5: out <= 4'b0100;
      default: out <= 0;
    endcase
  end

  // DDR OUTPUT (Fixed high for now)
  SB_IO #(.PIN_TYPE(6'b011000)) ioPB (.PACKAGE_PIN(rfPushBase), .OUTPUT_CLK(clk90), .D_OUT_0(out[0]), .D_OUT_1(out[0]));
  SB_IO #(.PIN_TYPE(6'b011000)) ioPP (.PACKAGE_PIN(rfPushPeak), .OUTPUT_CLK(clk90), .D_OUT_0(out[1]), .D_OUT_1(out[1]));
  SB_IO #(.PIN_TYPE(6'b011000)) ioLB (.PACKAGE_PIN(rfPullBase), .OUTPUT_CLK(clk90), .D_OUT_0(out[2]), .D_OUT_1(out[2]));
  SB_IO #(.PIN_TYPE(6'b011000)) ioLP (.PACKAGE_PIN(rfPullPeak), .OUTPUT_CLK(clk90), .D_OUT_0(out[3]), .D_OUT_1(out[3]));

endmodule
`default_nettype wire
