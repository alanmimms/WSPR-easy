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

  // T0
  reg [31:0] tw_q;
  reg [7:0]  pwr_q;
  reg        en_q, rst_q;
  always_ff @(posedge clk90) begin
    tw_q <= tuningWord; pwr_q <= powerThreshold; en_q <= txEnable; rst_q <= reset;
  end

  wire [32:0] tw90 = {tw_q, 1'b0};

  // NCO Pipeline (4-bit slices, 8 stages) - NO TUNING WORD PIPELINE
  reg [3:0] a [0:7];
  reg [3:0] m [0:7];
  reg       cf [0:7];
  reg       cm [0:7];
  reg       cf_t;

  always_ff @(posedge clk90) begin
    if (rst_q || !en_q) begin
      for (int i=0; i<8; i++) begin a[i]<=0; cf[i]<=0; m[i]<=0; cm[i]<=0; end
      cf_t <= 0;
    end else begin
      {cf[0], a[0]} <= a[0] + tw90[3:0]; {cm[0], m[0]} <= a[0] + tw_q[3:0];
      {cf[1], a[1]} <= a[1] + tw90[7:4] + cf[0]; {cm[1], m[1]} <= a[1] + tw_q[7:4] + cm[0];
      {cf[2], a[2]} <= a[2] + tw90[11:8] + cf[1]; {cm[2], m[2]} <= a[2] + tw_q[11:8] + cm[1];
      {cf[3], a[3]} <= a[3] + tw90[15:12] + cf[2]; {cm[3], m[3]} <= a[3] + tw_q[15:12] + cm[2];
      {cf[4], a[4]} <= a[4] + tw90[19:16] + cf[3]; {cm[4], m[4]} <= a[4] + tw_q[19:16] + cm[3];
      {cf[5], a[5]} <= a[5] + tw90[23:20] + cf[4]; {cm[5], m[5]} <= a[5] + tw_q[23:20] + cm[4];
      {cf[6], a[6]} <= a[6] + tw90[27:24] + cf[5]; {cm[6], m[6]} <= a[6] + tw_q[27:24] + cm[5];
      begin
        logic [4:0] sF, sM;
        sF = {1'b0, a[7]} + {1'b0, tw90[31:28]} + {4'd0, cf[6]};
        a[7] <= sF[3:0]; {cf_t, cf[7]} <= {1'b0, sF[4]} + {1'b0, tw90[32]};
        sM = {1'b0, a[7]} + {1'b0, tw_q[31:28]} + {4'd0, cm[6]};
        m[7] <= sM[3:0]; cm[7] <= sM[4];
      end
    end
  end

  // T9: Step Integration
  reg [1:0] stepsF_T9;
  reg       stepM_T9;
  reg [7:0] posA_T9, posB_T9;
  always_ff @(posedge clk90) begin
    stepsF_T9 <= {1'b0, cf[7]} + {1'b0, cf_t};
    stepM_T9  <= cm[7];
    posA_T9   <= {a[7], a[6]};
    posB_T9   <= {m[7], m[6]};
  end

  // T10: Compare
  reg gateA_T10, gateB_T10;
  always_ff @(posedge clk90) begin
    gateA_T10 <= (posA_T9 < pwr_q);
    gateB_T10 <= (posB_T9 < pwr_q);
  end

  // T11: Ring Update
  reg [5:0] ring;
  reg [5:0] rA_T11, rB_T11;
  wire [5:0] rP1 = {ring[4:0], ring[5]};
  wire [5:0] rP2 = {ring[3:0], ring[5:4]};
  always_ff @(posedge clk90) begin
    if (rst_q || !en_q) ring <= 6'b000001;
    else begin
      rA_T11 <= ring;
      rB_T11 <= stepM_T9 ? rP1 : ring;
      case (stepsF_T9)
        2'd1: ring <= rP1;
        2'd2: ring <= rP2;
        default: ring <= ring;
      endcase
    end
  end

  // T12: Final Gate
  reg [3:0] fa, fb;
  reg ga_d1, gb_d1, ga_d2, gb_d2;
  always_ff @(posedge clk90) begin
    ga_d1 <= gateA_T10; gb_d1 <= gateB_T10;
    ga_d2 <= ga_d1; gb_d2 <= gb_d1;
    begin
      logic [3:0] ba, bb;
      ba[0]=rA_T11[0]|rA_T11[1]|rA_T11[2]; ba[1]=rA_T11[1]; ba[2]=rA_T11[3]|rA_T11[4]|rA_T11[5]; ba[3]=rA_T11[4];
      bb[0]=rB_T11[0]|rB_T11[1]|rB_T11[2]; bb[1]=rB_T11[1]; bb[2]=rB_T11[3]|rB_T11[4]|rB_T11[5]; bb[3]=rB_T11[4];
      fa <= ba & {4{ga_d2}}; fb <= bb & {4{gb_d2}};
    end
  end

  // SB_IO
  SB_IO #(.PIN_TYPE(6'b011000)) ioPB (.PACKAGE_PIN(rfPushBase), .OUTPUT_CLK(clk90), .D_OUT_0(fa[0]), .D_OUT_1(fb[0]));
  SB_IO #(.PIN_TYPE(6'b011000)) ioPP (.PACKAGE_PIN(rfPushPeak), .OUTPUT_CLK(clk90), .D_OUT_0(fa[1]), .D_OUT_1(fb[1]));
  SB_IO #(.PIN_TYPE(6'b011000)) ioLB (.PACKAGE_PIN(rfPullBase), .OUTPUT_CLK(clk90), .D_OUT_0(fa[2]), .D_OUT_1(fb[2]));
  SB_IO #(.PIN_TYPE(6'b011000)) ioLP (.PACKAGE_PIN(rfPullPeak), .OUTPUT_CLK(clk90), .D_OUT_0(fa[3]), .D_OUT_1(fb[3]));

endmodule
`default_nettype wire
