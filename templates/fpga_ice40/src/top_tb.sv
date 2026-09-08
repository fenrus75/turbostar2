// @@PROJECT_NAME@@ - Testbench for top module
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
    initial clk = 0;
    always #5 clk = ~clk;

    initial begin
        $dumpfile("synth/top_tb.vcd");
        $dumpvars(0, top_tb);

        // Reset pulse (active-low)
        rst_n = 0;
        #20;
        rst_n = 1;

        // Run simulation for several clock cycles
        #200;

        $display("Testbench simulation completed successfully.");
        $finish;
    end
endmodule

`default_nettype wire
