module tb_tridrive;
	logic       clk;
	logic       en0;
	logic       en1;
	wire  [7:0] bus;

	tridrive u_drv0 (
		.en  (en0),
		.val (8'b0000_1000),
		.bus (bus)
	);

	tridrive u_drv1 (
		.en  (en1),
		.val (8'b0000_0100),
		.bus (bus)
	);

	initial clk = 1'b0;
	always #5 clk = ~clk;

	initial begin
		$dumpfile("dump_tridrive.vcd");
		$dumpvars(0, tb_tridrive);

		en0 = 1'b0;
		en1 = 1'b0;

		for (int cycle = 0; cycle < 10; cycle++) begin
			@(negedge clk);
			if (cycle < 8) begin
				en0 = (cycle % 2 == 0);
				en1 = (cycle % 2 == 1);
			end else begin
				en0 = 1'b1;
				en1 = 1'b1;
			end
			@(posedge clk);
			#1;
			$display("cycle=%0d en0=%0b en1=%0b bus=%b", cycle, en0, en1, bus);
		end

		$finish;
	end
endmodule
