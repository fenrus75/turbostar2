// @@PROJECT_NAME@@ - Top-level SystemVerilog module
// Language standard: @@LANGUAGE_STD@@

module top (
    input  logic       clk,
    input  logic       rst_n,
    input  logic [7:0] data_in,
    input  logic       valid_in,
    output logic [7:0] data_out,
    output logic       valid_out
);

    logic [7:0] reg_data;
    logic       reg_valid;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_data  <= 8'h00;
            reg_valid <= 1'b0;
        end else begin
            if (valid_in) begin
                reg_data  <= data_in;
                reg_valid <= 1'b1;
            end else begin
                reg_valid <= 1'b0;
            end
        end
    end

    always_comb begin
        data_out  = reg_data;
        valid_out = reg_valid;
    end

endmodule
