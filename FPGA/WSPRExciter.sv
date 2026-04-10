module WSPRExciter (
    input  logic clk90,
    input  logic reset,
    input  logic [31:0] tuningWord,
    input  logic txEnable,

    // Physical RF Pins (DDR)
    output logic rfPushBase,
    output logic rfPushPeak,
    output logic rfPullBase,
    output logic rfPullPeak
    );

  // =========================================================================
  // 1. NCO Pipeline (T1-T10)
  // =========================================================================
  
  // Register inputs locally
  logic [31:0] tw_q = 0;
  logic txEn_q = 0;
  always_ff @(posedge clk90) begin
    tw_q <= tuningWord;
    txEn_q <= txEnable;
  end

  // Pipelined 32-bit Phase Accumulator (8-stage 4-bit)
  logic [3:0] s0=0, s1=0, s2=0, s3=0, s4=0, s5=0, s6=0, s7=0;
  logic c0=0, c1=0, c2=0, c3=0, c4=0, c5=0, c6=0;
  logic c0q=0, c1q=0, c2q=0, c3q=0, c4q=0, c5q=0, c6q=0;
  logic [3:0] tw0=0, tw1=0, tw2=0, tw3=0, tw4=0, tw5=0, tw6=0, tw7=0;

  always_ff @(posedge clk90) begin
    tw0 <= tw_q[3:0];   tw1 <= tw_q[7:4];
    tw2 <= tw_q[11:8];  tw3 <= tw_q[15:12];
    tw4 <= tw_q[19:16]; tw5 <= tw_q[23:20];
    tw6 <= tw_q[27:24]; tw7 <= tw_q[31:28];
  end

  always_ff @(posedge clk90) begin
    if (!txEn_q) begin
      s0<=0; s1<=0; s2<=0; s3<=0; s4<=0; s5<=0; s6<=0; s7<=0;
      c0q<=0; c1q<=0; c2q<=0; c3q<=0; c4q<=0; c5q<=0; c6q<=0;
    end else begin
      {c0, s0} <= s0 + tw0;
      c0q <= c0;
      {c1, s1} <= s1 + tw1 + c0q;
      c1q <= c1;
      {c2, s2} <= s2 + tw2 + c1q;
      c2q <= c2;
      {c3, s3} <= s3 + tw3 + c2q;
      c3q <= c3;
      {c4, s4} <= s4 + tw4 + c3q;
      c4q <= c4;
      {c5, s5} <= s5 + tw5 + c4q;
      c5q <= c5;
      {c6, s6} <= s6 + tw6 + c5q;
      c6q <= c6;
      s7       <= s7 + tw7 + c6q;
    end
  end

  // Skew-align high bits (T10)
  logic [15:0] ph;
  logic [3:0] s6_d, s5_d, s4_d;
  always_ff @(posedge clk90) begin
    s6_d <= s6;
    s5_d <= s5;
    s4_d <= s4;
    ph <= {s7, s6_d, s5_d, s4_d};
  end

  // =========================================================================
  // 2. Scaling Pipeline (T11-T14)
  // =========================================================================
  logic [15:0] phR, phF;
  logic [14:0] twHigh_q;
  always_ff @(posedge clk90) begin
    twHigh_q <= tw_q[31:17]; 
    phR <= ph;
    phF <= ph + twHigh_q;
  end

  logic [10:0] sR, sF;
  always_ff @(posedge clk90) begin
    sR <= ({3'd0, phR[15:8]} << 2) + ({3'd0, phR[15:8]} << 1);
    sF <= ({3'd0, phF[15:8]} << 2) + ({3'd0, phF[15:8]} << 1);
  end

  logic [2:0] stR, stF;
  always_ff @(posedge clk90) begin
    stR <= sR[10:8];
    stF <= sF[10:8];
  end

  // =========================================================================
  // 3. Decoding & Output (T15-T17)
  // =========================================================================
  logic [31:0] enP = 0; 
  always_ff @(posedge clk90) enP <= {enP[30:0], txEn_q};

  logic rb0_i, rb1_i, rp0_i, rp1_i, lb0_i, lb1_i, lp0_i, lp1_i;
  always_ff @(posedge clk90) begin
    rb0_i <= 0; rb1_i <= 0; rp0_i <= 0; rp1_i <= 0;
    lb0_i <= 0; lb1_i <= 0; lp0_i <= 0; lp1_i <= 0;
    if (enP[15]) begin 
      case (stR)
        3'd0, 3'd2: rb0_i <= 1; 3'd1: rp0_i <= 1;
        3'd3, 3'd5: lb0_i <= 1; 3'd4: lp0_i <= 1;
      endcase
      case (stF)
        3'd0, 3'd2: rb1_i <= 1; 3'd1: rp1_i <= 1;
        3'd3, 3'd5: lb1_i <= 1; 3'd4: lp1_i <= 1;
      endcase
    end
  end

  // Final stage registers for SB_IO packing
  logic rb0, rb1, rp0, rp1, lb0, lb1, lp0, lp1;
  always_ff @(posedge clk90) begin
    rb0 <= rb0_i; rb1 <= rb1_i; rp0 <= rp0_i; rp1 <= rp1_i;
    lb0 <= lb0_i; lb1 <= lb1_i; lp0 <= lp0_i; lp1 <= lp1_i;
  end

  SB_IO #(.PIN_TYPE(6'b010000)) ioPushB (.PACKAGE_PIN(rfPushBase), .OUTPUT_CLK(clk90), .D_OUT_0(rb0), .D_OUT_1(rb1));
  SB_IO #(.PIN_TYPE(6'b010000)) ioPushP (.PACKAGE_PIN(rfPushPeak), .OUTPUT_CLK(clk90), .D_OUT_0(rp0), .D_OUT_1(rp1));
  SB_IO #(.PIN_TYPE(6'b010000)) ioPullB (.PACKAGE_PIN(rfPullBase), .OUTPUT_CLK(clk90), .D_OUT_0(lb0), .D_OUT_1(lb1));
  SB_IO #(.PIN_TYPE(6'b010000)) ioPullP (.PACKAGE_PIN(rfPullPeak), .OUTPUT_CLK(clk90), .D_OUT_0(lp0), .D_OUT_1(lp1));

endmodule
