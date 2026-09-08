// @@PROJECT_NAME@@ - Testbench for top module
// Language standard: @@LANGUAGE_STD@@

`timescale 1ns/1ps

module top_tb;

    logic       clk;
    logic       rst_n;
    logic [7:0] data_in;
    logic       valid_in;
    logic [7:0] data_out;
    logic       valid_out;

    top dut (
        .clk(clk),
        .rst_n(rst_n),
        .data_in(data_in),
        .valid_in(valid_in),
        .data_out(data_out),
        .valid_out(valid_out)
    );

    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    initial begin
        rst_n    = 0;
        data_in  = 8'h00;
        valid_in = 0;

        #20;
        rst_n = 1;

        #10;
        data_in  = 8'h42;
        valid_in = 1;

        #10;
        valid_in = 0;

        #20;
        $finish;
    end

endmodule
