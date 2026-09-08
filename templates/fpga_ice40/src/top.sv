// @@PROJECT_NAME@@ - Top-level module
// Target: Lattice iCE40-HX8K (Alchitry Cu / open-source FPGA flow)
// Language Standard: @@LANGUAGE_STD@@

`default_nettype none

module top #(
    parameter int CLK_FREQ_HZ = 100_000_000
) (
    input  logic       clk,
    input  logic       rst_n,
    output logic [7:0] led
);

    // 27-bit counter for clock division (~0.75 Hz on upper bits at 100MHz)
    logic [26:0] counter_reg;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            counter_reg <= 27'd0;
        end else begin
            counter_reg <= counter_reg + 1'b1;
        end
    end

    // Drive onboard LEDs with counter upper bits
    always_comb begin
        led = counter_reg[26:19];
    end

endmodule

`default_nettype wire
