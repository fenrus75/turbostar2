module tridrive (
	input  logic       en,
	input  logic [7:0] val,
	inout  wire  [7:0] bus
);
	assign bus = en ? val : 8'bzzzz_zzzz;
endmodule
