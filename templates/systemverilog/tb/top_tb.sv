// @@PROJECT_NAME@@ - Self-checking testbench for top module
// Language Standard: @@LANGUAGE_STD@@
`timescale 1ns / 1ps
`default_nettype none

module top_tb;
    logic       clk;
    logic       rst_n;
    logic [7:0] led;

    top #(
        .CLK_FREQ_HZ(10_000)
    ) dut (
        .clk   (clk),
        .rst_n (rst_n),
        .led   (led)
    );

    // 100 MHz clock generation (10ns period)
    initial clk = 1'b0;
    always #5 clk = ~clk;

    initial begin
        $dumpfile("wave/dump_top_tb.vcd");
        $dumpvars(0, top_tb);

        // Reset pulse (active-low)
        rst_n = 1'b0;
        #20;
        if (led !== 8'h00) begin
            $error("FAIL: expected led == 8'h00 during reset, got %h", led);
            $finish;
        end

        rst_n = 1'b1;

        // Run simulation for several clock cycles
        #200;
        if (dut.counter_reg == 27'd0) begin
            $error("FAIL: expected counter_reg to advance after reset release");
            $finish;
        end

        $display("PASS: top_tb simulation completed successfully.");
        $finish;
    end
endmodule

`default_nettype wire
