// 8x8 Register File
// Contains 8 General Purpose Registers (gpr_select type) with shared bus interface
// Dual-port read: two simultaneous reads from two different addresses
// Single-port write: one write per clock cycle
// Address decoder select bus is shared across all internal registers

module regfile (
    input  wire       clk,           // Clock input
    input  wire       rst_n,         // Active-low asynchronous reset
    input  wire       we,           // Write enable
    input  wire [2:0] addr_a,       // Read address A (3-bit, 0-7)
    input  wire [2:0] addr_b,       // Read address B (3-bit, 0-7)
    input  wire [2:0] addr_w,       // Write address (3-bit, 0-7)
    input  wire [7:0] d,           // Data input (write data)
    output wire [7:0] q_a,          // Read output A (tristate)
    output wire [7:0] q_b           // Read output B (tristate)
);

    // Internal wires for each register's output
    wire [7:0] q [0:7];

    // Instantiate 8 GPR select registers
    // Each register has a hardwired address (0-7)
    gpr_select reg0 (.clk(clk), .rst_n(rst_n), .we(we), .addr(3'd0), .sel(addr_w), .d(d), .q(q[0]));
    gpr_select reg1 (.clk(clk), .rst_n(rst_n), .we(we), .addr(3'd1), .sel(addr_w), .d(d), .q(q[1]));
    gpr_select reg2 (.clk(clk), .rst_n(rst_n), .we(we), .addr(3'd2), .sel(addr_w), .d(d), .q(q[2]));
    gpr_select reg3 (.clk(clk), .rst_n(rst_n), .we(we), .addr(3'd3), .sel(addr_w), .d(d), .q(q[3]));
    gpr_select reg4 (.clk(clk), .rst_n(rst_n), .we(we), .addr(3'd4), .sel(addr_w), .d(d), .q(q[4]));
    gpr_select reg5 (.clk(clk), .rst_n(rst_n), .we(we), .addr(3'd5), .sel(addr_w), .d(d), .q(q[5]));
    gpr_select reg6 (.clk(clk), .rst_n(rst_n), .we(we), .addr(3'd6), .sel(addr_w), .d(d), .q(q[6]));
    gpr_select reg7 (.clk(clk), .rst_n(rst_n), .we(we), .addr(3'd7), .sel(addr_w), .d(d), .q(q[7]));

    // Multiplexer for read port A
    assign q_a = q[addr_a];

    // Multiplexer for read port B
    assign q_b = q[addr_b];

endmodule