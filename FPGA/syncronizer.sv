module Synchronizer #(
		      parameter int WIDTH = 1,
		      parameter int STAGES = 2,
		      parameter [WIDTH-1:0] INIT = 0
		      )(
			input  logic clk,
			input  logic [WIDTH-1:0] dIn,
			output logic [WIDTH-1:0] dOut
			);
  // Use a flat vector for the pipeline to avoid "memory" warnings
  logic [WIDTH*STAGES-1:0] pipe = {STAGES{INIT}};

  always_ff @(posedge clk) begin
    // Shift in new data at the bottom, move everything up
    pipe <= {pipe[WIDTH*(STAGES-1)-1:0], dIn};
  end

  // Output is the "top" of the pipe
  assign dOut = pipe[WIDTH*STAGES-1 : WIDTH*(STAGES-1)];
endmodule
